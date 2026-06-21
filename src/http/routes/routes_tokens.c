/*
 * Copyright (c) 2025 The OpenJBOD Project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * /api/tokens: manage long-lived bearer API tokens.
 *   GET            -> list tokens (id + label only)
 *   POST {"label"} -> create, returns {"token", "id"} once
 *   DELETE ?id=... -> revoke (id in the query string; DELETE bodies aren't delivered)
 */

#include "http/routes/routes_tokens.h"

#include <stdio.h>
#include <string.h>

#include <zephyr/data/json.h>
#include <zephyr/net/http/server.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

#include "http/api_token.h"
#include "http/auth.h"

LOG_MODULE_REGISTER(tank_http_tokens, LOG_LEVEL_INF);

struct token_create_req {
	char *label;
};
static const struct json_obj_descr create_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct token_create_req, label, JSON_TOK_STRING),
};

struct token_delete_req {
	char *id;
};
static const struct json_obj_descr delete_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct token_delete_req, id, JSON_TOK_STRING),
};

static void respond_json(struct http_response_ctx *response_ctx, enum http_status status,
			 const char *body, size_t body_len)
{
	static const struct http_header json_hdr[] = {
		{"Content-Type", "application/json"},
	};

	response_ctx->status = status;
	response_ctx->headers = json_hdr;
	response_ctx->header_count = ARRAY_SIZE(json_hdr);
	response_ctx->body = (const uint8_t *)body;
	response_ctx->body_len = body_len;
	response_ctx->final_chunk = true;
}

struct list_ctx {
	char *buf;
	size_t cap;
	size_t pos;
	bool first;
};

/* Extract a query parameter value from a URL (e.g. id from "/api/tokens?id=ab12"). */
static bool get_query_param(const char *url, const char *key, char *out, size_t out_len)
{
	const char *q = url ? strchr(url, '?') : NULL;

	if (!q) {
		return false;
	}

	size_t klen = strlen(key);

	for (const char *p = q + 1; p && *p;) {
		if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
			const char *v = p + klen + 1;
			const char *amp = strchr(v, '&');
			size_t vlen = amp ? (size_t)(amp - v) : strlen(v);

			if (vlen >= out_len) {
				return false;
			}
			memcpy(out, v, vlen);
			out[vlen] = '\0';
			return true;
		}
		const char *amp = strchr(p, '&');

		p = amp ? amp + 1 : NULL;
	}
	return false;
}

static void list_cb(const char *id, const char *label, void *user)
{
	struct list_ctx *c = user;
	int n = snprintf(c->buf + c->pos, c->cap - c->pos, "%s{\"id\":\"%s\",\"label\":\"%s\"}",
			 c->first ? "" : ",", id, label);

	if (n > 0 && (size_t)n < c->cap - c->pos) {
		c->pos += (size_t)n;
		c->first = false;
	}
}

static int tokens_handler(struct http_client_ctx *client, enum http_data_status status,
			  const struct http_request_ctx *request_ctx,
			  struct http_response_ctx *response_ctx, void *user_data)
{
	static uint8_t payload[256];
	static size_t cursor;
	static struct http_client_ctx *owner;
	static char resp[512];

	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_DATA_ABORTED) {
		cursor = 0;
		owner = NULL;
		return 0;
	}

	if (cursor == 0) {
		owner = client;
	} else if (owner != client) {
		respond_json(response_ctx, HTTP_409_CONFLICT,
			     "{\"success\":false,\"message\":\"busy\"}", 35);
		return 0;
	}

	if (request_ctx->data_len + cursor < sizeof(payload)) {
		memcpy(payload + cursor, request_ctx->data, request_ctx->data_len);
		cursor += request_ctx->data_len;
	}

	if (status != HTTP_SERVER_DATA_FINAL) {
		return 0;
	}

	payload[cursor] = '\0';
	size_t len = cursor;

	cursor = 0;
	owner = NULL;

	if (http_check_auth(client) != 0) {
		http_send_auth_required_response(response_ctx);
		return 0;
	}

	switch (client->method) {
	case HTTP_GET: {
		struct list_ctx ctx = {.buf = resp, .cap = sizeof(resp), .pos = 0, .first = true};

		ctx.pos += (size_t)snprintf(resp, sizeof(resp), "[");
		api_token_list(list_cb, &ctx);
		if (ctx.pos < sizeof(resp) - 1) {
			resp[ctx.pos++] = ']';
		}
		respond_json(response_ctx, HTTP_200_OK, resp, ctx.pos);
		break;
	}
	case HTTP_POST: {
		struct token_create_req req = {0};

		(void)json_obj_parse((char *)payload, len, create_descr,
				     ARRAY_SIZE(create_descr), &req);

		char token[API_TOKEN_STR_MAX];
		int rc = api_token_create(req.label ? req.label : "token", token, sizeof(token));

		if (rc == -ENOSPC) {
			respond_json(response_ctx, HTTP_409_CONFLICT,
				     "{\"success\":false,\"message\":\"token limit reached\"}", 51);
			break;
		}
		if (rc != 0) {
			respond_json(response_ctx, HTTP_500_INTERNAL_SERVER_ERROR,
				     "{\"success\":false,\"message\":\"create failed\"}", 44);
			break;
		}

		/* id is the part before the '.' */
		char id[API_TOKEN_ID_HEX + 1];

		memcpy(id, token, API_TOKEN_ID_HEX);
		id[API_TOKEN_ID_HEX] = '\0';

		int n = snprintf(resp, sizeof(resp),
				 "{\"success\":true,\"id\":\"%s\",\"token\":\"%s\"}", id, token);
		respond_json(response_ctx, HTTP_200_OK, resp, (n > 0) ? (size_t)n : 0);
		break;
	}
	case HTTP_DELETE: {
		/* DELETE bodies are not delivered by the server, so the id comes from
		 * the query string (?id=...); fall back to a JSON body if present.
		 */
		char id[API_TOKEN_ID_HEX + 1];

		if (get_query_param(client->url_buffer, "id", id, sizeof(id))) {
			api_token_destroy(id);
			respond_json(response_ctx, HTTP_200_OK, "{\"success\":true}", 16);
			break;
		}

		struct token_delete_req req = {0};

		if (len > 0 && json_obj_parse((char *)payload, len, delete_descr,
					      ARRAY_SIZE(delete_descr), &req) >= 0 && req.id) {
			api_token_destroy(req.id);
			respond_json(response_ctx, HTTP_200_OK, "{\"success\":true}", 16);
			break;
		}

		respond_json(response_ctx, HTTP_400_BAD_REQUEST,
			     "{\"success\":false,\"message\":\"missing id\"}", 41);
		break;
	}
	default:
		respond_json(response_ctx, HTTP_405_METHOD_NOT_ALLOWED,
			     "{\"success\":false}", 17);
		break;
	}

	return 0;
}

struct http_resource_detail_dynamic tokens_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET) | BIT(HTTP_POST) | BIT(HTTP_DELETE),
		.content_type = "application/json",
	},
	.cb = tokens_handler,
	.user_data = NULL,
};
