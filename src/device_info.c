/*
 * Copyright (c) 2024 OpenJBOD Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <string.h>
#include "device_info.h"
#include "eeprom_mac.h"

LOG_MODULE_REGISTER(device_info, LOG_LEVEL_INF);

/* Git commit SHA will be injected at build time */
#ifndef OPENJBOD_GIT_COMMIT
#define OPENJBOD_GIT_COMMIT "unknown"
#endif

#define EEPROM_I2C_ADDR 0x50
#define BOARD_VERSION_REGISTER 0x00

static char cached_serial[OPENJBOD_SERIAL_MAX_LEN];
static char build_info_string[OPENJBOD_BUILD_INFO_MAX_LEN];
static char cached_board_revision[OPENJBOD_BOARD_REV_MAX_LEN];
static bool info_initialized = false;

static const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));

/* Forward declaration for uncached board revision function */
static int openjbod_device_info_get_board_revision_uncached(char *board_rev_buf, size_t buf_len);

/**
 * Convert device unique ID to hex string (similar to Python's binascii.hexlify().upper())
 */
static int device_id_to_hex_string(const uint8_t *device_id, size_t id_len, char *hex_string, size_t hex_buf_len)
{
	if (hex_buf_len < (id_len * 2) + 1) {
		return -EINVAL;
	}

	for (size_t i = 0; i < id_len; i++) {
		snprintf(&hex_string[i * 2], 3, "%02X", device_id[i]);
	}
	hex_string[id_len * 2] = '\0';
	
	return 0;
}

int openjbod_device_info_init(void)
{
	uint8_t unique_id[16];  /* RP2040 has 8-byte unique ID */
	int ret;

	if (info_initialized) {
		return 0;
	}

	LOG_INF("Initializing OpenJBOD device info");

	/* Get the unique board ID for serial number derivation */
	ret = hwinfo_get_device_id(unique_id, sizeof(unique_id));
	if (ret < 0) {
		LOG_ERR("Failed to get device unique ID: %d", ret);
		return ret;
	}

	size_t id_len = ret;
	
	/* Convert device ID to uppercase hex string for serial number */
	ret = device_id_to_hex_string(unique_id, id_len, cached_serial, sizeof(cached_serial));
	if (ret < 0) {
		LOG_ERR("Failed to convert device ID to hex string: %d", ret);
		return ret;
	}

	/* Create build info string with version and git commit */
	snprintf(build_info_string, sizeof(build_info_string), 
		 "%s (%s)", OPENJBOD_VERSION_STRING, OPENJBOD_GIT_COMMIT);

	/* Cache board revision to avoid repeated I2C calls */
	ret = openjbod_device_info_get_board_revision_uncached(cached_board_revision, sizeof(cached_board_revision));
	if (ret < 0) {
		LOG_WRN("Failed to get board revision: %d", ret);
		strcpy(cached_board_revision, "Unknown");
	}

	info_initialized = true;

	LOG_INF("Device serial number: %s", cached_serial);
	LOG_INF("Firmware version: %s", OPENJBOD_VERSION_STRING);
	LOG_INF("Bootloader version: %d", OPENJBOD_BOOTLOADER_VER);
	LOG_INF("Build info: %s", build_info_string);
	LOG_INF("Board revision: %s", cached_board_revision);

	return 0;
}

int openjbod_device_info_get_serial(char *serial_buf, size_t buf_len)
{
	if (!info_initialized) {
		int ret = openjbod_device_info_init();
		if (ret < 0) {
			return ret;
		}
	}

	if (buf_len < strlen(cached_serial) + 1) {
		return -EINVAL;
	}

	strcpy(serial_buf, cached_serial);
	return 0;
}

const char *openjbod_device_info_get_version(void)
{
	return OPENJBOD_VERSION_STRING;
}

const char *openjbod_device_info_get_build_info(void)
{
	if (!info_initialized) {
		int ret = openjbod_device_info_init();
		if (ret < 0) {
			return "error";
		}
	}

	return build_info_string;
}

void openjbod_device_info_get_version_components(uint8_t *major, uint8_t *minor, uint8_t *patch)
{
	if (major) *major = OPENJBOD_VERSION_MAJOR;
	if (minor) *minor = OPENJBOD_VERSION_MINOR;
	if (patch) *patch = OPENJBOD_VERSION_PATCH;
}

static int openjbod_device_info_get_board_revision_uncached(char *board_rev_buf, size_t buf_len)
{
	uint8_t version_byte = 0xFF;
	int ret;

	if (!board_rev_buf || buf_len < OPENJBOD_BOARD_REV_MAX_LEN) {
		return -EINVAL;
	}

	/* Check if MACROM is present on I2C bus */
	if (!eeprom_24aa025e_present(i2c_dev)) {
		LOG_INF("No MACROM detected - presuming Rev 4 board");
		strncpy(board_rev_buf, "Rev 4", buf_len - 1);
		board_rev_buf[buf_len - 1] = '\0';
		return 0;
	}

	/* MACROM is present, try to read version register */
	ret = i2c_write_read(i2c_dev, EEPROM_I2C_ADDR, 
			     &(uint8_t){BOARD_VERSION_REGISTER}, 1,
			     &version_byte, 1);
	
	if (ret < 0) {
		LOG_WRN("Failed to read board version from MACROM register 0x%02X: %d", 
			BOARD_VERSION_REGISTER, ret);
		strncpy(board_rev_buf, "Unknown Board Version", buf_len - 1);
		board_rev_buf[buf_len - 1] = '\0';
		return ret;
	}

	/* Interpret version byte according to the specified logic */
	if (version_byte == 1) {
		strncpy(board_rev_buf, "Rev 5", buf_len - 1);
		LOG_INF("Detected Rev 5 board (MACROM version register = 0x%02X)", version_byte);
	} else if (version_byte == 0xFF) {
		strncpy(board_rev_buf, "Unknown Board Version", buf_len - 1);
		LOG_INF("Unknown board version (MACROM version register = 0x%02X)", version_byte);
	} else {
		/* Handle other version values - might be future revisions */
		snprintf(board_rev_buf, buf_len, "Rev %d", version_byte);
		LOG_INF("Detected board revision %d (MACROM version register = 0x%02X)", 
			version_byte, version_byte);
	}
	
	board_rev_buf[buf_len - 1] = '\0';
	return 0;
}

int openjbod_device_info_get_board_revision(char *board_rev_buf, size_t buf_len)
{
	if (!info_initialized) {
		int ret = openjbod_device_info_init();
		if (ret < 0) {
			return ret;
		}
	}

	if (buf_len < strlen(cached_board_revision) + 1) {
		return -EINVAL;
	}

	strcpy(board_rev_buf, cached_board_revision);
	return 0;
}
