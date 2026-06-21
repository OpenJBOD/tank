/*
 * Copyright (c) 2025 The OpenJBOD Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TANK_HTTP_ROUTES_AUTH_H_
#define TANK_HTTP_ROUTES_AUTH_H_

#include <zephyr/net/http/server.h>

/* POST /api/login  {"username","password"} -> Set-Cookie session (public). */
extern struct http_resource_detail_dynamic login_resource_detail;

/* POST /api/logout -> clears the session + cookie (public; no-op if not logged in). */
extern struct http_resource_detail_dynamic logout_resource_detail;

#endif /* TANK_HTTP_ROUTES_AUTH_H_ */
