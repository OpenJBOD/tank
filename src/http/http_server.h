/*
 * HTTP server public API.
 */

#ifndef TANK_HTTP_SERVER_H_
#define TANK_HTTP_SERVER_H_

#include <zephyr/net/http/server.h>

int tank_http_server_init(void);
int tank_http_server_start(void);
int tank_http_server_stop(void);
void tank_http_server_schedule_restart(void);

#endif /* TANK_HTTP_SERVER_H_ */
