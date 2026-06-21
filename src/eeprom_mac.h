/*
 * EEPROM MAC Address Driver for OpenJBOD
 */

#ifndef EEPROM_MAC_H
#define EEPROM_MAC_H

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>

/* I2C address of the onboard Microchip 24AA025E (MAC + board-revision EEPROM). */
#define OPENJBOD_EEPROM_I2C_ADDR 0x50

/**
 * @brief Probe whether the 24AA025E EEPROM responds on the I2C bus.
 *
 * @param i2c_dev I2C controller the EEPROM is attached to
 * @return true if the device ACKs a 1-byte read, false otherwise
 */
static inline bool eeprom_24aa025e_present(const struct device *i2c_dev)
{
	uint8_t dummy;

	if (!device_is_ready(i2c_dev)) {
		return false;
	}
	return i2c_read(i2c_dev, &dummy, 1, OPENJBOD_EEPROM_I2C_ADDR) == 0;
}

/**
 * @brief Read MAC address from EEPROM
 *
 * @param mac_addr Buffer to store the 6-byte MAC address
 * @return 0 on success, negative error code on failure
 */
int read_mac_address(uint8_t *mac_addr);

#endif /* EEPROM_MAC_H */