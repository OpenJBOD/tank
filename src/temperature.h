/*
 * Temperature sensor driver for OpenJBOD
 * Supports DS18B20 external sensor and RP2040 onboard temperature sensor
 */

#ifndef TEMPERATURE_H
#define TEMPERATURE_H

#include <zephyr/kernel.h>

struct temperature_data {
	float ds18b20_temp;
	float rp2040_temp;
	bool ds18b20_valid;
	bool rp2040_valid;
};

/**
 * Initialize temperature sensors
 * @return 0 on success, negative error code otherwise
 */
int temperature_init(void);

/**
 * Read temperature from both sensors
 * This function handles the 93.75ms delay required for DS18B20 9-bit conversion
 * @param temp_data Pointer to structure to store temperature readings
 * @return 0 on success, negative error code otherwise
 */
int temperature_read(struct temperature_data *temp_data);

#endif /* TEMPERATURE_H */