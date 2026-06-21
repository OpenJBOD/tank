/*
 * Authentication helpers for the web server.
 *
 * The gate (http_check_auth) accepts either a valid session cookie (the browser
 * login flow) or a bearer API token (scripts). HTTP Basic auth has been removed -
 * see BASIC_AUTH_DEPRECATION.md.
 */

#include <errno.h>
#include <string.h>
#include <strings.h>

#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/sys/util.h>

#include "http/api_token.h"
#include "http/auth.h"
#include "http/session.h"

LOG_MODULE_REGISTER(tank_auth, LOG_LEVEL_INF);

int http_get_cookie(struct http_client_ctx *client, const char *name, char *out, size_t out_len)
{
	const struct http_header *headers = client->header_capture_ctx.headers;
	size_t header_count = client->header_capture_ctx.count;
	const char *cookie = NULL;
	size_t name_len = strlen(name);

	for (size_t i = 0; i < header_count; i++) {
		if (strcasecmp(headers[i].name, "Cookie") == 0) {
			cookie = headers[i].value;
			break;
		}
	}
	if (!cookie) {
		return -ENOENT;
	}

	/* Cookie: name1=value1; name2=value2 */
	for (const char *p = cookie; p && *p;) {
		while (*p == ' ') {
			p++;
		}
		if (strncmp(p, name, name_len) == 0 && p[name_len] == '=') {
			const char *v = p + name_len + 1;
			const char *end = strchr(v, ';');
			size_t vlen = end ? (size_t)(end - v) : strlen(v);

			if (vlen >= out_len) {
				return -ENOMEM;
			}
			memcpy(out, v, vlen);
			out[vlen] = '\0';
			return 0;
		}
		const char *semi = strchr(p, ';');

		p = semi ? semi + 1 : NULL;
	}
	return -ENOENT;
}

int http_check_auth(struct http_client_ctx *client)
{
	char sid[SESSION_ID_HEX_LEN + 1];

	/* 1. Browser session cookie. */
	if (http_get_cookie(client, "session", sid, sizeof(sid)) == 0 &&
	    session_validate(sid) == 0) {
		return 0;
	}

	/* 2. Bearer API token (scripts). */
	const struct http_header *headers = client->header_capture_ctx.headers;
	size_t header_count = client->header_capture_ctx.count;

	for (size_t i = 0; i < header_count; i++) {
		if (strcasecmp(headers[i].name, "Authorization") == 0 &&
		    strncasecmp(headers[i].value, "Bearer ", 7) == 0) {
			if (api_token_validate(headers[i].value + 7) == 0) {
				return 0;
			}
			break;
		}
	}

	return -EACCES;
}

void http_send_auth_required_response(struct http_response_ctx *response_ctx)
{
	/* 401 without WWW-Authenticate: no browser popup; clients (and the SPA) treat
	 * it as "not logged in". Scripts authenticate with a bearer API token.
	 */
	static const char body[] = "{\"error\":\"unauthorized\"}";
	static const struct http_header headers[] = {
		{"Content-Type", "application/json"},
	};

	response_ctx->status = HTTP_401_UNAUTHORIZED;
	response_ctx->headers = headers;
	response_ctx->header_count = ARRAY_SIZE(headers);
	response_ctx->body = (const uint8_t *)body;
	response_ctx->body_len = strlen(body);
	response_ctx->final_chunk = true;
}

void http_send_login_redirect(struct http_response_ctx *response_ctx)
{
	static const char body[] = "Redirecting to /login";
	static const struct http_header headers[] = {
		{"Location", "/login"},
		{"Content-Type", "text/plain"},
	};

	response_ctx->status = HTTP_302_FOUND;
	response_ctx->headers = headers;
	response_ctx->header_count = ARRAY_SIZE(headers);
	response_ctx->body = (const uint8_t *)body;
	response_ctx->body_len = strlen(body);
	response_ctx->final_chunk = true;
}
