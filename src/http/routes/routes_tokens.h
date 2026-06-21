/*
 * Copyright (c) 2025 The OpenJBOD Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TANK_HTTP_ROUTES_TOKENS_H_
#define TANK_HTTP_ROUTES_TOKENS_H_

#include <zephyr/net/http/server.h>

/* /api/tokens : GET (list), POST {label} (create), DELETE {id} (revoke). Auth-gated. */
extern struct http_resource_detail_dynamic tokens_resource_detail;

#endif /* TANK_HTTP_ROUTES_TOKENS_H_ */
