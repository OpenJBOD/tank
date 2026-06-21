/*
 * Authentication helpers for the web server.
 */

#ifndef TANK_HTTP_AUTH_H_
#define TANK_HTTP_AUTH_H_

#include <stddef.h>

#include <zephyr/net/http/server.h>

/* Unified auth gate: valid session cookie OR bearer API token. 0 if authenticated. */
int http_check_auth(struct http_client_ctx *client);

/* Extract a cookie value by name into out (NUL-terminated). 0 / -ENOENT / -ENOMEM. */
int http_get_cookie(struct http_client_ctx *client, const char *name, char *out, size_t out_len);

/* 401 JSON for API endpoints (no WWW-Authenticate, so no browser popup). */
void http_send_auth_required_response(struct http_response_ctx *response_ctx);

/* 302 redirect to /login for HTML page requests. */
void http_send_login_redirect(struct http_response_ctx *response_ctx);

#endif /* TANK_HTTP_AUTH_H_ */
