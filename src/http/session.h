/*
 * Copyright (c) 2025 The OpenJBOD Project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * In-RAM web session store. A login creates a session keyed by a random 256-bit
 * id (the value of the HttpOnly session cookie); the gate validates the cookie
 * against this table. Sessions are revocable (logout) and reset on reboot.
 */

#ifndef TANK_HTTP_SESSION_H_
#define TANK_HTTP_SESSION_H_

#include <stddef.h>

/* 32 random bytes rendered as lowercase hex. */
#define SESSION_ID_BYTES   32
#define SESSION_ID_HEX_LEN (SESSION_ID_BYTES * 2)

/* Max concurrent sessions and idle lifetime (sliding - refreshed on use). */
#define SESSION_MAX        8
#define SESSION_TTL_MS     (24LL * 60 * 60 * 1000) /* 24 hours */

/**
 * Create a session for an authenticated user.
 * @param username  the verified username (stored for reference)
 * @param id_out    buffer receiving the hex session id (>= SESSION_ID_HEX_LEN + 1)
 * @param id_out_len length of id_out
 * @return 0 on success, negative errno on failure (no slot / RNG error).
 */
int session_create(const char *username, char *id_out, size_t id_out_len);

/**
 * Validate a session id. On success the session's idle expiry is refreshed.
 * @return 0 if the id maps to a live session, -EACCES otherwise.
 */
int session_validate(const char *id);

/** Destroy (revoke) a session by id. No-op if not found. */
void session_destroy(const char *id);

#endif /* TANK_HTTP_SESSION_H_ */
