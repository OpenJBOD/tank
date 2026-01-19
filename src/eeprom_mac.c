/*
 * EEPROM MAC Address Driver for OpenJBOD
 * Reads MAC address from Microchip 24AA025E EEPROM
 * Falls back to LAA (Locally Administered Address) generation from unique board ID
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(eeprom_mac, LOG_LEVEL_INF);

#define MAC_ADDRESS_OFFSET 0xFA
#define MAC_ADDRESS_SIZE 6
#define EEPROM_I2C_ADDR 0x50

static const struct device *eeprom_dev = DEVICE_DT_GET(DT_NODELABEL(eeprom));
static const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));

/**
 * Check if EEPROM is present on I2C bus
 * Returns true if device responds to its address
 */
static bool eeprom_is_present(void)
{
	uint8_t dummy_data;
	int ret;

	if (!device_is_ready(i2c_dev)) {
		return false;
	}

	/* Try a simple read to see if device ACKs its address */
	ret = i2c_read(i2c_dev, &dummy_data, 1, EEPROM_I2C_ADDR);
	return (ret == 0);
}

/**
 * Generate LAA (Locally Administered Address) MAC from board unique ID
 * Based on MicroPython implementation for W5500 driver
 */
static int generate_laa_mac(uint8_t *mac_addr)
{
	uint8_t unique_id[16];  /* RP2040 has 8-byte unique ID, but buffer larger for safety */
	size_t id_len;
	int ret;

	/* Get the unique board ID */
	ret = hwinfo_get_device_id(unique_id, sizeof(unique_id));
	if (ret < 0) {
		LOG_ERR("Failed to get device unique ID: %d", ret);
		return ret;
	}
	
	id_len = ret;
	LOG_INF("Device unique ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
		unique_id[0], unique_id[1], unique_id[2], unique_id[3],
		unique_id[4], unique_id[5], unique_id[6], unique_id[7]);

	/* Ensure we have at least 8 bytes for the algorithm */
	if (id_len < 8) {
		LOG_ERR("Unique ID too short: %zu bytes (need at least 8)", id_len);
		return -EINVAL;
	}

	/* Generate LAA MAC using the algorithm from MicroPython */
	mac_addr[0] = 0x02;  /* LAA range (bit 1 set = locally administered) */
	mac_addr[1] = (unique_id[7] << 4) | (unique_id[6] & 0x0f);
	mac_addr[2] = (unique_id[5] << 4) | (unique_id[4] & 0x0f);
	mac_addr[3] = (unique_id[3] << 4) | (unique_id[2] & 0x0f);
	mac_addr[4] = unique_id[1];
	mac_addr[5] = (unique_id[0] << 2) | 0x02; 
	/* hack: 0x02 in mac_addr[5] replaces an idx in the MicroPython W5500 driver. */
	/* This ensures the MAC remains identical to the Python software. */

	LOG_INF("Generated LAA MAC address: %02x:%02x:%02x:%02x:%02x:%02x",
		mac_addr[0], mac_addr[1], mac_addr[2],
		mac_addr[3], mac_addr[4], mac_addr[5]);

	return 0;
}

int read_mac_address(uint8_t *mac_addr)
{
	int ret;

	/* Check for MACROM on I2C bus (any I2C errors during this check are expected on boards without MACROM) */
	LOG_INF("Checking for MACROM on I2C bus...");
	if (!eeprom_is_present()) {
		LOG_INF("No MACROM detected on I2C bus - board revision likely lacks onboard EEPROM");
		goto fallback_laa;
	}

	/* EEPROM is present, try to read from it */
	if (device_is_ready(eeprom_dev)) {
		ret = eeprom_read(eeprom_dev, MAC_ADDRESS_OFFSET, mac_addr, MAC_ADDRESS_SIZE);
		if (ret >= 0) {
			/* Validate MAC address - check if it's not all zeros or all 0xFF */
			bool valid_mac = false;
			for (int i = 0; i < MAC_ADDRESS_SIZE; i++) {
				if (mac_addr[i] != 0x00 && mac_addr[i] != 0xFF) {
					valid_mac = true;
					break;
				}
			}
			
			if (valid_mac) {
				LOG_INF("MAC address read from EEPROM: %02x:%02x:%02x:%02x:%02x:%02x",
					mac_addr[0], mac_addr[1], mac_addr[2],
					mac_addr[3], mac_addr[4], mac_addr[5]);
				return 0;
			} else {
				LOG_INF("EEPROM contains invalid MAC address (all zeros or all 0xFF) - board likely unprogrammed");
			}
		} else {
			LOG_WRN("Failed to read MAC address from EEPROM: %d", ret);
		}
	} else {
		LOG_WRN("EEPROM device not ready despite I2C presence check");
	}

fallback_laa:

	/* EEPROM failed or not available, fallback to LAA generation */
	LOG_INF("Using LAA (Locally Administered Address) MAC generation from board unique ID");
	ret = generate_laa_mac(mac_addr);
	if (ret < 0) {
		LOG_ERR("Failed to generate LAA MAC address: %d", ret);
		return ret;
	}

	return 0;
}