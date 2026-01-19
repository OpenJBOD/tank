#include "http/routes/routes_users.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/sys/util.h>

#include "http/auth.h"
#include "settings.h"

LOG_MODULE_REGISTER(tank_http_users, LOG_LEVEL_INF);

static int users_handler(struct http_client_ctx *client, enum http_data_status status,
			 const struct http_request_ctx *request_ctx,
			 struct http_response_ctx *response_ctx, void *user_data)
{
	static char response_buffer[2048];
	static char post_payload_buf[512];
	static size_t cursor;
	enum http_method method = client->method;

	ARG_UNUSED(user_data);

	LOG_DBG("Users handler status %d, method %d", status, method);

	if (status == HTTP_SERVER_DATA_FINAL) {
		int auth_result = http_basic_auth_check(client);
		if (auth_result != 0) {
			LOG_WRN("Authentication failed for users endpoint");
			http_send_auth_required_response(response_ctx);
			return 0;
		}
	}

	if (status == HTTP_SERVER_DATA_ABORTED) {
		cursor = 0;
		return 0;
	}

	if (method == HTTP_GET) {
		if (status == HTTP_SERVER_DATA_FINAL) {
			struct openjbod_settings *settings = openjbod_settings_get();
			if (!settings) {
				LOG_ERR("Failed to get settings for users list");
				response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
				response_ctx->final_chunk = true;
				return 0;
			}

			int len = snprintf(response_buffer, sizeof(response_buffer), "{");

			for (int i = 0; i < MAX_USERS && len < (int)sizeof(response_buffer) - 50; i++) {
				if (settings->auth.users[i].username[0] != '\0') {
					len += snprintf(response_buffer + len, sizeof(response_buffer) - len,
						       "%s\"%d\":{\"username\":\"%s\"}",
						       (len > 1) ? "," : "",
						       i + 1,
						       settings->auth.users[i].username);
				}
			}

			len += snprintf(response_buffer + len, sizeof(response_buffer) - len, "}");

			if (len >= (int)sizeof(response_buffer)) {
				LOG_ERR("Response buffer overflow in users list");
				response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
				response_ctx->final_chunk = true;
				return 0;
			}

			response_ctx->body = response_buffer;
			response_ctx->body_len = strlen(response_buffer);
			response_ctx->final_chunk = true;
		}
	} else if (method == HTTP_POST) {
		if (status == HTTP_SERVER_DATA_MORE) {
			if (cursor + request_ctx->data_len >= sizeof(post_payload_buf)) {
				LOG_WRN("POST data too large");
				response_ctx->status = HTTP_413_PAYLOAD_TOO_LARGE;
				response_ctx->final_chunk = true;
				cursor = 0;
				return 0;
			}
			memcpy(post_payload_buf + cursor, request_ctx->data, request_ctx->data_len);
			cursor += request_ctx->data_len;
		}

		if (status == HTTP_SERVER_DATA_FINAL) {
			if (cursor == 0 && request_ctx->data_len > 0) {
				if (request_ctx->data_len >= sizeof(post_payload_buf)) {
					LOG_WRN("POST data too large in final chunk");
					response_ctx->status = HTTP_413_PAYLOAD_TOO_LARGE;
					response_ctx->final_chunk = true;
					return 0;
				}
				memcpy(post_payload_buf, request_ctx->data, request_ctx->data_len);
				cursor = request_ctx->data_len;
			}

			post_payload_buf[cursor] = '\0';
			cursor = 0;

			char *action_start = strstr(post_payload_buf, "\"action\":");
			char *username_start = strstr(post_payload_buf, "\"username\":");
			char *password_start = strstr(post_payload_buf, "\"password\":");

			if (!action_start || !username_start) {
				LOG_WRN("Missing required fields in user management request");
				LOG_WRN("POST payload was: %s", post_payload_buf);
				snprintf(response_buffer, sizeof(response_buffer),
					 "{\"status\":\"error\",\"message\":\"Missing required fields\"}");
				response_ctx->body = response_buffer;
				response_ctx->body_len = strlen(response_buffer);
				response_ctx->final_chunk = true;
				response_ctx->status = HTTP_400_BAD_REQUEST;
				return 0;
			}

			action_start += 9;
			while (*action_start == ' ' || *action_start == '\t' || *action_start == '"') {
				action_start++;
			}
			char *action_end = strchr(action_start, '"');
			if (!action_end) {
				LOG_WRN("Invalid action format");
				response_ctx->status = HTTP_400_BAD_REQUEST;
				return 0;
			}
			*action_end = '\0';

			username_start += 11;
			while (*username_start == ' ' || *username_start == '\t' || *username_start == '"') {
				username_start++;
			}
			char *username_end = strchr(username_start, '"');
			if (!username_end) {
				LOG_WRN("Invalid username format");
				response_ctx->status = HTTP_400_BAD_REQUEST;
				return 0;
			}
			*username_end = '\0';

			int result = 0;

			if (strcmp(action_start, "create") == 0) {
				if (!password_start) {
					LOG_WRN("Password required for create action");
					snprintf(response_buffer, sizeof(response_buffer),
						 "{\"status\":\"error\",\"message\":\"Password required\"}");
					response_ctx->status = HTTP_400_BAD_REQUEST;
				} else {
					password_start += 11;
					while (*password_start == ' ' || *password_start == '\t' ||
					       *password_start == '"') {
						password_start++;
					}
					char *password_end = strchr(password_start, '"');
					if (password_end) {
						*password_end = '\0';
					}

					result = openjbod_settings_create_user(username_start, password_start);
					if (result >= 0) {
						snprintf(response_buffer, sizeof(response_buffer),
							 "{\"status\":\"success\",\"message\":\"User created successfully\"}");
						response_ctx->status = HTTP_200_OK;
					} else {
						snprintf(response_buffer, sizeof(response_buffer),
							 "{\"status\":\"error\",\"message\":\"Failed to create user\"}");
						response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
					}
				}
			} else if (strcmp(action_start, "update") == 0) {
				if (!password_start) {
					LOG_WRN("Password required for update action");
					snprintf(response_buffer, sizeof(response_buffer),
						 "{\"status\":\"error\",\"message\":\"Password required\"}");
					response_ctx->status = HTTP_400_BAD_REQUEST;
				} else {
					password_start += 11;
					while (*password_start == ' ' || *password_start == '\t' ||
					       *password_start == '"') {
						password_start++;
					}
					char *password_end = strchr(password_start, '"');
					if (password_end) {
						*password_end = '\0';
					}

					result = openjbod_settings_update_user_password(username_start, password_start);
					if (result == 0) {
						snprintf(response_buffer, sizeof(response_buffer),
							 "{\"status\":\"success\",\"message\":\"Password updated successfully\"}");
						response_ctx->status = HTTP_200_OK;
					} else {
						snprintf(response_buffer, sizeof(response_buffer),
							 "{\"status\":\"error\",\"message\":\"Failed to update password\"}");
						response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
					}
				}
			} else if (strcmp(action_start, "delete") == 0) {
				const struct openjbod_settings *settings = openjbod_settings_get();
				int user_idx = -1;

				for (int i = 0; i < MAX_USERS; i++) {
					if (strcmp(settings->auth.users[i].username, username_start) == 0) {
						user_idx = i;
						break;
					}
				}

				if (user_idx >= 0) {
					result = openjbod_settings_delete_user(user_idx);
					if (result == 0) {
						snprintf(response_buffer, sizeof(response_buffer),
							 "{\"status\":\"success\",\"message\":\"User deleted successfully\"}");
						response_ctx->status = HTTP_200_OK;
					} else {
						snprintf(response_buffer, sizeof(response_buffer),
							 "{\"status\":\"error\",\"message\":\"Failed to delete user\"}");
						response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
					}
				} else {
					snprintf(response_buffer, sizeof(response_buffer),
						 "{\"status\":\"error\",\"message\":\"User not found\"}");
					response_ctx->status = HTTP_404_NOT_FOUND;
				}
			} else {
				LOG_WRN("Unknown action: %s", action_start);
				snprintf(response_buffer, sizeof(response_buffer),
					 "{\"status\":\"error\",\"message\":\"Unknown action\"}");
				response_ctx->status = HTTP_400_BAD_REQUEST;
			}

			response_ctx->body = response_buffer;
			response_ctx->body_len = strlen(response_buffer);
			response_ctx->final_chunk = true;
		}
	}

	return 0;
}

struct http_resource_detail_dynamic users_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET) | BIT(HTTP_POST),
		.content_type = "application/json",
	},
	.cb = users_handler,
	.user_data = NULL,
};
