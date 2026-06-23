/*
 * Temperature sensor driver for OpenJBOD
 * Supports DS18B20 external sensor and RP2040 onboard temperature sensor
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include "temperature.h"

LOG_MODULE_REGISTER(tank_temps, LOG_LEVEL_INF);

/* Device tree references */
static const struct device *ds18b20_dev = DEVICE_DT_GET_OR_NULL(DT_ALIAS(ds18b20));
static const struct device *ds18b20_ext_dev = DEVICE_DT_GET_OR_NULL(DT_ALIAS(ds18b20_ext));
static const struct device *rp2040_temp_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(die_temp));

/* Latched at boot: whether the optional header DS18B20 actually responded. */
static bool ext_present;

/* Cached readings. A DS18B20 conversion blocks ~750ms, so a background reader (the
 * fan-control thread, every fan interval) refreshes this and HTTP/status callers read
 * the cache instead of triggering a fresh blocking conversion per request. Seeded at
 * init so it is valid from boot.
 */
static struct temperature_data temp_cache;
static bool temp_cache_valid;
static K_MUTEX_DEFINE(temp_cache_lock);

void temperature_cache_store(const struct temperature_data *data)
{
	if (data == NULL) {
		return;
	}
	k_mutex_lock(&temp_cache_lock, K_FOREVER);
	temp_cache = *data;
	temp_cache_valid = true;
	k_mutex_unlock(&temp_cache_lock);
}

int temperature_read_cached(struct temperature_data *out)
{
	int rc;

	if (out == NULL) {
		return -EINVAL;
	}
	k_mutex_lock(&temp_cache_lock, K_FOREVER);
	if (temp_cache_valid) {
		*out = temp_cache;
		rc = 0;
	} else {
		memset(out, 0, sizeof(*out)); /* all-invalid until the first read lands */
		rc = -EAGAIN;
	}
	k_mutex_unlock(&temp_cache_lock);
	return rc;
}

int temperature_init(void)
{
	int ret = 0;

	/* Check DS18B20 sensor */
	if (ds18b20_dev == NULL) {
		LOG_WRN("DS18B20 sensor not found in device tree");
	} else if (!device_is_ready(ds18b20_dev)) {
		LOG_ERR("DS18B20 sensor device is not ready");
		ret = -ENODEV;
	} else {
		LOG_INF("DS18B20 sensor initialized successfully");
	}

	/* Detect the optional external DS18B20 on the GPIO11 header. The bus is
	 * always present in DT, so probe with an actual conversion to see if a
	 * sensor is physically connected.
	 */
	ext_present = false;
	if (ds18b20_ext_dev != NULL && device_is_ready(ds18b20_ext_dev)) {
		/* sample_fetch waits for the conversion internally (no extra delay). */
		if (sensor_sample_fetch(ds18b20_ext_dev) == 0) {
			struct sensor_value v;
			if (sensor_channel_get(ds18b20_ext_dev, SENSOR_CHAN_AMBIENT_TEMP, &v) == 0) {
				ext_present = true;
			}
		}
		LOG_INF("External (header) DS18B20: %s",
			ext_present ? "detected" : "not present");
	} else {
		LOG_WRN("External DS18B20 device not ready/found in device tree");
	}

	/* Check RP2040 onboard temperature sensor */
	if (rp2040_temp_dev == NULL) {
		LOG_WRN("RP2040 temperature sensor not found in device tree");
	} else if (!device_is_ready(rp2040_temp_dev)) {
		LOG_ERR("RP2040 temperature sensor device is not ready");
		ret = -ENODEV;
	} else {
		LOG_INF("RP2040 temperature sensor initialized successfully");
	}

	if (ds18b20_dev == NULL && rp2040_temp_dev == NULL) {
		LOG_ERR("No temperature sensors available");
		return -ENODEV;
	}

	/* Seed the cache so /api/status has data from the first request, before the
	 * fan-control thread's first refresh.
	 */
	struct temperature_data seed;

	if (temperature_read(&seed) == 0) {
		temperature_cache_store(&seed);
	}

	return ret;
}

bool temperature_ext_present(void)
{
	return ext_present;
}

int temperature_read(struct temperature_data *temp_data)
{
	int ret;
	struct sensor_value temp_val;
	
	if (temp_data == NULL) {
		return -EINVAL;
	}
	
	/* Initialize structure */
	temp_data->ds18b20_valid = false;
	temp_data->rp2040_valid = false;
	temp_data->ds18b20_ext_valid = false;
	temp_data->ds18b20_temp = 0.0f;
	temp_data->rp2040_temp = 0.0f;
	temp_data->ds18b20_ext_temp = 0.0f;

	/* Read DS18B20 temperature. sensor_sample_fetch() already performs the
	 * conversion AND waits the resolution-dependent conversion time internally
	 * (see the ds18b20 driver), so no extra delay is needed here.
	 */
	if (ds18b20_dev != NULL && device_is_ready(ds18b20_dev)) {
		ret = sensor_sample_fetch(ds18b20_dev);
		if (ret == 0) {
			ret = sensor_channel_get(ds18b20_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp_val);
			if (ret == 0) {
				temp_data->ds18b20_temp = sensor_value_to_float(&temp_val);
				temp_data->ds18b20_valid = true;
				LOG_DBG("DS18B20 temperature: %.3f°C", (double)temp_data->ds18b20_temp);
			} else {
				LOG_WRN("Failed to get DS18B20 temperature: %d", ret);
			}
		} else {
			LOG_WRN("Failed to fetch DS18B20 sample: %d", ret);
		}
	}
	
	/* Read external (header) DS18B20 temperature. Always attempt it so the
	 * valid flag reflects live presence (handles unplug after boot).
	 */
	if (ext_present && ds18b20_ext_dev != NULL && device_is_ready(ds18b20_ext_dev)) { {
		ret = sensor_sample_fetch(ds18b20_ext_dev);
		if (ret == 0) {
			ret = sensor_channel_get(ds18b20_ext_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp_val);
			if (ret == 0) {
				temp_data->ds18b20_ext_temp = sensor_value_to_float(&temp_val);
				temp_data->ds18b20_ext_valid = true;
				LOG_DBG("External DS18B20 temperature: %.3f°C",
					(double)temp_data->ds18b20_ext_temp);
			} else {
				LOG_DBG("Failed to get external DS18B20 temperature: %d", ret);
			}
		} else {
			LOG_DBG("Failed to fetch external DS18B20 sample: %d", ret);
		}
	}

	/* Read RP2040 onboard temperature */
	if (rp2040_temp_dev != NULL && device_is_ready(rp2040_temp_dev)) {
		ret = sensor_sample_fetch(rp2040_temp_dev);
		if (ret == 0) {
			ret = sensor_channel_get(rp2040_temp_dev, SENSOR_CHAN_DIE_TEMP, &temp_val);
			if (ret == 0) {
				temp_data->rp2040_temp = sensor_value_to_float(&temp_val);
				temp_data->rp2040_valid = true;
				LOG_DBG("RP2040 temperature: %.3f°C", (double)temp_data->rp2040_temp);
			} else {
				LOG_WRN("Failed to get RP2040 temperature: %d", ret);
			}
		} else {
			LOG_WRN("Failed to fetch RP2040 sample: %d", ret);
		}
	}
	
	if (!temp_data->ds18b20_valid && !temp_data->rp2040_valid &&
	    !temp_data->ds18b20_ext_valid) {
		LOG_ERR("No temperature sensors provided valid readings");
		return -EIO;
	}

	return 0;
}

int temperature_get_active(const struct temperature_data *temp_data, uint8_t source,
			   float *temp_c, const char **source_name)
{
	if (temp_data == NULL || temp_c == NULL || source_name == NULL) {
		return -EINVAL;
	}

	/* Honour the requested probe first, then fall back to the other DS18B20,
	 * then to the RP2040 die temperature so fan control never starves.
	 */
	if (source == TEMP_SOURCE_HEADER) {
		if (temp_data->ds18b20_ext_valid) {
			*temp_c = temp_data->ds18b20_ext_temp;
			*source_name = "header";
			return 0;
		}
		if (temp_data->ds18b20_valid) {
			*temp_c = temp_data->ds18b20_temp;
			*source_name = "onboard";
			return 0;
		}
	} else {
		if (temp_data->ds18b20_valid) {
			*temp_c = temp_data->ds18b20_temp;
			*source_name = "onboard";
			return 0;
		}
		if (temp_data->ds18b20_ext_valid) {
			*temp_c = temp_data->ds18b20_ext_temp;
			*source_name = "header";
			return 0;
		}
	}

	if (temp_data->rp2040_valid) {
		*temp_c = temp_data->rp2040_temp;
		*source_name = "rp2040";
		return 0;
	}

	return -EIO;
}
