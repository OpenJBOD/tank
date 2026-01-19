/*
 * Authentication helpers for the web server.
 */

#ifndef TANK_HTTP_AUTH_H_
#define TANK_HTTP_AUTH_H_

#include <zephyr/net/http/server.h>

int http_basic_auth_check(struct http_client_ctx *client);
void http_send_auth_required_response(struct http_response_ctx *response_ctx);

#endif /* TANK_HTTP_AUTH_H_ */
