#include "http/routes/routes_power.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/logging/log.h>

#include "http/auth.h"
#include "sr_latch.h"

LOG_MODULE_REGISTER(tank_http_power, LOG_LEVEL_INF);

static int power_on_handler(struct http_client_ctx *client, enum http_data_status status,
			  const struct http_request_ctx *request_ctx,
			  struct http_response_ctx *response_ctx,
			  void *user_data)
{
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_DATA_FINAL) {
		int auth_result = http_basic_auth_check(client);
		if (auth_result != 0) {
			LOG_WRN("Authentication failed for power_on endpoint");
			http_send_auth_required_response(response_ctx);
			return 0;
		}
	}

	static char response_buffer[] = "{\"status\":\"power_on\",\"result\":\"success\"}";

	if (status == HTTP_SERVER_DATA_FINAL) {
		LOG_INF("ATX Power ON");
		sr_latch_set_on();
		response_ctx->body = response_buffer;
		response_ctx->body_len = strlen(response_buffer);
		response_ctx->final_chunk = true;
	}

	return 0;
}

static int power_off_handler(struct http_client_ctx *client, enum http_data_status status,
			   const struct http_request_ctx *request_ctx,
			   struct http_response_ctx *response_ctx,
			   void *user_data)
{
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_DATA_FINAL) {
		int auth_result = http_basic_auth_check(client);
		if (auth_result != 0) {
			LOG_WRN("Authentication failed for power_off endpoint");
			http_send_auth_required_response(response_ctx);
			return 0;
		}
	}

	static char response_buffer[] = "{\"status\":\"power_off\",\"result\":\"success\"}";

	if (status == HTTP_SERVER_DATA_FINAL) {
		LOG_INF("ATX Power OFF");
		sr_latch_set_off();
		response_ctx->body = response_buffer;
		response_ctx->body_len = strlen(response_buffer);
		response_ctx->final_chunk = true;
	}

	return 0;
}

static int power_status_handler(struct http_client_ctx *client, enum http_data_status status,
			      const struct http_request_ctx *request_ctx,
			      struct http_response_ctx *response_ctx,
			      void *user_data)
{
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_DATA_FINAL) {
		int auth_result = http_basic_auth_check(client);
		if (auth_result != 0) {
			LOG_WRN("Authentication failed for power_status endpoint");
			http_send_auth_required_response(response_ctx);
			return 0;
		}
	}

	static char response_buffer[64];

	if (status == HTTP_SERVER_DATA_FINAL) {
		bool power_state = sr_latch_get_state();
		snprintf(response_buffer, sizeof(response_buffer),
			 "{\"status\":\"power_status\",\"state\":\"%s\"}",
			 power_state ? "on" : "off");
		response_ctx->body = response_buffer;
		response_ctx->body_len = strlen(response_buffer);
		response_ctx->final_chunk = true;
	}

	return 0;
}

struct http_resource_detail_dynamic power_on_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET) | BIT(HTTP_POST),
		.content_type = "application/json",
	},
	.cb = power_on_handler,
	.user_data = NULL,
};

struct http_resource_detail_dynamic power_off_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET) | BIT(HTTP_POST),
		.content_type = "application/json",
	},
	.cb = power_off_handler,
	.user_data = NULL,
};

struct http_resource_detail_dynamic power_status_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
		.content_type = "application/json",
	},
	.cb = power_status_handler,
	.user_data = NULL,
};
