#include "http/routes/routes_environment.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/logging/log.h>

#include "emc2301.h"
#include "http/auth.h"
#include "settings.h"
#include "temperature.h"

LOG_MODULE_REGISTER(tank_http_env, LOG_LEVEL_INF);

static int temp_handler(struct http_client_ctx *client, enum http_data_status status,
			const struct http_request_ctx *request_ctx,
			struct http_response_ctx *response_ctx,
			void *user_data)
{
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_DATA_FINAL) {
		int auth_result = http_check_auth(client);
		if (auth_result != 0) {
			LOG_WRN("Authentication failed for temp endpoint");
			http_send_auth_required_response(response_ctx);
			return 0;
		}
	}

	static char response_buffer[512];
	struct temperature_data temp_data;

	if (status == HTTP_SERVER_DATA_FINAL) {
		/* Cached read: avoid blocking the HTTP thread on a DS18B20 conversion. */
		int ret = temperature_read_cached(&temp_data);
		int written = 0;
		if (ret == 0) {
			const struct openjbod_settings *st = openjbod_settings_get();
			float active_temp = 0.0f;
			const char *active_src = "none";

			(void)temperature_get_active(&temp_data,
						     st->environment.primary_temp_source,
						     &active_temp, &active_src);

			written = snprintf(response_buffer, sizeof(response_buffer),
				 "{"
				 "\"status\":\"temp_reading\","
				 "\"primary_source\":%u,"
				 "\"active_source\":\"%s\","
				 "\"active_temperature\":%.3f,"
				 "\"ds18b20\":{"
				 "\"temperature\":%.3f,"
				 "\"valid\":%s,"
				 "\"unit\":\"celsius\""
				 "},"
				 "\"ds18b20_ext\":{"
				 "\"temperature\":%.3f,"
				 "\"valid\":%s,"
				 "\"present\":%s,"
				 "\"unit\":\"celsius\""
				 "},"
				 "\"rp2040\":{"
				 "\"temperature\":%.3f,"
				 "\"valid\":%s,"
				 "\"unit\":\"celsius\""
				 "}"
				 "}",
				 st->environment.primary_temp_source,
				 active_src,
				 (double)active_temp,
				 (double)temp_data.ds18b20_temp,
				 temp_data.ds18b20_valid ? "true" : "false",
				 (double)temp_data.ds18b20_ext_temp,
				 temp_data.ds18b20_ext_valid ? "true" : "false",
				 temperature_ext_present() ? "true" : "false",
				 (double)temp_data.rp2040_temp,
				 temp_data.rp2040_valid ? "true" : "false");
		} else {
			snprintf(response_buffer, sizeof(response_buffer),
				 "{\"status\":\"temp_error\",\"error\":\"Failed to read temperature sensors\"}");
			response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
		}

		if (written < 0 || written >= (int)sizeof(response_buffer)) {
			LOG_ERR("Temperature JSON truncated (%d/%zu bytes)", written, sizeof(response_buffer));
			snprintf(response_buffer, sizeof(response_buffer),
				 "{\"status\":\"temp_error\",\"error\":\"response too large\"}");
			response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
		}

		response_ctx->body = response_buffer;
		response_ctx->body_len = strlen(response_buffer);
		response_ctx->final_chunk = true;
	}

	return 0;
}

static int fan_handler(struct http_client_ctx *client, enum http_data_status status,
		     const struct http_request_ctx *request_ctx,
		     struct http_response_ctx *response_ctx,
		     void *user_data)
{
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_DATA_FINAL) {
		int auth_result = http_check_auth(client);
		if (auth_result != 0) {
			LOG_WRN("Authentication failed for fan endpoint");
			http_send_auth_required_response(response_ctx);
			return 0;
		}
	}

	static char response_buffer[512];
	struct emc2301_data fan_data;

	if (status == HTTP_SERVER_DATA_FINAL) {
		int ret = emc2301_get_status(&fan_data);
		if (ret == 0) {
			snprintf(response_buffer, sizeof(response_buffer),
				 "{"
				 "\"status\":\"fan_reading\","
				 "\"pwm\":{"
				 "\"duty\":%d,"
				 "\"percent\":%d"
				 "},"
				 "\"fan\":{"
				 "\"rpm\":%d,"
				 "\"fault\":%s"
				 "},"
				 "\"initialized\":%s"
				 "}",
				 fan_data.pwm_duty,
				 emc2301_duty_to_percent(fan_data.pwm_duty),
				 fan_data.fan_rpm,
				 fan_data.fan_fault ? "true" : "false",
				 fan_data.initialized ? "true" : "false");
		} else {
			snprintf(response_buffer, sizeof(response_buffer),
				 "{\"status\":\"fan_error\",\"error\":\"Failed to read fan controller\"}");
			response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
		}

		response_ctx->body = response_buffer;
		response_ctx->body_len = strlen(response_buffer);
		response_ctx->final_chunk = true;
	}

	return 0;
}

static int fan_set_handler(struct http_client_ctx *client, enum http_data_status status,
			const struct http_request_ctx *request_ctx,
			struct http_response_ctx *response_ctx,
			void *user_data)
{
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_DATA_FINAL) {
		int auth_result = http_check_auth(client);
		if (auth_result != 0) {
			LOG_WRN("Authentication failed for fan_set endpoint");
			http_send_auth_required_response(response_ctx);
			return 0;
		}
	}

	static char response_buffer[256];
	static char post_payload_buf[64];
	static size_t cursor;

	if (status == HTTP_SERVER_DATA_ABORTED) {
		cursor = 0;
		return 0;
	}

	if (request_ctx->data_len + cursor > sizeof(post_payload_buf)) {
		cursor = 0;
		return -ENOMEM;
	}

	memcpy(post_payload_buf + cursor, request_ctx->data, request_ctx->data_len);
	cursor += request_ctx->data_len;

	if (status == HTTP_SERVER_DATA_FINAL) {
		post_payload_buf[cursor] = '\0';

		char *duty_param = strstr(post_payload_buf, "duty=");
		char *percent_param = strstr(post_payload_buf, "percent=");
		uint8_t duty_value = 0;

		if (duty_param) {
			duty_value = (uint8_t)strtol(duty_param + 5, NULL, 10);
			if (duty_value > 255) {
				duty_value = 255;
			}
		} else if (percent_param) {
			uint8_t percent = (uint8_t)strtol(percent_param + 8, NULL, 10);
			if (percent > 100) {
				percent = 100;
			}
			duty_value = emc2301_percent_to_duty(percent);
		} else {
			snprintf(response_buffer, sizeof(response_buffer),
				 "{\"status\":\"fan_set_error\",\"error\":\"Missing duty or percent parameter\"}");
			response_ctx->status = HTTP_400_BAD_REQUEST;
			cursor = 0;
			goto send_response;
		}

		int ret = emc2301_set_pwm_duty(duty_value);
		if (ret == 0) {
			snprintf(response_buffer, sizeof(response_buffer),
				 "{"
				 "\"status\":\"fan_set_success\","
				 "\"pwm\":{"
				 "\"duty\":%d,"
				 "\"percent\":%d"
				 "}"
				 "}",
				 duty_value,
				 emc2301_duty_to_percent(duty_value));
			LOG_INF("Fan speed set to %d duty (%d%%)", duty_value,
				emc2301_duty_to_percent(duty_value));
		} else {
			snprintf(response_buffer, sizeof(response_buffer),
				 "{\"status\":\"fan_set_error\",\"error\":\"Failed to set fan speed\"}");
			response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
		}

		cursor = 0;
send_response:
		response_ctx->body = response_buffer;
		response_ctx->body_len = strlen(response_buffer);
		response_ctx->final_chunk = true;
	}

	return 0;
}

struct http_resource_detail_dynamic temp_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
		.content_type = "application/json",
	},
	.cb = temp_handler,
	.user_data = NULL,
};

struct http_resource_detail_dynamic fan_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
		.content_type = "application/json",
	},
	.cb = fan_handler,
	.user_data = NULL,
};

struct http_resource_detail_dynamic fan_set_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
		.content_type = "application/json",
	},
	.cb = fan_set_handler,
	.user_data = NULL,
};
