/*
 * Copyright (c) 2025 The OpenJBOD Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "http/session.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(tank_session, LOG_LEVEL_INF);

#define SESSION_USERNAME_MAX 32

struct session {
	char id[SESSION_ID_HEX_LEN + 1];
	char username[SESSION_USERNAME_MAX];
	int64_t expires_at;
	bool in_use;
};

static struct session sessions[SESSION_MAX];
static K_MUTEX_DEFINE(session_lock);

/* Constant-time compare of two equal-length NUL-terminated strings. Returns 0 if
 * equal. Avoids leaking how much of a guessed session id matched. */
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

/* Drop expired sessions. Caller holds session_lock. */
static void session_gc_locked(int64_t now)
{
	for (int i = 0; i < SESSION_MAX; i++) {
		if (sessions[i].in_use && now >= sessions[i].expires_at) {
			memset(&sessions[i], 0, sizeof(sessions[i]));
		}
	}
}

int session_create(const char *username, char *id_out, size_t id_out_len)
{
	uint8_t raw[SESSION_ID_BYTES];
	char id[SESSION_ID_HEX_LEN + 1];
	int rc;

	if (!username || !id_out || id_out_len <= SESSION_ID_HEX_LEN) {
		return -EINVAL;
	}

	rc = sys_csrand_get(raw, sizeof(raw));
	if (rc != 0) {
		LOG_ERR("session id RNG failed: %d", rc);
		return rc;
	}
	for (size_t i = 0; i < sizeof(raw); i++) {
		snprintf(&id[i * 2], 3, "%02x", raw[i]);
	}

	int64_t now = k_uptime_get();

	k_mutex_lock(&session_lock, K_FOREVER);
	session_gc_locked(now);

	int slot = -1;

	for (int i = 0; i < SESSION_MAX; i++) {
		if (!sessions[i].in_use) {
			slot = i;
			break;
		}
	}
	if (slot < 0) {
		/* All slots live: evict the one nearest expiry. */
		int64_t oldest = INT64_MAX;

		for (int i = 0; i < SESSION_MAX; i++) {
			if (sessions[i].expires_at < oldest) {
				oldest = sessions[i].expires_at;
				slot = i;
			}
		}
		LOG_WRN("session table full, evicting slot %d", slot);
	}

	memset(&sessions[slot], 0, sizeof(sessions[slot]));
	memcpy(sessions[slot].id, id, sizeof(id));
	strncpy(sessions[slot].username, username, sizeof(sessions[slot].username) - 1);
	sessions[slot].expires_at = now + SESSION_TTL_MS;
	sessions[slot].in_use = true;
	k_mutex_unlock(&session_lock);

	memcpy(id_out, id, sizeof(id));
	LOG_INF("session created for '%s' (slot %d)", username, slot);
	return 0;
}

int session_validate(const char *id)
{
	if (!id || strlen(id) != SESSION_ID_HEX_LEN) {
		return -EACCES;
	}

	int64_t now = k_uptime_get();
	int rc = -EACCES;

	k_mutex_lock(&session_lock, K_FOREVER);
	session_gc_locked(now);
	for (int i = 0; i < SESSION_MAX; i++) {
		if (sessions[i].in_use &&
		    ct_streq(sessions[i].id, id, SESSION_ID_HEX_LEN) == 0) {
			sessions[i].expires_at = now + SESSION_TTL_MS; /* sliding refresh */
			rc = 0;
			break;
		}
	}
	k_mutex_unlock(&session_lock);
	return rc;
}

void session_destroy(const char *id)
{
	if (!id || strlen(id) != SESSION_ID_HEX_LEN) {
		return;
	}

	k_mutex_lock(&session_lock, K_FOREVER);
	for (int i = 0; i < SESSION_MAX; i++) {
		if (sessions[i].in_use &&
		    ct_streq(sessions[i].id, id, SESSION_ID_HEX_LEN) == 0) {
			memset(&sessions[i], 0, sizeof(sessions[i]));
			LOG_INF("session destroyed (slot %d)", i);
			break;
		}
	}
	k_mutex_unlock(&session_lock);
}
