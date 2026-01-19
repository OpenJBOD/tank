#include "http/routes/routes_certificates.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <strings.h>

#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/sys/util.h>

#include "http/auth.h"
#include "settings.h"

LOG_MODULE_REGISTER(tank_http_certs, LOG_LEVEL_INF);

static int parse_multipart_data(const uint8_t *data, size_t data_len, const char *boundary,
			      uint8_t **cert_data, size_t *cert_len,
			      uint8_t **key_data, size_t *key_len)
{
	const uint8_t *pos = data;
	const uint8_t *end = data + data_len;
	char boundary_marker[128];
	size_t boundary_len;

	snprintf(boundary_marker, sizeof(boundary_marker), "--%s", boundary);
	boundary_len = strlen(boundary_marker);

	*cert_data = NULL;
	*cert_len = 0;
	*key_data = NULL;
	*key_len = 0;

	while (pos < end) {
		const uint8_t *boundary_pos = (const uint8_t *)strstr((const char *)pos, boundary_marker);
		if (!boundary_pos) {
			break;
		}

		pos = boundary_pos + boundary_len;
		while (pos < end && (*pos == '\r' || *pos == '\n')) {
			pos++;
		}

		if (pos >= end) {
			break;
		}

		const uint8_t *headers_end = (const uint8_t *)strstr((const char *)pos, "\r\n\r\n");
		if (!headers_end) {
			break;
		}

		const char *cert_name = "name=\"certificate\"";
		const char *key_name = "name=\"private_key\"";
		bool is_cert = (strstr((const char *)pos, cert_name) != NULL);
		bool is_key = (strstr((const char *)pos, key_name) != NULL);

		pos = headers_end + 4;

		const uint8_t *next_boundary = NULL;
		for (size_t i = 0; i <= (size_t)(end - pos) - boundary_len; i++) {
			if (memcmp(pos + i, boundary_marker, boundary_len) == 0) {
				next_boundary = pos + i;
				break;
			}
		}

		const uint8_t *part_end = next_boundary ? next_boundary - 2 : end;

		if (part_end <= pos) {
			continue;
		}

		size_t part_len = part_end - pos;

		if (is_cert && !*cert_data) {
			*cert_data = (uint8_t *)pos;
			*cert_len = part_len;
		} else if (is_key && !*key_data) {
			*key_data = (uint8_t *)pos;
			*key_len = part_len;
		}

		if (next_boundary) {
			pos = next_boundary;
		} else {
			pos = end;
		}
	}

	return (*cert_data && *key_data) ? 0 : -ENOENT;
}

static int convert_binary_to_hex(const uint8_t *binary_data, size_t binary_len,
				  char *hex_string, size_t hex_string_len)
{
	if (hex_string_len < (binary_len * 2 + 1)) {
		LOG_ERR("Hex string buffer too small: need %zu, have %zu",
			binary_len * 2 + 1, hex_string_len);
		return -ENOMEM;
	}

	for (size_t i = 0; i < binary_len; i++) {
		snprintf(&hex_string[i * 2], 3, "%02x", binary_data[i]);
	}
	hex_string[binary_len * 2] = '\0';

	return 0;
}

static int store_certificate_in_settings(const uint8_t *cert_data, size_t cert_len)
{
	struct openjbod_settings *settings = openjbod_settings_get();
	int ret = convert_binary_to_hex(cert_data, cert_len,
				      settings->http.custom_certificate,
				      sizeof(settings->http.custom_certificate));
	if (ret < 0) {
		LOG_ERR("Failed to convert certificate to hex: %d", ret);
		return ret;
	}

	ret = openjbod_settings_save_all();
	if (ret < 0) {
		LOG_ERR("Failed to save settings with certificate: %d", ret);
		return ret;
	}

	LOG_INF("Stored custom certificate in settings (%zu bytes)", cert_len);
	return 0;
}

static int store_private_key_in_settings(const uint8_t *key_data, size_t key_len)
{
	struct openjbod_settings *settings = openjbod_settings_get();
	int ret = convert_binary_to_hex(key_data, key_len,
				      settings->http.custom_private_key,
				      sizeof(settings->http.custom_private_key));
	if (ret < 0) {
		LOG_ERR("Failed to convert private key to hex: %d", ret);
		return ret;
	}

	ret = openjbod_settings_save_all();
	if (ret < 0) {
		LOG_ERR("Failed to save settings with private key: %d", ret);
		return ret;
	}

	LOG_INF("Stored custom private key in settings (%zu bytes)", key_len);
	return 0;
}

static int certificates_upload_handler(struct http_client_ctx *client, enum http_data_status status,
				      const struct http_request_ctx *request_ctx,
				      struct http_response_ctx *response_ctx, void *user_data)
{
	static char response_buffer[512];
	static uint8_t upload_buffer[8192];
	static size_t upload_cursor;

	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_DATA_FINAL) {
		int auth_result = http_basic_auth_check(client);
		if (auth_result != 0) {
			LOG_WRN("Authentication failed for certificate upload endpoint");
			http_send_auth_required_response(response_ctx);
			return 0;
		}
	}

	if (status == HTTP_SERVER_DATA_ABORTED) {
		upload_cursor = 0;
		return 0;
	}

	if (request_ctx->data_len + upload_cursor > sizeof(upload_buffer) - 1) {
		upload_cursor = 0;
		snprintf(response_buffer, sizeof(response_buffer),
			 "{\"success\":false,\"message\":\"Upload too large (max %zu bytes)\"}",
			 sizeof(upload_buffer) - 1);

		response_ctx->status = HTTP_413_PAYLOAD_TOO_LARGE;
		response_ctx->body = response_buffer;
		response_ctx->body_len = strlen(response_buffer);
		response_ctx->final_chunk = true;
		return 0;
	}

	memcpy(upload_buffer + upload_cursor, request_ctx->data, request_ctx->data_len);
	upload_cursor += request_ctx->data_len;

	if (status == HTTP_SERVER_DATA_FINAL) {
		const struct http_header *headers = client->header_capture_ctx.headers;
		size_t header_count = client->header_capture_ctx.count;
		const char *content_type = NULL;

		for (size_t i = 0; i < header_count; i++) {
			if (strcasecmp(headers[i].name, "Content-Type") == 0) {
				content_type = headers[i].value;
				break;
			}
		}

		ARG_UNUSED(content_type);

		char boundary[64] = {0};
		const char *data_str = (const char *)upload_buffer;

		if (data_str[0] == '-' && data_str[1] == '-') {
			const char *line_end = strstr(data_str, "\r\n");
			if (line_end) {
				const char *boundary_start = data_str;
				while (boundary_start < line_end && *boundary_start == '-') {
					boundary_start++;
				}

				size_t boundary_len = line_end - boundary_start;
				strncpy(boundary, boundary_start, MIN(boundary_len, sizeof(boundary) - 1));
				boundary[MIN(boundary_len, sizeof(boundary) - 1)] = '\0';
			} else {
				upload_cursor = 0;
				snprintf(response_buffer, sizeof(response_buffer),
					 "{\"success\":false,\"message\":\"Invalid multipart format - no boundary line ending found\"}");

				response_ctx->status = HTTP_400_BAD_REQUEST;
				response_ctx->body = response_buffer;
				response_ctx->body_len = strlen(response_buffer);
				response_ctx->final_chunk = true;
				return 0;
			}
		} else {
			upload_cursor = 0;
			snprintf(response_buffer, sizeof(response_buffer),
				 "{\"success\":false,\"message\":\"Invalid multipart format - boundary not found at start\"}");

			response_ctx->status = HTTP_400_BAD_REQUEST;
			response_ctx->body = response_buffer;
			response_ctx->body_len = strlen(response_buffer);
			response_ctx->final_chunk = true;
			return 0;
		}

		uint8_t *cert_data;
		uint8_t *key_data;
		size_t cert_len;
		size_t key_len;

		int parse_result = parse_multipart_data(upload_buffer, upload_cursor, boundary,
					       &cert_data, &cert_len, &key_data, &key_len);

		upload_cursor = 0;

		if (parse_result != 0) {
			snprintf(response_buffer, sizeof(response_buffer),
				 "{\"success\":false,\"message\":\"Failed to parse certificate and key from upload\"}");

			response_ctx->status = HTTP_400_BAD_REQUEST;
			response_ctx->body = response_buffer;
			response_ctx->body_len = strlen(response_buffer);
			response_ctx->final_chunk = true;
			return 0;
		}

		int cert_result = store_certificate_in_settings(cert_data, cert_len);
		int key_result = store_private_key_in_settings(key_data, key_len);

		if (cert_result != 0 || key_result != 0) {
			snprintf(response_buffer, sizeof(response_buffer),
				 "{\"success\":false,\"message\":\"Failed to store certificates in settings (cert: %d, key: %d)\"}",
				 cert_result, key_result);

			response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
			response_ctx->body = response_buffer;
			response_ctx->body_len = strlen(response_buffer);
			response_ctx->final_chunk = true;
			return 0;
		}

		snprintf(response_buffer, sizeof(response_buffer),
			 "{\"success\":true,\"message\":\"Certificates uploaded successfully. Restart the server to use new certificates.\"}");

		response_ctx->status = HTTP_200_OK;
		response_ctx->body = response_buffer;
		response_ctx->body_len = strlen(response_buffer);
		response_ctx->final_chunk = true;
	}

	return 0;
}

struct http_resource_detail_dynamic certificates_upload_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
		.content_type = "application/json",
	},
	.cb = certificates_upload_handler,
	.user_data = NULL,
};
