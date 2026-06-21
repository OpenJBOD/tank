/*
 * Static asset storage and helpers for the HTTP server.
 */

#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/sys/util.h>

#include "http/auth.h"
#include "http/static_assets.h"

LOG_MODULE_REGISTER(tank_http_static, LOG_LEVEL_INF);

static const uint8_t index_html_gz[] = {
#include "index.html.gz.inc"
};

static const uint8_t about_html_gz[] = {
#include "about.html.gz.inc"
};

static const uint8_t network_html_gz[] = {
#include "network.html.gz.inc"
};

static const uint8_t power_html_gz[] = {
#include "power.html.gz.inc"
};

static const uint8_t users_html_gz[] = {
#include "users.html.gz.inc"
};

static const uint8_t reset_html_gz[] = {
#include "reset.html.gz.inc"
};

static const uint8_t firmware_html_gz[] = {
#include "firmware.html.gz.inc"
};

static const uint8_t console_html_gz[] = {
#include "console.html.gz.inc"
};

static const uint8_t login_html_gz[] = {
#include "login.html.gz.inc"
};

static const uint8_t tokens_html_gz[] = {
#include "tokens.html.gz.inc"
};

static const uint8_t http_html_gz[] = {
#include "http.html.gz.inc"
};

static const uint8_t environment_html_gz[] = {
#include "environment.html.gz.inc"
};

struct http_resource_detail_static index_html_gz_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "text/html",
		},
	.static_data = index_html_gz,
	.static_data_len = sizeof(index_html_gz),
};

struct http_resource_detail_static about_html_gz_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "text/html",
		},
	.static_data = about_html_gz,
	.static_data_len = sizeof(about_html_gz),
};

struct http_resource_detail_static network_html_gz_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "text/html",
		},
	.static_data = network_html_gz,
	.static_data_len = sizeof(network_html_gz),
};

struct http_resource_detail_static power_html_gz_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "text/html",
		},
	.static_data = power_html_gz,
	.static_data_len = sizeof(power_html_gz),
};

struct http_resource_detail_static users_html_gz_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "text/html",
		},
	.static_data = users_html_gz,
	.static_data_len = sizeof(users_html_gz),
};

struct http_resource_detail_static reset_html_gz_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "text/html",
		},
	.static_data = reset_html_gz,
	.static_data_len = sizeof(reset_html_gz),
};

struct http_resource_detail_static firmware_html_gz_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "text/html",
		},
	.static_data = firmware_html_gz,
	.static_data_len = sizeof(firmware_html_gz),
};

struct http_resource_detail_static console_html_gz_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "text/html",
		},
	.static_data = console_html_gz,
	.static_data_len = sizeof(console_html_gz),
};

struct http_resource_detail_static login_html_gz_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "text/html",
		},
	.static_data = login_html_gz,
	.static_data_len = sizeof(login_html_gz),
};

struct http_resource_detail_static tokens_html_gz_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "text/html",
		},
	.static_data = tokens_html_gz,
	.static_data_len = sizeof(tokens_html_gz),
};

struct http_resource_detail_static http_html_gz_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "text/html",
		},
	.static_data = http_html_gz,
	.static_data_len = sizeof(http_html_gz),
};

struct http_resource_detail_static environment_html_gz_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "text/html",
		},
	.static_data = environment_html_gz,
	.static_data_len = sizeof(environment_html_gz),
};

static void serve_static(struct http_response_ctx *response_ctx,
			 const struct http_resource_detail_static *static_detail)
{
	static struct http_header static_headers[2];
	size_t header_count = 0;

	if (static_detail->common.content_type) {
		static_headers[header_count].name = "Content-Type";
		static_headers[header_count].value = static_detail->common.content_type;
		header_count++;
	}

	if (static_detail->common.content_encoding) {
		static_headers[header_count].name = "Content-Encoding";
		static_headers[header_count].value = static_detail->common.content_encoding;
		header_count++;
	}

	response_ctx->status = HTTP_200_OK;
	response_ctx->headers = static_headers;
	response_ctx->header_count = header_count;
	response_ctx->body = static_detail->static_data;
	response_ctx->body_len = static_detail->static_data_len;
	response_ctx->final_chunk = true;
}

int authenticated_static_handler(struct http_client_ctx *client, enum http_data_status status,
				 const struct http_request_ctx *request_ctx,
				 struct http_response_ctx *response_ctx,
				 void *user_data)
{
	(void)request_ctx;

	if (status == HTTP_SERVER_DATA_FINAL) {
		if (http_check_auth(client) != 0) {
			/* HTML page request without a session -> bounce to the login page. */
			http_send_login_redirect(response_ctx);
			return 0;
		}
		serve_static(response_ctx, (const struct http_resource_detail_static *)user_data);
	}

	return 0;
}

int public_static_handler(struct http_client_ctx *client, enum http_data_status status,
			  const struct http_request_ctx *request_ctx,
			  struct http_response_ctx *response_ctx,
			  void *user_data)
{
	(void)client;
	(void)request_ctx;

	if (status == HTTP_SERVER_DATA_FINAL) {
		serve_static(response_ctx, (const struct http_resource_detail_static *)user_data);
	}

	return 0;
}
