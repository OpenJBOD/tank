/*
 * Automatic Fan Control for OpenJBOD
 * Controls EMC2301 fan based on DS18B20 temperature with customizable fan curve
 */

#include "fan_control.h"
#include "temperature.h"
#include "emc2301.h"
#include "settings.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <math.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(fan_control, LOG_LEVEL_INF);

/* Fan control state */
static struct {
	struct k_thread thread;
	k_tid_t thread_id;
	bool running;
	bool initialized;
	float last_temperature;
	uint8_t last_fan_percent;
	bool increasing_direction;  /* Track direction for hysteresis */
} fan_control_state = {
	.running = false,
	.initialized = false,
	.last_temperature = 0.0f,
	.last_fan_percent = 0,
	.increasing_direction = true
};

/* Thread stack */
#define FAN_CONTROL_STACK_SIZE 2048
K_THREAD_STACK_DEFINE(fan_control_stack, FAN_CONTROL_STACK_SIZE);

/**
 * Linear interpolation between two fan curve points
 */
static uint8_t interpolate_fan_speed(float temp, 
				     const struct fan_curve_point *p1,
				     const struct fan_curve_point *p2)
{
	if (temp <= p1->temperature) {
		return p1->fan_percent;
	}
	if (temp >= p2->temperature) {
		return p2->fan_percent;
	}
	
	/* Linear interpolation: y = y1 + (x - x1) * (y2 - y1) / (x2 - x1) */
	float ratio = (temp - p1->temperature) / (p2->temperature - p1->temperature);
	float interpolated = p1->fan_percent + ratio * (p2->fan_percent - p1->fan_percent);
	
	/* Clamp to valid range and round */
	if (interpolated < 0) interpolated = 0;
	if (interpolated > 100) interpolated = 100;
	
	return (uint8_t)(interpolated + 0.5f);
}

/**
 * Calculate fan speed percentage based on temperature and fan curve from settings
 */
static uint8_t calculate_fan_speed(float temperature)
{
	const struct openjbod_settings *settings = openjbod_settings_get();
	const struct fan_curve_point *curve = settings->environment.fan_curve;
	
	/* Find the appropriate segment in the fan curve */
	for (int i = 0; i < 4; i++) {
		if (temperature <= curve[i + 1].temperature) {
			return interpolate_fan_speed(temperature, &curve[i], &curve[i + 1]);
		}
	}
	
	/* Temperature is above the highest point */
	return curve[4].fan_percent;
}

/**
 * Apply hysteresis to prevent oscillation
 */
static uint8_t apply_hysteresis(float current_temp, uint8_t calculated_percent)
{
	const struct openjbod_settings *settings = openjbod_settings_get();
	uint8_t hysteresis_percent = settings->environment.fan_hysteresis_percent;
	
	/* Determine if temperature is increasing or decreasing */
	bool temp_increasing = current_temp > fan_control_state.last_temperature;
	
	/* Apply hysteresis only if direction has changed */
	if (temp_increasing != fan_control_state.increasing_direction) {
		int percent_diff = calculated_percent - fan_control_state.last_fan_percent;
		
		/* If the change is small (within hysteresis), keep the old value */
		if (abs(percent_diff) <= hysteresis_percent) {
			LOG_DBG("Hysteresis applied: keeping fan at %d%% (calculated %d%%)",
				fan_control_state.last_fan_percent, calculated_percent);
			return fan_control_state.last_fan_percent;
		}
		
		/* Update direction tracking */
		fan_control_state.increasing_direction = temp_increasing;
	}
	
	return calculated_percent;
}

/**
 * Fan control background thread
 */
static void fan_control_thread_func(void *arg1, void *arg2, void *arg3)
{
	struct temperature_data temp_data;
	int ret;
	
	LOG_INF("Fan control thread started");
	
	while (fan_control_state.running) {
		const struct openjbod_settings *settings = openjbod_settings_get();
		
		/* Check if external fan control is enabled or fan control is disabled */
		if (settings->environment.use_external_fan_control) {
			/* External fan control enabled, sleep longer */
			k_msleep(settings->environment.fan_update_interval_ms * 2);
			continue;
		}
		
		/* Read temperature */
		ret = temperature_read(&temp_data);
		if (ret != 0) {
			LOG_WRN("Failed to read temperature: %d", ret);
			k_msleep(settings->environment.fan_update_interval_ms);
			continue;
		}
		
		/* Use DS18B20 temperature if valid, otherwise skip this cycle */
		if (!temp_data.ds18b20_valid) {
			LOG_WRN("DS18B20 temperature not valid, skipping fan control update");
			k_msleep(settings->environment.fan_update_interval_ms);
			continue;
		}
		
		float current_temp = temp_data.ds18b20_temp;
		
		/* Calculate desired fan speed */
		uint8_t calculated_percent = calculate_fan_speed(current_temp);
		
		/* Apply hysteresis */
		uint8_t target_percent = apply_hysteresis(current_temp, calculated_percent);
		
		/* Convert percentage to duty cycle */
		uint8_t duty = emc2301_percent_to_duty(target_percent);
		
		/* Set fan speed */
		ret = emc2301_set_pwm_duty(duty);
		if (ret != 0) {
			LOG_WRN("Failed to set fan PWM duty: %d", ret);
		} else {
			LOG_DBG("Temperature: %.2f°C, Fan speed: %d%% (duty: %d)",
				(double)current_temp, target_percent, duty);
		}
		
		/* Update state tracking */
		fan_control_state.last_temperature = current_temp;
		fan_control_state.last_fan_percent = target_percent;
		
		/* Sleep until next update */
		k_msleep(settings->environment.fan_update_interval_ms);
	}
	
	LOG_INF("Fan control thread stopped");
}

int fan_control_init(void)
{
	if (fan_control_state.initialized) {
		return 0;
	}
	
	const struct openjbod_settings *settings = openjbod_settings_get();
	
	LOG_INF("Initializing fan control subsystem");
	LOG_INF("External fan control: %s", settings->environment.use_external_fan_control ? "enabled" : "disabled");
	LOG_INF("Fan curve points:");
	for (int i = 0; i < 5; i++) {
		LOG_INF("  %.1f°C -> %d%%",
			(double)settings->environment.fan_curve[i].temperature,
			settings->environment.fan_curve[i].fan_percent);
	}
	LOG_INF("Update interval: %d ms", settings->environment.fan_update_interval_ms);
	LOG_INF("Hysteresis: %d%%", settings->environment.fan_hysteresis_percent);
	
	fan_control_state.initialized = true;
	return 0;
}

int fan_control_start(void)
{
	if (!fan_control_state.initialized) {
		LOG_ERR("Fan control not initialized");
		return -ENODEV;
	}
	
	if (fan_control_state.running) {
		LOG_WRN("Fan control already running");
		return 0;
	}
	
	LOG_INF("Starting fan control background task");
	
	fan_control_state.running = true;
	
	fan_control_state.thread_id = k_thread_create(&fan_control_state.thread,
						      fan_control_stack,
						      FAN_CONTROL_STACK_SIZE,
						      fan_control_thread_func,
						      NULL, NULL, NULL,
						      K_PRIO_COOP(7),  /* Medium priority */
						      0, K_NO_WAIT);
	
	if (!fan_control_state.thread_id) {
		LOG_ERR("Failed to create fan control thread");
		fan_control_state.running = false;
		return -ENOMEM;
	}
	
	k_thread_name_set(fan_control_state.thread_id, "fan_control");
	
	return 0;
}

int fan_control_stop(void)
{
	if (!fan_control_state.running) {
		return 0;
	}
	
	LOG_INF("Stopping fan control background task");
	
	fan_control_state.running = false;
	
	/* Wait for thread to finish */
	k_thread_join(fan_control_state.thread_id, K_SECONDS(5));
	
	return 0;
}

const struct fan_control_config* fan_control_get_config(void)
{
	static struct fan_control_config config;
	const struct openjbod_settings *settings = openjbod_settings_get();
	
	/* Convert settings to fan_control_config format */
	config.enabled = !settings->environment.use_external_fan_control;
	config.update_interval_ms = settings->environment.fan_update_interval_ms;
	config.hysteresis_percent = settings->environment.fan_hysteresis_percent;
	
	for (int i = 0; i < 5; i++) {
		config.curve[i] = settings->environment.fan_curve[i];
	}
	
	return &config;
}

int fan_control_set_config(const struct fan_control_config *config)
{
	if (!config) {
		return -EINVAL;
	}
	
	LOG_INF("Updating fan control configuration");
	
	/* Validate configuration */
	for (int i = 0; i < 4; i++) {
		if (config->curve[i].temperature >= config->curve[i + 1].temperature) {
			LOG_ERR("Invalid fan curve: temperatures must be increasing");
			return -EINVAL;
		}
		if (config->curve[i].fan_percent > 100 || config->curve[i + 1].fan_percent > 100) {
			LOG_ERR("Invalid fan curve: percentages must be 0-100");
			return -EINVAL;
		}
	}
	
	if (config->update_interval_ms < 1000 || config->update_interval_ms > 60000) {
		LOG_ERR("Invalid update interval: must be 1000-60000 ms");
		return -EINVAL;
	}
	
	if (config->hysteresis_percent > 50) {
		LOG_ERR("Invalid hysteresis: must be 0-50%%");
		return -EINVAL;
	}
	
	/* Convert fan_control_config to environment settings format */
	struct environment_settings env_settings;
	const struct openjbod_settings *current_settings = openjbod_settings_get();
	
	/* Copy current environment settings and update fan control parts */
	env_settings = current_settings->environment;
	env_settings.use_external_fan_control = !config->enabled;
	env_settings.fan_update_interval_ms = config->update_interval_ms;
	env_settings.fan_hysteresis_percent = config->hysteresis_percent;
	
	for (int i = 0; i < 5; i++) {
		env_settings.fan_curve[i] = config->curve[i];
	}
	
	/* Save to settings */
	int ret = openjbod_settings_set_environment(&env_settings);
	if (ret != 0) {
		LOG_ERR("Failed to save environment settings: %d", ret);
		return ret;
	}
	
	LOG_INF("Fan control configuration updated");
	LOG_INF("Enabled: %s", config->enabled ? "true" : "false");
	LOG_INF("Update interval: %d ms", config->update_interval_ms);
	LOG_INF("Hysteresis: %d%%", config->hysteresis_percent);
	
	return 0;
}

int fan_control_get_status(float *current_temp, uint8_t *current_fan_percent, bool *is_running)
{
	if (current_temp) {
		*current_temp = fan_control_state.last_temperature;
	}
	
	if (current_fan_percent) {
		*current_fan_percent = fan_control_state.last_fan_percent;
	}
	
	if (is_running) {
		*is_running = fan_control_state.running;
	}
	
	return 0;
}
