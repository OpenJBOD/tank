/*
 * Temperature sensor driver for OpenJBOD
 * Supports DS18B20 external sensor and RP2040 onboard temperature sensor
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include "temperature.h"

LOG_MODULE_REGISTER(tank_temps, LOG_LEVEL_INF);

/* Device tree references */
static const struct device *ds18b20_dev = DEVICE_DT_GET_OR_NULL(DT_ALIAS(ds18b20));
static const struct device *rp2040_temp_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(die_temp));

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
	
	return ret;
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
	temp_data->ds18b20_temp = 0.0f;
	temp_data->rp2040_temp = 0.0f;
	
	/* Read DS18B20 temperature */
	if (ds18b20_dev != NULL && device_is_ready(ds18b20_dev)) {
		/* Trigger temperature conversion */
		ret = sensor_sample_fetch(ds18b20_dev);
		if (ret == 0) {
			/* Wait for 9-bit conversion to complete (max 93.75ms) */
			k_msleep(100);  /* Add small margin for safety */
			
			/* Get temperature reading */
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
	
	if (!temp_data->ds18b20_valid && !temp_data->rp2040_valid) {
		LOG_ERR("No temperature sensors provided valid readings");
		return -EIO;
	}
	
	return 0;
}