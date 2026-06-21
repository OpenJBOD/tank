#include "http/routes/routes_reset.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

#include "http/auth.h"
#include "settings.h"

LOG_MODULE_REGISTER(tank_http_reset, LOG_LEVEL_INF);

/* Backup/restore of the raw settings file (FATFS). Restore stages to a temp file then
 * renames + reboots so the settings subsystem reloads it. */
#define SETTINGS_PATH      CONFIG_SETTINGS_FILE_PATH
#define RESTORE_TMP_PATH   "/settings/restore.tmp"
#define BACKUP_BUF_SIZE    12288   /* covers a full config incl. custom TLS cert+key */
#define RESTORE_MAX_SIZE   65536   /* hard ceiling so a bad upload can't fill the partition */

static void reboot_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	LOG_INF("Executing scheduled reboot");
	sys_reboot(SYS_REBOOT_COLD);
}

K_WORK_DELAYABLE_DEFINE(reset_reboot_work, reboot_work_handler);

static void schedule_reboot(void)
{
	/* Give the HTTP stack time to flush the response before rebooting. */
	(int)k_work_schedule(&reset_reboot_work, K_MSEC(500));
}

static int reset_device_handler(struct http_client_ctx *client, enum http_data_status status,
			   const struct http_request_ctx *request_ctx,
			   struct http_response_ctx *response_ctx,
			   void *user_data)
{
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status != HTTP_SERVER_DATA_FINAL) {
		return 0;
	}

	int auth_result = http_check_auth(client);
	if (auth_result != 0) {
		LOG_WRN("Authentication failed for device reset endpoint");
		http_send_auth_required_response(response_ctx);
		return 0;
	}

	static char response_buffer[] = "{\"status\":\"reset_device\",\"result\":\"accepted\"}";

	response_ctx->status = HTTP_200_OK;
	response_ctx->body = response_buffer;
	response_ctx->body_len = strlen(response_buffer);
	response_ctx->final_chunk = true;

	LOG_INF("Device reboot requested via API");
	schedule_reboot();
	return 0;
}

static int reset_config_handler(struct http_client_ctx *client, enum http_data_status status,
			    const struct http_request_ctx *request_ctx,
			    struct http_response_ctx *response_ctx,
			    void *user_data)
{
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status != HTTP_SERVER_DATA_FINAL) {
		return 0;
	}

	int auth_result = http_check_auth(client);
	if (auth_result != 0) {
		LOG_WRN("Authentication failed for configuration reset endpoint");
		http_send_auth_required_response(response_ctx);
		return 0;
	}

	char response_buffer[96];
	int rc = openjbod_settings_reset_all();
	if (rc != 0) {
		LOG_ERR("Configuration reset failed: %d", rc);
		response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
		snprintf(response_buffer, sizeof(response_buffer),
			"{\"status\":\"reset_config\",\"result\":\"error\",\"code\":%d}", rc);
		response_ctx->body = response_buffer;
		response_ctx->body_len = strlen(response_buffer);
		response_ctx->final_chunk = true;
		return 0;
	}

	response_ctx->status = HTTP_200_OK;
	strcpy(response_buffer, "{\"status\":\"reset_config\",\"result\":\"accepted\"}");
	response_ctx->body = response_buffer;
	response_ctx->body_len = strlen(response_buffer);
	response_ctx->final_chunk = true;

	LOG_INF("Configuration reset requested via API; reboot scheduled");
	schedule_reboot();
	return 0;
}

static void respond_error(struct http_response_ctx *response_ctx, enum http_status code,
			  const char *msg)
{
	static char buf[128];

	snprintf(buf, sizeof(buf), "{\"status\":\"error\",\"message\":\"%s\"}", msg);
	response_ctx->status = code;
	response_ctx->body = buf;
	response_ctx->body_len = strlen(buf);
	response_ctx->final_chunk = true;
}

/* GET /api/settings/backup -> the raw settings file as a download. */
static int backup_handler(struct http_client_ctx *client, enum http_data_status status,
			  const struct http_request_ctx *request_ctx,
			  struct http_response_ctx *response_ctx, void *user_data)
{
	static uint8_t backup_buf[BACKUP_BUF_SIZE];
	static const struct http_header backup_headers[] = {
		{"Content-Type", "application/octet-stream"},
		{"Content-Disposition", "attachment; filename=\"settings.dat\""},
	};

	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status != HTTP_SERVER_DATA_FINAL) {
		return 0;
	}

	if (http_check_auth(client) != 0) {
		http_send_auth_required_response(response_ctx);
		return 0;
	}

	struct fs_dirent ent;
	int rc = fs_stat(SETTINGS_PATH, &ent);

	if (rc != 0) {
		LOG_ERR("backup: fs_stat(%s) failed: %d", SETTINGS_PATH, rc);
		respond_error(response_ctx, HTTP_500_INTERNAL_SERVER_ERROR, "no settings file");
		return 0;
	}
	if (ent.size > sizeof(backup_buf)) {
		LOG_ERR("backup: settings file %zu B exceeds buffer", (size_t)ent.size);
		respond_error(response_ctx, HTTP_500_INTERNAL_SERVER_ERROR, "settings file too large");
		return 0;
	}

	struct fs_file_t file;

	fs_file_t_init(&file);
	rc = fs_open(&file, SETTINGS_PATH, FS_O_READ);
	if (rc != 0) {
		respond_error(response_ctx, HTTP_500_INTERNAL_SERVER_ERROR, "open failed");
		return 0;
	}

	ssize_t n = fs_read(&file, backup_buf, sizeof(backup_buf));

	fs_close(&file);
	if (n < 0) {
		respond_error(response_ctx, HTTP_500_INTERNAL_SERVER_ERROR, "read failed");
		return 0;
	}

	response_ctx->status = HTTP_200_OK;
	response_ctx->headers = backup_headers;
	response_ctx->header_count = ARRAY_SIZE(backup_headers);
	response_ctx->body = backup_buf;
	response_ctx->body_len = (size_t)n;
	response_ctx->final_chunk = true;
	return 0;
}

/* POST /api/settings/restore -> overwrite the settings file with the upload, reboot.
 * The body is streamed to a temp file, then renamed over the live file on success.
 */
static int restore_handler(struct http_client_ctx *client, enum http_data_status status,
			   const struct http_request_ctx *request_ctx,
			   struct http_response_ctx *response_ctx, void *user_data)
{
	static struct fs_file_t file;
	static bool file_open;
	static struct http_client_ctx *owner;
	static size_t total;
	static bool failed;

	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_DATA_ABORTED) {
		if (file_open) {
			fs_close(&file);
			fs_unlink(RESTORE_TMP_PATH);
		}
		file_open = false;
		owner = NULL;
		total = 0;
		failed = false;
		return 0;
	}

	if (owner == NULL) {
		/* Start of a new upload: authenticate before touching the filesystem. */
		if (http_check_auth(client) != 0) {
			http_send_auth_required_response(response_ctx);
			return 0;
		}

		fs_file_t_init(&file);
		int rc = fs_open(&file, RESTORE_TMP_PATH,
				 FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);

		if (rc != 0) {
			LOG_ERR("restore: open temp failed: %d", rc);
			respond_error(response_ctx, HTTP_500_INTERNAL_SERVER_ERROR, "open failed");
			return 0;
		}
		file_open = true;
		owner = client;
		total = 0;
		failed = false;
	} else if (owner != client) {
		respond_error(response_ctx, HTTP_409_CONFLICT, "another restore is in progress");
		return 0;
	}

	if (!failed && request_ctx->data_len > 0) {
		if (total + request_ctx->data_len > RESTORE_MAX_SIZE) {
			failed = true;
		} else if (fs_write(&file, request_ctx->data, request_ctx->data_len) < 0) {
			failed = true;
		} else {
			total += request_ctx->data_len;
		}
	}

	if (status != HTTP_SERVER_DATA_FINAL) {
		return 0;
	}

	fs_close(&file);
	file_open = false;
	owner = NULL;

	if (failed || total == 0) {
		bool empty = (total == 0);

		fs_unlink(RESTORE_TMP_PATH);
		total = 0;
		failed = false;
		respond_error(response_ctx, HTTP_400_BAD_REQUEST,
			      empty ? "empty upload" : "upload too large or write failed");
		return 0;
	}

	/* Swap the temp file in for the live settings file, then reboot to reload it. */
	fs_unlink(SETTINGS_PATH);
	int rc = fs_rename(RESTORE_TMP_PATH, SETTINGS_PATH);

	if (rc != 0) {
		LOG_ERR("restore: rename failed: %d", rc);
		fs_unlink(RESTORE_TMP_PATH);
		respond_error(response_ctx, HTTP_500_INTERNAL_SERVER_ERROR, "could not apply restore");
		return 0;
	}

	static char ok_buf[96];

	snprintf(ok_buf, sizeof(ok_buf),
		 "{\"status\":\"restored\",\"bytes\":%zu,\"message\":\"rebooting to apply\"}", total);
	response_ctx->status = HTTP_200_OK;
	response_ctx->body = ok_buf;
	response_ctx->body_len = strlen(ok_buf);
	response_ctx->final_chunk = true;

	LOG_INF("Settings restored (%zu bytes); rebooting to apply", total);
	total = 0;
	schedule_reboot();
	return 0;
}

struct http_resource_detail_dynamic reset_device_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
		.content_type = "application/json",
	},
	.cb = reset_device_handler,
	.user_data = NULL,
};

struct http_resource_detail_dynamic reset_config_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
		.content_type = "application/json",
	},
	.cb = reset_config_handler,
	.user_data = NULL,
};

struct http_resource_detail_dynamic settings_backup_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
		.content_type = "application/octet-stream",
	},
	.cb = backup_handler,
	.user_data = NULL,
};

struct http_resource_detail_dynamic settings_restore_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
		.content_type = "application/json",
	},
	.cb = restore_handler,
	.user_data = NULL,
};
