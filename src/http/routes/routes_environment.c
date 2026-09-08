#include "http/routes/routes_environment.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/logging/log.h>

#include <errno.h>

#include "emc2301.h"
#include "fan_calibrate.h"
#include "fan_control.h"
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

	static char response_buffer[768];
	struct emc2301_data fan_data;
	struct emc2301_config cfg;

	if (status == HTTP_SERVER_DATA_FINAL) {
		int ret = emc2301_get_status(&fan_data);
		if (ret == 0) {
			const struct environment_settings *env = &openjbod_settings_get()->environment;

			emc2301_get_config(&cfg);
			int written = snprintf(response_buffer, sizeof(response_buffer),
				 "{"
				 "\"status\":\"fan_reading\","
				 "\"pwm\":{"
				 "\"duty\":%u,"
				 "\"percent\":%u,"
				 "\"base_hz\":%u,"
				 "\"divide\":%u,"
				 "\"effective_hz\":%u"
				 "},"
				 "\"fan\":{"
				 "\"rpm\":%u,"
				 "\"tach_count\":%u,"
				 "\"tach_valid\":%s,"
				 "\"fault\":%s,"
				 "\"stall\":%s,"
				 "\"spin_fail\":%s,"
				 "\"drive_fail\":%s,"
				 "\"watchdog\":%s,"
				 "\"status_reg\":%u"
				 "},"
				 "\"tach\":{"
				 "\"pulses_per_rev\":%u,"
				 "\"edges\":%u,"
				 "\"edges_auto\":%s,"
				 "\"range\":%u,"
				 "\"min_rpm\":%u"
				 "},"
				 "\"control\":{"
				 "\"mode\":\"%s\","
				 "\"target_percent\":%u,"
				 "\"min_drive_percent\":%u"
				 "},"
				 "\"initialized\":%s"
				 "}",
				 fan_data.pwm_duty,
				 emc2301_duty_to_percent(fan_data.pwm_duty),
				 cfg.pwm_base_hz,
				 cfg.pwm_divide,
				 emc2301_effective_pwm_hz(),
				 fan_data.fan_rpm,
				 fan_data.tach_count,
				 fan_data.tach_valid ? "true" : "false",
				 fan_data.fan_fault ? "true" : "false",
				 fan_data.stall ? "true" : "false",
				 fan_data.spin_fail ? "true" : "false",
				 fan_data.drive_fail ? "true" : "false",
				 fan_data.watchdog ? "true" : "false",
				 fan_data.status_reg,
				 cfg.tach_pulses_per_rev,
				 emc2301_config_edges(&cfg),
				 cfg.tach_edges ? "false" : "true",
				 cfg.tach_range_mult,
				 emc2301_config_min_rpm(&cfg),
				 fan_control_mode(),
				 fan_control_current_target(),
				 env->fan_min_drive_percent,
				 fan_data.initialized ? "true" : "false");
			if (written < 0 || written >= (int)sizeof(response_buffer)) {
				LOG_ERR("Fan JSON truncated (%d/%zu bytes)", written, sizeof(response_buffer));
				snprintf(response_buffer, sizeof(response_buffer),
					 "{\"status\":\"fan_error\",\"error\":\"response too large\"}");
				response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
			}
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

	if (request_ctx->data_len + cursor >= sizeof(post_payload_buf)) {
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
			long v = strtol(duty_param + 5, NULL, 10);

			if (v < 0) {
				v = 0;
			} else if (v > 255) {
				v = 255;
			}
			duty_value = (uint8_t)v;
		} else if (percent_param) {
			long v = strtol(percent_param + 8, NULL, 10);

			if (v < 0) {
				v = 0;
			} else if (v > 100) {
				v = 100;
			}
			duty_value = emc2301_percent_to_duty((uint8_t)v);
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
				 "},"
				 "\"note\":\"automatic control overrides this within one update interval unless use_external_fan_control is set\""
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

/* Render the calibration state and last result as JSON. */
static int format_cal_status(char *buf, size_t len)
{
	struct fan_cal_status st;
	int n;

	fan_calibrate_get_status(&st);
	n = snprintf(buf, len,
		     "{"
		     "\"state\":\"%s\","
		     "\"phase\":\"%s\","
		     "\"progress_percent\":%u,"
		     "\"elapsed_ms\":%u,"
		     "\"abort_reason\":%s%s%s,"
		     "\"result\":{"
		     "\"tach_present\":%s,"
		     "\"monotonic\":%s,"
		     "\"atx_power_on\":%s,"
		     "\"budget_cutoff\":%s,"
		     "\"used_retry\":%s,"
		     "\"min_spin_percent\":%u,"
		     "\"max_rpm\":%u,"
		     "\"rpm_at\":[",
		     fan_cal_state_str(st.state), fan_cal_phase_str(st.phase),
		     st.progress_percent, st.elapsed_ms,
		     st.abort_reason ? "\"" : "", st.abort_reason ? st.abort_reason : "null",
		     st.abort_reason ? "\"" : "",
		     st.result.tach_present ? "true" : "false",
		     st.result.monotonic ? "true" : "false",
		     st.result.atx_power_on ? "true" : "false",
		     st.result.budget_cutoff ? "true" : "false",
		     st.result.used_retry ? "true" : "false",
		     st.result.min_spin_percent, st.result.max_rpm);
	if (n < 0 || n >= (int)len) {
		return -ENOMEM;
	}
	for (int i = 0; i < FAN_CAL_SWEEP_POINTS; i++) {
		int m = snprintf(buf + n, len - n, "%s[%u,%u]", i ? "," : "",
				 st.result.sweep_percent[i], st.result.sweep_rpm[i]);
		if (m < 0 || m >= (int)(len - n)) {
			return -ENOMEM;
		}
		n += m;
	}
	int m = snprintf(buf + n, len - n,
			 "],"
			 "\"chosen_pwm_base_hz\":%u,"
			 "\"chosen_edges\":%u,"
			 "\"chosen_range\":%u,"
			 "\"assumed_ppr\":%u,"
			 "\"suggested_ppr\":%u,"
			 "\"confidence\":%u,"
			 "\"note\":\"%s\""
			 "}}",
			 st.result.chosen_pwm_base_hz, st.result.chosen_edges, st.result.chosen_range,
			 st.result.assumed_ppr, st.result.suggested_ppr, st.result.confidence,
			 st.result.note);
	if (m < 0 || m >= (int)(len - n)) {
		return -ENOMEM;
	}
	return 0;
}

static int fan_calibrate_handler(struct http_client_ctx *client, enum http_data_status status,
				 const struct http_request_ctx *request_ctx,
				 struct http_response_ctx *response_ctx,
				 void *user_data)
{
	ARG_UNUSED(user_data);

	static char response_buffer[768];
	static char post_payload_buf[64];
	static size_t cursor;

	if (status == HTTP_SERVER_DATA_ABORTED) {
		cursor = 0;
		return 0;
	}

	if (client->method == HTTP_POST) {
		if (request_ctx->data_len + cursor >= sizeof(post_payload_buf)) {
			cursor = 0;
			return -ENOMEM;
		}
		memcpy(post_payload_buf + cursor, request_ctx->data, request_ctx->data_len);
		cursor += request_ctx->data_len;
	}

	if (status != HTTP_SERVER_DATA_FINAL) {
		return 0;
	}

	if (http_check_auth(client) != 0) {
		LOG_WRN("Authentication failed for fan_calibrate endpoint");
		http_send_auth_required_response(response_ctx);
		cursor = 0;
		return 0;
	}

	if (client->method == HTTP_GET) {
		if (format_cal_status(response_buffer, sizeof(response_buffer)) != 0) {
			snprintf(response_buffer, sizeof(response_buffer),
				 "{\"status\":\"fan_error\",\"error\":\"response too large\"}");
			response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
		}
		goto send_response;
	}

	post_payload_buf[cursor] = '\0';
	cursor = 0;

	if (strstr(post_payload_buf, "action=abort")) {
		int rc = fan_calibrate_abort();

		if (rc == 0) {
			snprintf(response_buffer, sizeof(response_buffer),
				 "{\"status\":\"calibration_abort_requested\"}");
		} else {
			snprintf(response_buffer, sizeof(response_buffer),
				 "{\"status\":\"calibration_not_running\"}");
			response_ctx->status = HTTP_409_CONFLICT;
		}
	} else if (strstr(post_payload_buf, "action=reset")) {
		int rc = openjbod_settings_reset_fan_hw();

		if (rc == 0) {
			rc = fan_control_apply_hw_config();
			if (rc == -ENODEV) {
				rc = 0;  /* controller absent: settings still reset */
			}
		}
		if (rc == 0) {
			snprintf(response_buffer, sizeof(response_buffer),
				 "{\"status\":\"fan_hw_reset\"}");
		} else {
			snprintf(response_buffer, sizeof(response_buffer),
				 "{\"status\":\"fan_error\",\"error\":\"reset failed (%d)\"}", rc);
			response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
		}
	} else {  /* action=start (default) */
		int rc = fan_calibrate_request();

		switch (rc) {
		case 0:
			snprintf(response_buffer, sizeof(response_buffer),
				 "{\"status\":\"calibration_started\"}");
			response_ctx->status = HTTP_202_ACCEPTED;
			break;
		case -EBUSY:
			snprintf(response_buffer, sizeof(response_buffer),
				 "{\"status\":\"calibration_running\"}");
			response_ctx->status = HTTP_409_CONFLICT;
			break;
		case -ENODEV:
			snprintf(response_buffer, sizeof(response_buffer),
				 "{\"status\":\"fan_controller_not_initialized\"}");
			response_ctx->status = HTTP_503_SERVICE_UNAVAILABLE;
			break;
		case -EHOSTDOWN:
			snprintf(response_buffer, sizeof(response_buffer),
				 "{\"status\":\"temperature_too_high\"}");
			response_ctx->status = HTTP_409_CONFLICT;
			break;
		default:
			snprintf(response_buffer, sizeof(response_buffer),
				 "{\"status\":\"fan_error\",\"error\":\"request failed (%d)\"}", rc);
			response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
			break;
		}
	}

send_response:
	response_ctx->body = response_buffer;
	response_ctx->body_len = strlen(response_buffer);
	response_ctx->final_chunk = true;
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

struct http_resource_detail_dynamic fan_calibrate_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET) | BIT(HTTP_POST),
		.content_type = "application/json",
	},
	.cb = fan_calibrate_handler,
	.user_data = NULL,
};
