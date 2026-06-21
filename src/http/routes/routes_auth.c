/*
 * Copyright (c) 2025 The OpenJBOD Project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Login / logout endpoints for the cookie-session auth flow.
 */

#include "http/routes/routes_auth.h"

#include <stdio.h>
#include <string.h>

#include <zephyr/data/json.h>
#include <zephyr/net/http/server.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

#include "http/auth.h"
#include "http/session.h"
#include "settings.h"

LOG_MODULE_REGISTER(tank_http_auth, LOG_LEVEL_INF);

struct login_req {
	char *username;
	char *password;
};

static const struct json_obj_descr login_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct login_req, username, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct login_req, password, JSON_TOK_STRING),
};

static void respond_json(struct http_response_ctx *response_ctx, enum http_status status,
			 const char *body)
{
	static const struct http_header json_hdr[] = {
		{"Content-Type", "application/json"},
	};

	response_ctx->status = status;
	response_ctx->headers = json_hdr;
	response_ctx->header_count = ARRAY_SIZE(json_hdr);
	response_ctx->body = (const uint8_t *)body;
	response_ctx->body_len = strlen(body);
	response_ctx->final_chunk = true;
}

static int login_handler(struct http_client_ctx *client, enum http_data_status status,
			 const struct http_request_ctx *request_ctx,
			 struct http_response_ctx *response_ctx, void *user_data)
{
	static uint8_t payload[256];
	static size_t cursor;
	static struct http_client_ctx *owner;

	/* Persist across the response of one request (single-threaded server). */
	static char cookie_buf[160];
	static struct http_header login_headers[2];

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
			     "{\"success\":false,\"message\":\"busy\"}");
		return 0;
	}

	if (request_ctx->data_len + cursor > sizeof(payload) - 1) {
		cursor = 0;
		owner = NULL;
		respond_json(response_ctx, HTTP_413_PAYLOAD_TOO_LARGE,
			     "{\"success\":false,\"message\":\"payload too large\"}");
		return 0;
	}

	memcpy(payload + cursor, request_ctx->data, request_ctx->data_len);
	cursor += request_ctx->data_len;

	if (status != HTTP_SERVER_DATA_FINAL) {
		return 0;
	}

	payload[cursor] = '\0';
	size_t len = cursor;

	cursor = 0;
	owner = NULL;

	struct login_req req = {0};
	int parsed = json_obj_parse((char *)payload, len, login_descr,
				    ARRAY_SIZE(login_descr), &req);

	if (parsed != (int)BIT_MASK(ARRAY_SIZE(login_descr)) || !req.username || !req.password) {
		respond_json(response_ctx, HTTP_400_BAD_REQUEST,
			     "{\"success\":false,\"message\":\"invalid request\"}");
		return 0;
	}

	if (openjbod_auth_verify_credentials(req.username, req.password) != 0) {
		LOG_WRN("Login failed for user '%s'", req.username);
		respond_json(response_ctx, HTTP_401_UNAUTHORIZED,
			     "{\"success\":false,\"message\":\"invalid credentials\"}");
		return 0;
	}

	char sid[SESSION_ID_HEX_LEN + 1];

	if (session_create(req.username, sid, sizeof(sid)) != 0) {
		respond_json(response_ctx, HTTP_500_INTERNAL_SERVER_ERROR,
			     "{\"success\":false,\"message\":\"session error\"}");
		return 0;
	}

	snprintf(cookie_buf, sizeof(cookie_buf),
		 "session=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=%lld",
		 sid, (long long)(SESSION_TTL_MS / 1000));
	login_headers[0].name = "Set-Cookie";
	login_headers[0].value = cookie_buf;
	login_headers[1].name = "Content-Type";
	login_headers[1].value = "application/json";

	response_ctx->status = HTTP_200_OK;
	response_ctx->headers = login_headers;
	response_ctx->header_count = ARRAY_SIZE(login_headers);
	response_ctx->body = (const uint8_t *)"{\"success\":true}";
	response_ctx->body_len = strlen("{\"success\":true}");
	response_ctx->final_chunk = true;
	return 0;
}

static int logout_handler(struct http_client_ctx *client, enum http_data_status status,
			  const struct http_request_ctx *request_ctx,
			  struct http_response_ctx *response_ctx, void *user_data)
{
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status != HTTP_SERVER_DATA_FINAL) {
		return 0;
	}

	char sid[SESSION_ID_HEX_LEN + 1];

	if (http_get_cookie(client, "session", sid, sizeof(sid)) == 0) {
		session_destroy(sid);
	}

	static const struct http_header headers[] = {
		{"Set-Cookie", "session=; Path=/; HttpOnly; Max-Age=0"},
		{"Content-Type", "application/json"},
	};

	response_ctx->status = HTTP_200_OK;
	response_ctx->headers = headers;
	response_ctx->header_count = ARRAY_SIZE(headers);
	response_ctx->body = (const uint8_t *)"{\"success\":true}";
	response_ctx->body_len = strlen("{\"success\":true}");
	response_ctx->final_chunk = true;
	return 0;
}

struct http_resource_detail_dynamic login_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
		.content_type = "application/json",
	},
	.cb = login_handler,
	.user_data = NULL,
};

struct http_resource_detail_dynamic logout_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
		.content_type = "application/json",
	},
	.cb = logout_handler,
	.user_data = NULL,
};
