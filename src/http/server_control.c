#include "http/server_control.h"

#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>

#include "http/router.h"
#include "settings.h"

LOG_MODULE_REGISTER(tank_http_control, LOG_LEVEL_INF);

void restart_http_server(void)
{
	static bool first_call = true;
	const struct openjbod_settings *settings = openjbod_settings_get();

	if (!settings) {
		LOG_ERR("Restart request ignored: settings unavailable");
		return;
	}

	if (!first_call) {
		LOG_INF("Restarting HTTP server with new settings");
		http_server_stop();
		k_sleep(K_MSEC(100)); /* Allow sockets to close cleanly */
	}
	first_call = false;

	tank_http_router_configure(settings);

	if (settings->http.enable_http || settings->http.enable_https) {
		int ret = http_server_start();
		if (ret == 0) {
			LOG_INF("HTTP server restarted successfully");
		} else {
			LOG_ERR("Failed to restart HTTP server: %d", ret);
		}
	} else {
		LOG_INF("Both HTTP and HTTPS services disabled - server stopped");
	}
}
