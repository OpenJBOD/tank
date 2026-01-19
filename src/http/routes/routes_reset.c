#include "http/routes/routes_reset.h"

#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

#include "http/auth.h"
#include "settings.h"

LOG_MODULE_REGISTER(tank_http_reset, LOG_LEVEL_INF);

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

	int auth_result = http_basic_auth_check(client);
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

	int auth_result = http_basic_auth_check(client);
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
