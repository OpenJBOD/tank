/*
 * Temperature sensor driver for OpenJBOD
 * Supports DS18B20 external sensor and RP2040 onboard temperature sensor
 */

#ifndef TEMPERATURE_H
#define TEMPERATURE_H

#include <zephyr/kernel.h>

struct temperature_data {
	float ds18b20_temp;      /* Onboard DS18B20 (GPIO18) */
	float rp2040_temp;       /* RP2040 onboard die temperature */
	float ds18b20_ext_temp;  /* External DS18B20 on the GPIO11 pin header */
	bool ds18b20_valid;
	bool rp2040_valid;
	bool ds18b20_ext_valid;
};

/* Temperature source selection (matches enum temp_source in settings.h). */
#define TEMP_SOURCE_ONBOARD 0
#define TEMP_SOURCE_HEADER  1

/**
 * Initialize temperature sensors
 * @return 0 on success, negative error code otherwise
 */
int temperature_init(void);

/**
 * Read temperature from all sensors.
 * Handles the 93.75ms delay required for each DS18B20 9-bit conversion.
 * @param temp_data Pointer to structure to store temperature readings
 * @return 0 on success, negative error code otherwise
 */
int temperature_read(struct temperature_data *temp_data);

/**
 * Return the most recent cached readings without triggering a conversion. A DS18B20
 * conversion blocks ~750ms, so request handlers should use this instead of
 * temperature_read(). The cache is seeded at init and refreshed by the fan-control
 * thread on each fan interval.
 * @param out  Pointer to structure to receive the cached readings.
 * @return 0 if the cache holds a reading, -EAGAIN if not yet populated (out is
 *         zeroed / all-invalid), -EINVAL if out is NULL.
 */
int temperature_read_cached(struct temperature_data *out);

/**
 * Store a fresh reading into the cache. Called by the background reader (the
 * fan-control thread) after temperature_read().
 */
void temperature_cache_store(const struct temperature_data *data);

/**
 * Whether the external (header) DS18B20 was detected at boot.
 * @return true if the header probe responded during temperature_init().
 */
bool temperature_ext_present(void);

/**
 * Select the active temperature for a requested source, applying fallback:
 * selected DS18B20 -> the other DS18B20 -> RP2040 die.
 * @param temp_data    Readings from temperature_read()
 * @param source       Requested source (TEMP_SOURCE_ONBOARD / TEMP_SOURCE_HEADER)
 * @param temp_c       Out: selected temperature in Celsius
 * @param source_name  Out: human name of the source actually used
 *                     ("onboard" / "header" / "rp2040")
 * @return 0 on success, -EIO if no source is valid
 */
int temperature_get_active(const struct temperature_data *temp_data, uint8_t source,
			   float *temp_c, const char **source_name);

#endif /* TEMPERATURE_H */