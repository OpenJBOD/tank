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

static const uint8_t main_js_gz[] = {
#include "main.js.gz.inc"
};

static const uint8_t style_css_gz[] = {
#include "style.css.gz.inc"
};

static const uint8_t network_html_gz[] = {
#include "network.html.gz.inc"
};

static const uint8_t network_js_gz[] = {
#include "network.js.gz.inc"
};

static const uint8_t power_html_gz[] = {
#include "power.html.gz.inc"
};

static const uint8_t power_js_gz[] = {
#include "power.js.gz.inc"
};

static const uint8_t users_html_gz[] = {
#include "users.html.gz.inc"
};

static const uint8_t users_js_gz[] = {
#include "users.js.gz.inc"
};

static const uint8_t reset_html_gz[] = {
#include "reset.html.gz.inc"
};

static const uint8_t reset_js_gz[] = {
#include "reset.js.gz.inc"
};

static const uint8_t http_html_gz[] = {
#include "http.html.gz.inc"
};

static const uint8_t http_js_gz[] = {
#include "http.js.gz.inc"
};

static const uint8_t environment_html_gz[] = {
#include "environment.html.gz.inc"
};

static const uint8_t environment_js_gz[] = {
#include "environment.js.gz.inc"
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

struct http_resource_detail_static main_js_gz_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "text/javascript",
		},
	.static_data = main_js_gz,
	.static_data_len = sizeof(main_js_gz),
};

struct http_resource_detail_static style_css_gz_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "text/css",
		},
	.static_data = style_css_gz,
	.static_data_len = sizeof(style_css_gz),
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

struct http_resource_detail_static network_js_gz_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "text/javascript",
		},
	.static_data = network_js_gz,
	.static_data_len = sizeof(network_js_gz),
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

struct http_resource_detail_static power_js_gz_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "text/javascript",
		},
	.static_data = power_js_gz,
	.static_data_len = sizeof(power_js_gz),
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

struct http_resource_detail_static users_js_gz_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "text/javascript",
		},
	.static_data = users_js_gz,
	.static_data_len = sizeof(users_js_gz),
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

struct http_resource_detail_static reset_js_gz_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "text/javascript",
		},
	.static_data = reset_js_gz,
	.static_data_len = sizeof(reset_js_gz),
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

struct http_resource_detail_static http_js_gz_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "text/javascript",
		},
	.static_data = http_js_gz,
	.static_data_len = sizeof(http_js_gz),
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

struct http_resource_detail_static environment_js_gz_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "text/javascript",
		},
	.static_data = environment_js_gz,
	.static_data_len = sizeof(environment_js_gz),
};

int authenticated_static_handler(struct http_client_ctx *client, enum http_data_status status,
				 const struct http_request_ctx *request_ctx,
				 struct http_response_ctx *response_ctx,
				 void *user_data)
{
	(void)request_ctx;

	const struct http_resource_detail_static *static_detail =
		(const struct http_resource_detail_static *)user_data;

	if (status == HTTP_SERVER_DATA_FINAL) {
		int auth_result = http_basic_auth_check(client);
		if (auth_result != 0) {
			LOG_WRN("Authentication failed for static resource");
			http_send_auth_required_response(response_ctx);
			return 0;
		}

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

	return 0;
}
