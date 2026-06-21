/*
 * Copyright (c) 2025 The OpenJBOD Project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Long-lived bearer API tokens for programmatic access (Authorization: Bearer).
 * A token is "<id>.<secret>": the id identifies the record (and is shown in the
 * UI), the secret is a 256-bit value shown only once at creation. Only the id,
 * label, and SHA-256(secret) are persisted (in the settings "tok" subtree), so
 * tokens survive reboots but the secret can never be recovered from storage.
 */

#ifndef TANK_HTTP_API_TOKEN_H_
#define TANK_HTTP_API_TOKEN_H_

#include <stddef.h>

#define API_TOKEN_ID_HEX     8                 /* 4 random bytes */
#define API_TOKEN_SECRET_HEX 64                /* 32 random bytes */
#define API_TOKEN_HASH_HEX   64                /* SHA-256 hex */
#define API_TOKEN_LABEL_MAX  32
#define API_TOKEN_MAX        8
/* "<id>.<secret>\0" */
#define API_TOKEN_STR_MAX    (API_TOKEN_ID_HEX + 1 + API_TOKEN_SECRET_HEX + 1)

/**
 * Create a token with the given label. Writes the full "<id>.<secret>" into
 * token_out (>= API_TOKEN_STR_MAX). Returns 0, -ENOSPC if full, or negative errno.
 */
int api_token_create(const char *label, char *token_out, size_t token_out_len);

/** Validate a presented "<id>.<secret>" bearer token. 0 if valid, -EACCES otherwise. */
int api_token_validate(const char *token);

/** Revoke a token by its id. No-op if not found. */
void api_token_destroy(const char *id);

/** Callback for api_token_list(); id and label are NUL-terminated. */
typedef void (*api_token_list_cb)(const char *id, const char *label, void *user);

/** Invoke cb for each stored token (metadata only - never the secret). */
void api_token_list(api_token_list_cb cb, void *user);

#endif /* TANK_HTTP_API_TOKEN_H_ */
