/*
 * Static asset definitions for the HTTP server.
 */

#ifndef TANK_HTTP_STATIC_ASSETS_H_
#define TANK_HTTP_STATIC_ASSETS_H_

#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>

int authenticated_static_handler(struct http_client_ctx *client,
				 enum http_data_status status,
				 const struct http_request_ctx *request_ctx,
				 struct http_response_ctx *response_ctx,
				 void *user_data);

/* Serves a static asset without an auth check (e.g. the login page). */
int public_static_handler(struct http_client_ctx *client,
			  enum http_data_status status,
			  const struct http_request_ctx *request_ctx,
			  struct http_response_ctx *response_ctx,
			  void *user_data);

extern struct http_resource_detail_static index_html_gz_resource_detail;
extern struct http_resource_detail_static about_html_gz_resource_detail;
extern struct http_resource_detail_static network_html_gz_resource_detail;
extern struct http_resource_detail_static power_html_gz_resource_detail;
extern struct http_resource_detail_static users_html_gz_resource_detail;
extern struct http_resource_detail_static reset_html_gz_resource_detail;
extern struct http_resource_detail_static http_html_gz_resource_detail;
extern struct http_resource_detail_static environment_html_gz_resource_detail;
extern struct http_resource_detail_static firmware_html_gz_resource_detail;
extern struct http_resource_detail_static console_html_gz_resource_detail;
extern struct http_resource_detail_static login_html_gz_resource_detail;
extern struct http_resource_detail_static tokens_html_gz_resource_detail;

#endif /* TANK_HTTP_STATIC_ASSETS_H_ */
