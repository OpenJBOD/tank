/*
 * Copyright (c) 2025 The OpenJBOD Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "firmware_update.h"

#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/net/http/server.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/logging/log.h>

#include "http/auth.h"

LOG_MODULE_REGISTER(tank_firmware, LOG_LEVEL_INF);

/* Delay before rebooting so the HTTP 200 response flushes and the socket closes. */
#define REBOOT_DELAY_MS 1500

/* The HTTP server services all clients from one thread, and the streaming write
 * targets a single flash image context, so only one upload can be in flight.
 * State is reset on completion or abort.
 */
static struct flash_img_context img_ctx;
static struct http_client_ctx *upload_owner;
static bool upload_started;
static int upload_err;
static size_t upload_bytes;

static char response_buffer[160];

static void reboot_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	LOG_INF("Rebooting to apply firmware upgrade");
	sys_reboot(SYS_REBOOT_COLD);
}
static K_WORK_DELAYABLE_DEFINE(reboot_work, reboot_work_handler);

static void respond(struct http_response_ctx *response_ctx, enum http_status status,
		    const char *json)
{
	strncpy(response_buffer, json, sizeof(response_buffer) - 1);
	response_buffer[sizeof(response_buffer) - 1] = '\0';
	response_ctx->status = status;
	response_ctx->body = response_buffer;
	response_ctx->body_len = strlen(response_buffer);
	response_ctx->final_chunk = true;
}

static void reset_state(void)
{
	upload_owner = NULL;
	upload_started = false;
	upload_err = 0;
	upload_bytes = 0;
}

static int firmware_upload_handler(struct http_client_ctx *client, enum http_data_status status,
				   const struct http_request_ctx *request_ctx,
				   struct http_response_ctx *response_ctx, void *user_data)
{
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_DATA_ABORTED) {
		if (upload_started && upload_owner == client) {
			LOG_WRN("Firmware upload aborted after %zu bytes", upload_bytes);
			reset_state();
		}
		return 0;
	}

	/* Start of a new upload: authenticate before writing anything to flash. */
	if (!upload_started) {
		if (http_check_auth(client) != 0) {
			LOG_WRN("Unauthenticated firmware upload rejected");
			http_send_auth_required_response(response_ctx);
			return 0;
		}

		int rc = flash_img_init(&img_ctx);

		if (rc != 0) {
			LOG_ERR("flash_img_init failed: %d", rc);
			respond(response_ctx, HTTP_500_INTERNAL_SERVER_ERROR,
				"{\"success\":false,\"message\":\"flash init failed\"}");
			return 0;
		}

		upload_owner = client;
		upload_started = true;
		upload_err = 0;
		upload_bytes = 0;
		LOG_INF("Firmware upload started, staging into the secondary slot");
	} else if (upload_owner != client) {
		LOG_WRN("Rejected concurrent firmware upload");
		respond(response_ctx, HTTP_409_CONFLICT,
			"{\"success\":false,\"message\":\"Another firmware upload is in progress\"}");
		return 0;
	}

	/* Stream this chunk straight to the secondary slot (the image is too large
	 * to buffer). flush=true on the final chunk pads and closes the area.
	 */
	if (upload_err == 0 && request_ctx->data_len > 0) {
		bool flush = (status == HTTP_SERVER_DATA_FINAL);
		int rc = flash_img_buffered_write(&img_ctx, request_ctx->data,
						  request_ctx->data_len, flush);

		if (rc != 0) {
			LOG_ERR("flash_img_buffered_write failed at %zu bytes: %d",
				upload_bytes, rc);
			upload_err = rc;
		} else {
			upload_bytes += request_ctx->data_len;
		}
	}

	if (status != HTTP_SERVER_DATA_FINAL) {
		return 0;
	}

	/* Final chunk - finalize the staged image. */
	if (upload_err != 0) {
		int err = upload_err;

		reset_state();
		snprintf(response_buffer, sizeof(response_buffer),
			 "{\"success\":false,\"message\":\"flash write failed (%d)\"}", err);
		response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
		response_ctx->body = response_buffer;
		response_ctx->body_len = strlen(response_buffer);
		response_ctx->final_chunk = true;
		return 0;
	}

	size_t total = flash_img_bytes_written(&img_ctx);

	/* Mark the staged image pending as a TEST swap: MCUboot boots it once, then
	 * reverts unless the new firmware confirms itself (firmware_update_mark_healthy).
	 */
	int rc = boot_request_upgrade(BOOT_UPGRADE_TEST);

	reset_state();

	if (rc != 0) {
		LOG_ERR("boot_request_upgrade failed: %d", rc);
		respond(response_ctx, HTTP_500_INTERNAL_SERVER_ERROR,
			"{\"success\":false,\"message\":\"could not set pending image\"}");
		return 0;
	}

	LOG_INF("Firmware staged (%zu bytes), pending swap; rebooting in %d ms",
		total, REBOOT_DELAY_MS);
	snprintf(response_buffer, sizeof(response_buffer),
		 "{\"success\":true,\"bytes\":%zu,\"message\":\"rebooting to apply update\"}",
		 total);
	response_ctx->status = HTTP_200_OK;
	response_ctx->body = response_buffer;
	response_ctx->body_len = strlen(response_buffer);
	response_ctx->final_chunk = true;

	k_work_schedule(&reboot_work, K_MSEC(REBOOT_DELAY_MS));
	return 0;
}

struct http_resource_detail_dynamic firmware_upload_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
		.content_type = "application/json",
	},
	.cb = firmware_upload_handler,
	.user_data = NULL,
};

void firmware_update_mark_healthy(void)
{
	if (boot_is_img_confirmed()) {
		return;
	}

	int rc = boot_write_img_confirmed();

	if (rc != 0) {
		LOG_ERR("Failed to confirm running image: %d", rc);
	} else {
		LOG_INF("Running image confirmed (upgrade kept)");
	}
}
