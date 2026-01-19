/*
 * Automatic Fan Control for OpenJBOD
 * Controls EMC2301 fan based on DS18B20 temperature with customizable fan curve
 */

#ifndef FAN_CONTROL_H
#define FAN_CONTROL_H

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>
#include "settings.h"  /* For fan_curve_point definition */

/* Fan control configuration */
struct fan_control_config {
	bool enabled;                    /* Enable/disable automatic fan control */
	uint32_t update_interval_ms;     /* Update interval in milliseconds */
	struct fan_curve_point curve[5]; /* 5-point fan curve */
	uint8_t hysteresis_percent;      /* Hysteresis to prevent oscillation */
};

/**
 * Initialize fan control subsystem
 * @return 0 on success, negative error code otherwise
 */
int fan_control_init(void);

/**
 * Start the fan control background task
 * @return 0 on success, negative error code otherwise
 */
int fan_control_start(void);

/**
 * Stop the fan control background task
 * @return 0 on success, negative error code otherwise
 */
int fan_control_stop(void);

/**
 * Get current fan control configuration
 * @return Pointer to current configuration
 */
const struct fan_control_config* fan_control_get_config(void);

/**
 * Update fan control configuration
 * @param config New configuration to apply
 * @return 0 on success, negative error code otherwise
 */
int fan_control_set_config(const struct fan_control_config *config);

/**
 * Get current fan control status
 * @param current_temp Pointer to store current temperature
 * @param current_fan_percent Pointer to store current fan percentage
 * @param is_running Pointer to store running status
 * @return 0 on success, negative error code otherwise
 */
int fan_control_get_status(float *current_temp, uint8_t *current_fan_percent, bool *is_running);

#endif /* FAN_CONTROL_H */
