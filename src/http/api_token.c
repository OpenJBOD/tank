/*
 * Copyright (c) 2025 The OpenJBOD Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "http/api_token.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>

#include <psa/crypto.h>

LOG_MODULE_REGISTER(tank_api_token, LOG_LEVEL_INF);

struct api_token {
	char id[API_TOKEN_ID_HEX + 1];
	char label[API_TOKEN_LABEL_MAX];
	char hash[API_TOKEN_HASH_HEX + 1]; /* SHA-256(secret) hex */
	bool in_use;
};

static struct api_token tokens[API_TOKEN_MAX];
static K_MUTEX_DEFINE(token_lock);

static void bytes_to_hex(const uint8_t *in, size_t in_len, char *out)
{
	for (size_t i = 0; i < in_len; i++) {
		snprintf(&out[i * 2], 3, "%02x", in[i]);
	}
}

static int sha256_hex(const char *input, char *out, size_t out_len)
{
	psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
	uint8_t digest[32];
	size_t digest_len;

	if (out_len < API_TOKEN_HASH_HEX + 1) {
		return -EINVAL;
	}
	if (psa_hash_setup(&op, PSA_ALG_SHA_256) != PSA_SUCCESS) {
		return -EIO;
	}
	if (psa_hash_update(&op, (const uint8_t *)input, strlen(input)) != PSA_SUCCESS ||
	    psa_hash_finish(&op, digest, sizeof(digest), &digest_len) != PSA_SUCCESS) {
		psa_hash_abort(&op);
		return -EIO;
	}
	bytes_to_hex(digest, digest_len, out);
	return 0;
}

/* Constant-time compare of equal-length strings; 0 if equal. */
static int ct_streq(const char *a, const char *b, size_t len)
{
	const volatile uint8_t *pa = (const volatile uint8_t *)a;
	const volatile uint8_t *pb = (const volatile uint8_t *)b;
	uint8_t diff = 0;

	for (size_t i = 0; i < len; i++) {
		diff |= (uint8_t)(pa[i] ^ pb[i]);
	}
	return diff;
}

/* Keep only safe label characters to avoid JSON-injection in the list response. */
static void sanitize_label(const char *in, char *out, size_t out_len)
{
	size_t j = 0;

	for (size_t i = 0; in && in[i] && j < out_len - 1; i++) {
		char c = in[i];

		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		    (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_' || c == '.') {
			out[j++] = c;
		}
	}
	out[j] = '\0';
	if (j == 0) {
		strncpy(out, "token", out_len - 1);
		out[out_len - 1] = '\0';
	}
}

static void persist_slot(int i)
{
	char key[24];

	snprintf(key, sizeof(key), "tok/%d/id", i);
	settings_save_one(key, tokens[i].id, strlen(tokens[i].id));
	snprintf(key, sizeof(key), "tok/%d/label", i);
	settings_save_one(key, tokens[i].label, strlen(tokens[i].label));
	snprintf(key, sizeof(key), "tok/%d/hash", i);
	settings_save_one(key, tokens[i].hash, strlen(tokens[i].hash));
}

static void delete_slot_storage(int i)
{
	char key[24];

	snprintf(key, sizeof(key), "tok/%d/id", i);
	settings_delete(key);
	snprintf(key, sizeof(key), "tok/%d/label", i);
	settings_delete(key);
	snprintf(key, sizeof(key), "tok/%d/hash", i);
	settings_delete(key);
}

int api_token_create(const char *label, char *token_out, size_t token_out_len)
{
	uint8_t id_raw[4];
	uint8_t secret_raw[32];
	char secret[API_TOKEN_SECRET_HEX + 1];
	char hash[API_TOKEN_HASH_HEX + 1];
	int rc;

	if (!token_out || token_out_len < API_TOKEN_STR_MAX) {
		return -EINVAL;
	}

	if (sys_csrand_get(id_raw, sizeof(id_raw)) != 0 ||
	    sys_csrand_get(secret_raw, sizeof(secret_raw)) != 0) {
		return -EIO;
	}

	k_mutex_lock(&token_lock, K_FOREVER);

	int slot = -1;

	for (int i = 0; i < API_TOKEN_MAX; i++) {
		if (!tokens[i].in_use) {
			slot = i;
			break;
		}
	}
	if (slot < 0) {
		k_mutex_unlock(&token_lock);
		return -ENOSPC;
	}

	bytes_to_hex(id_raw, sizeof(id_raw), tokens[slot].id);
	bytes_to_hex(secret_raw, sizeof(secret_raw), secret);

	rc = sha256_hex(secret, hash, sizeof(hash));
	if (rc != 0) {
		memset(&tokens[slot], 0, sizeof(tokens[slot]));
		k_mutex_unlock(&token_lock);
		return rc;
	}

	memcpy(tokens[slot].hash, hash, sizeof(hash));
	sanitize_label(label, tokens[slot].label, sizeof(tokens[slot].label));
	tokens[slot].in_use = true;
	persist_slot(slot);

	snprintf(token_out, token_out_len, "%s.%s", tokens[slot].id, secret);
	LOG_INF("API token '%s' created (id %s)", tokens[slot].label, tokens[slot].id);

	k_mutex_unlock(&token_lock);
	return 0;
}

int api_token_validate(const char *token)
{
	if (!token) {
		return -EACCES;
	}

	const char *dot = strchr(token, '.');

	if (!dot) {
		return -EACCES;
	}

	size_t id_len = (size_t)(dot - token);
	const char *secret = dot + 1;

	if (id_len != API_TOKEN_ID_HEX || strlen(secret) != API_TOKEN_SECRET_HEX) {
		return -EACCES;
	}

	char hash[API_TOKEN_HASH_HEX + 1];

	if (sha256_hex(secret, hash, sizeof(hash)) != 0) {
		return -EACCES;
	}

	int rc = -EACCES;

	k_mutex_lock(&token_lock, K_FOREVER);
	for (int i = 0; i < API_TOKEN_MAX; i++) {
		if (tokens[i].in_use &&
		    memcmp(tokens[i].id, token, API_TOKEN_ID_HEX) == 0 &&
		    ct_streq(tokens[i].hash, hash, API_TOKEN_HASH_HEX) == 0) {
			rc = 0;
			break;
		}
	}
	k_mutex_unlock(&token_lock);
	return rc;
}

void api_token_destroy(const char *id)
{
	if (!id || strlen(id) != API_TOKEN_ID_HEX) {
		return;
	}

	k_mutex_lock(&token_lock, K_FOREVER);
	for (int i = 0; i < API_TOKEN_MAX; i++) {
		if (tokens[i].in_use && memcmp(tokens[i].id, id, API_TOKEN_ID_HEX) == 0) {
			LOG_INF("API token id %s revoked", tokens[i].id);
			memset(&tokens[i], 0, sizeof(tokens[i]));
			delete_slot_storage(i);
			break;
		}
	}
	k_mutex_unlock(&token_lock);
}

void api_token_list(api_token_list_cb cb, void *user)
{
	k_mutex_lock(&token_lock, K_FOREVER);
	for (int i = 0; i < API_TOKEN_MAX; i++) {
		if (tokens[i].in_use) {
			cb(tokens[i].id, tokens[i].label, user);
		}
	}
	k_mutex_unlock(&token_lock);
}

/* ---- settings persistence (subtree "tok") -------------------------------- */

static int tok_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const char *next;
	size_t name_len = settings_name_next(name, &next);

	if (!next) {
		return -ENOENT;
	}

	int idx = (int)strtoul(name, NULL, 10);

	if (idx < 0 || idx >= API_TOKEN_MAX) {
		return -ENOENT;
	}

	struct api_token *t = &tokens[idx];
	ssize_t rc;

	if (strcmp(next, "id") == 0) {
		rc = read_cb(cb_arg, t->id, sizeof(t->id) - 1);
		if (rc >= 0) {
			t->id[rc] = '\0';
			t->in_use = true;
		}
	} else if (strcmp(next, "label") == 0) {
		rc = read_cb(cb_arg, t->label, sizeof(t->label) - 1);
		if (rc >= 0) {
			t->label[rc] = '\0';
		}
	} else if (strcmp(next, "hash") == 0) {
		rc = read_cb(cb_arg, t->hash, sizeof(t->hash) - 1);
		if (rc >= 0) {
			t->hash[rc] = '\0';
		}
	} else {
		return -ENOENT;
	}

	return rc < 0 ? (int)rc : 0;
}

static int tok_settings_export(int (*cb)(const char *name, const void *value, size_t val_len))
{
	char key[24];

	for (int i = 0; i < API_TOKEN_MAX; i++) {
		if (!tokens[i].in_use) {
			continue;
		}
		snprintf(key, sizeof(key), "tok/%d/id", i);
		cb(key, tokens[i].id, strlen(tokens[i].id));
		snprintf(key, sizeof(key), "tok/%d/label", i);
		cb(key, tokens[i].label, strlen(tokens[i].label));
		snprintf(key, sizeof(key), "tok/%d/hash", i);
		cb(key, tokens[i].hash, strlen(tokens[i].hash));
	}
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(tok, "tok", NULL, tok_settings_set, NULL, tok_settings_export);
