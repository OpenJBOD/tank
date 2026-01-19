/*
 * Authentication helpers for the web server.
 */

#include <errno.h>
#include <string.h>
#include <strings.h>

#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/sys/base64.h>
#include <zephyr/sys/util.h>

#include "settings.h"

LOG_MODULE_REGISTER(tank_auth, LOG_LEVEL_INF);

int http_basic_auth_check(struct http_client_ctx *client)
{
	const struct http_header *headers = client->header_capture_ctx.headers;
	size_t header_count = client->header_capture_ctx.count;
	const char *auth_header = NULL;
	char credentials[256];
	size_t credentials_len;
	char username[64];
	char password[64];
	char *colon_pos;
	int rc;

	for (size_t i = 0; i < header_count; i++) {
		if (strcasecmp(headers[i].name, "Authorization") == 0) {
			auth_header = headers[i].value;
			break;
		}
	}

	if (!auth_header) {
		LOG_WRN("No Authorization header found");
		return -EACCES;
	}

	if (strncasecmp(auth_header, "Basic ", 6) != 0) {
		LOG_WRN("Authorization header is not Basic auth");
		return -EACCES;
	}

	rc = base64_decode((uint8_t *)credentials, sizeof(credentials) - 1,
			   &credentials_len, (const uint8_t *)auth_header + 6,
			   strlen(auth_header + 6));
	if (rc != 0) {
		LOG_ERR("Failed to decode Basic auth credentials: %d", rc);
		return -EACCES;
	}

	credentials[credentials_len] = '\0';

	colon_pos = strchr(credentials, ':');
	if (!colon_pos) {
		LOG_WRN("Invalid Basic auth credentials format");
		return -EACCES;
	}

	*colon_pos = '\0';
	strncpy(username, credentials, sizeof(username) - 1);
	username[sizeof(username) - 1] = '\0';
	strncpy(password, colon_pos + 1, sizeof(password) - 1);
	password[sizeof(password) - 1] = '\0';

	rc = openjbod_auth_verify_credentials(username, password);
	if (rc != 0) {
		LOG_WRN("Authentication failed for user: %s", username);
		return -EACCES;
	}

	LOG_DBG("Authentication successful for user: %s", username);
	return 0;
}

void http_send_auth_required_response(struct http_response_ctx *response_ctx)
{
	static const char auth_body[] = "Unauthorized";
	static const struct http_header auth_headers[] = {
		{"WWW-Authenticate", "Basic realm=\"OpenJBOD\", charset=\"UTF-8\""},
		{"Content-Type", "text/plain"},
	};

	response_ctx->status = HTTP_401_UNAUTHORIZED;
	response_ctx->headers = auth_headers;
	response_ctx->header_count = ARRAY_SIZE(auth_headers);
	response_ctx->body = (const uint8_t *)auth_body;
	response_ctx->body_len = strlen(auth_body);
	response_ctx->final_chunk = true;
}
