/*
 * Copyright (c) 2024 OpenJBOD Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DEVICE_INFO_H__
#define DEVICE_INFO_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SEMVER version - update as needed */
#define OPENJBOD_VERSION_MAJOR 1
#define OPENJBOD_VERSION_MINOR 0
#define OPENJBOD_VERSION_PATCH 0

#define OPENJBOD_VERSION_STRING "1.0.0"

/* MCUboot bootloader revision. Not semver - just a counter bumped whenever the
 * bootloader is changed (rare). It is reported by the application; since the
 * bootloader and app are built and flashed together (the "full" image), this
 * reflects the bootloader the running app was built against. Bumping it implies
 * shipping a new full image - a board must be re-flashed over BOOTSEL to pick up
 * a new bootloader (a DFU app-only update never replaces it).
 */
#define OPENJBOD_BOOTLOADER_VER 1

/* Maximum serial number string length (16 hex chars + null terminator) */
#define OPENJBOD_SERIAL_MAX_LEN 17

/* Maximum build info string length */
#define OPENJBOD_BUILD_INFO_MAX_LEN 64

/* Maximum board revision string length */
#define OPENJBOD_BOARD_REV_MAX_LEN 32

/**
 * Initialize device info module
 * @return 0 on success, negative errno on error
 */
int openjbod_device_info_init(void);

/**
 * Get device serial number (hex string derived from RP2040 unique ID)
 * @param serial_buf Buffer to store serial number string
 * @param buf_len Buffer length (should be at least OPENJBOD_SERIAL_MAX_LEN)
 * @return 0 on success, negative errno on error
 */
int openjbod_device_info_get_serial(char *serial_buf, size_t buf_len);

/**
 * Get firmware version string
 * @return Version string (e.g., "0.0.1")
 */
const char *openjbod_device_info_get_version(void);

/**
 * Get build information string
 * @return Build info string (e.g., "0.0.1+abc1234")
 */
const char *openjbod_device_info_get_build_info(void);

/**
 * Get version components
 */
void openjbod_device_info_get_version_components(uint8_t *major, uint8_t *minor, uint8_t *patch);

/**
 * Get board revision string based on MACROM detection
 * @param board_rev_buf Buffer to store board revision string
 * @param buf_len Buffer length (should be at least OPENJBOD_BOARD_REV_MAX_LEN)
 * @return 0 on success, negative errno on error
 */
int openjbod_device_info_get_board_revision(char *board_rev_buf, size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_INFO_H__ */