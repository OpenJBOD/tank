#include "http/routes/routes_users.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/data/json.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/sys/util.h>

#include "http/auth.h"
#include "settings.h"

LOG_MODULE_REGISTER(tank_http_users, LOG_LEVEL_INF);

/* User-management request body: {"action":..,"username":..,"password":..}.
 * Parsed with the JSON library instead of substring scanning. */
struct user_req {
	const char *action;
	const char *username;
	const char *password;
};

static const struct json_obj_descr user_req_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct user_req, action, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct user_req, username, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct user_req, password, JSON_TOK_STRING),
};

#define USER_REQ_ACTION   BIT(0)
#define USER_REQ_USERNAME BIT(1)

/* Returns NULL if the lengths are acceptable, else a user-facing error message.
 * Pass username=NULL to skip the username check (e.g. password-only update). Keep
 * the numbers in sync with USERNAME_MAX_CHARS / PASSWORD_MIN_LEN / PASSWORD_MAX_LEN. */
static const char *creds_length_error(const char *username, const char *password)
{
	if (username) {
		size_t ulen = strlen(username);

		if (ulen == 0 || ulen > USERNAME_MAX_CHARS) {
			return "Username must be 1-31 characters";
		}
	}
	if (password) {
		size_t plen = strlen(password);

		if (plen < PASSWORD_MIN_LEN || plen > PASSWORD_MAX_LEN) {
			return "Password must be 8-64 characters";
		}
	}
	return NULL;
}

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
		int auth_result = http_check_auth(client);
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

			struct user_req req = {0};
			int parsed = json_obj_parse(post_payload_buf, strlen(post_payload_buf),
						    user_req_descr, ARRAY_SIZE(user_req_descr), &req);

			if (parsed < 0 ||
			    (parsed & (USER_REQ_ACTION | USER_REQ_USERNAME)) !=
				    (USER_REQ_ACTION | USER_REQ_USERNAME)) {
				LOG_WRN("Missing/invalid required fields in user management request");
				snprintf(response_buffer, sizeof(response_buffer),
					 "{\"status\":\"error\",\"message\":\"Missing required fields\"}");
				response_ctx->body = response_buffer;
				response_ctx->body_len = strlen(response_buffer);
				response_ctx->final_chunk = true;
				response_ctx->status = HTTP_400_BAD_REQUEST;
				return 0;
			}

			const char *action_start = req.action;
			const char *username_start = req.username;
			const char *password_start = req.password;  /* NULL if absent */

			int result = 0;

			if (strcmp(action_start, "create") == 0) {
				const char *len_err = creds_length_error(username_start, password_start);

				if (!password_start) {
					LOG_WRN("Password required for create action");
					snprintf(response_buffer, sizeof(response_buffer),
						 "{\"status\":\"error\",\"message\":\"Password required\"}");
					response_ctx->status = HTTP_400_BAD_REQUEST;
				} else if (len_err) {
					snprintf(response_buffer, sizeof(response_buffer),
						 "{\"status\":\"error\",\"message\":\"%s\"}", len_err);
					response_ctx->status = HTTP_400_BAD_REQUEST;
				} else {
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
				const char *len_err = creds_length_error(NULL, password_start);

				if (!password_start) {
					LOG_WRN("Password required for update action");
					snprintf(response_buffer, sizeof(response_buffer),
						 "{\"status\":\"error\",\"message\":\"Password required\"}");
					response_ctx->status = HTTP_400_BAD_REQUEST;
				} else if (len_err) {
					snprintf(response_buffer, sizeof(response_buffer),
						 "{\"status\":\"error\",\"message\":\"%s\"}", len_err);
					response_ctx->status = HTTP_400_BAD_REQUEST;
				} else {
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
