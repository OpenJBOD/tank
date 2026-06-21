/*
 * Copyright (c) 2025 The OpenJBOD Project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Network firmware upgrade (DFU). Streams a signed MCUboot image uploaded over
 * HTTP into the secondary slot, marks it pending (test), and reboots; MCUboot
 * swaps to it on the next boot. The new image is confirmed once it comes up
 * healthy (see firmware_update_mark_healthy), otherwise MCUboot reverts.
 */

#ifndef FIRMWARE_UPDATE_H_
#define FIRMWARE_UPDATE_H_

#include <zephyr/net/http/server.h>

/* POST /api/firmware - raw signed image body streamed to slot1. */
extern struct http_resource_detail_dynamic firmware_upload_resource_detail;

/*
 * Confirm the running image if it booted from a pending (test) swap and has not
 * yet been confirmed. Call when the device is known healthy (e.g. the web server
 * has started). No-op if already confirmed or not running under MCUboot.
 */
void firmware_update_mark_healthy(void);

#endif /* FIRMWARE_UPDATE_H_ */
