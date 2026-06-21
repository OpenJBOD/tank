#include "http/routes/routes_settings.h"

#include <errno.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/data/json.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/sys/util.h>

#include "http/auth.h"
#include "http/server_control.h"
#include "settings.h"

LOG_MODULE_REGISTER(tank_http_settings, LOG_LEVEL_INF);

static const char *ipv6_mode_to_string(enum ipv6_mode mode)
{
	switch (mode) {
	case IPV6_MODE_DISABLED:
		return "disabled";
	case IPV6_MODE_SLAAC:
		return "slaac";
	case IPV6_MODE_DHCPV6:
		return "dhcpv6";
	case IPV6_MODE_STATIC:
		return "static";
	default:
		return "unknown";
	}
}

static enum ipv6_mode ipv6_mode_from_string(const char *value, enum ipv6_mode fallback)
{
	if (!value) {
		return fallback;
	}

	if (strcmp(value, "disabled") == 0) {
		return IPV6_MODE_DISABLED;
	}

	if (strcmp(value, "slaac") == 0) {
		return IPV6_MODE_SLAAC;
	}

	if (strcmp(value, "dhcpv6") == 0) {
		return IPV6_MODE_DHCPV6;
	}

	if (strcmp(value, "static") == 0) {
		return IPV6_MODE_STATIC;
	}

	return fallback;
}

/* ---- JSON-library parsing of the settings POST body ----------------------
 * The body is {"network":{...},"power":{...},"http":{...},"environment":{...},
 * "console":{...}} and each web-UI page posts one sub-object. json_obj_parse()
 * skips keys it doesn't know and only writes fields it finds, so we pre-seed the
 * parse target from the current settings: any field a page omits (e.g. the
 * environment page predates primary_temp_source) keeps its current value.
 * Strings arrive as pointers into the (mutated) payload buffer, numbers widen to
 * int32, booleans to bool, the fan-curve temperature to float, and the ip_method
 * / ipv6_mode enums arrive as strings.
 */
struct curve_json {
	float temperature;
	int32_t fan_percent;
};

struct net_json {
	char *hostname, *ip_method, *ip_addr, *ip_mask, *gw_addr, *dns1;
	char *ipv6_mode, *ipv6_addr, *ipv6_gateway, *ipv6_dns1;
	int32_t ipv6_prefix_length;
};

struct power_json {
	bool ignore_power_switch, on_boot, follow_usb;
	int32_t on_boot_delay, follow_usb_delay;
};

struct http_json {
	bool enable_http, enable_https, use_custom_certificates;
	int32_t http_port, https_port;
};

struct env_json {
	bool use_external_fan_control;
	int32_t fan_update_interval_ms, fan_hysteresis_percent, primary_temp_source;
	struct curve_json fan_curve[5];
	size_t fan_curve_count;
};

struct console_json {
	bool uart_enabled, usb_enabled;
};

struct settings_json {
	struct net_json network;
	struct power_json power;
	struct http_json http;
	struct env_json environment;
	struct console_json console;
};

static const struct json_obj_descr curve_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct curve_json, temperature, JSON_TOK_FLOAT_FP),
	JSON_OBJ_DESCR_PRIM(struct curve_json, fan_percent, JSON_TOK_NUMBER),
};

static const struct json_obj_descr net_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct net_json, hostname, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct net_json, ip_method, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct net_json, ip_addr, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct net_json, ip_mask, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct net_json, gw_addr, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct net_json, dns1, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct net_json, ipv6_mode, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct net_json, ipv6_addr, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct net_json, ipv6_gateway, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct net_json, ipv6_dns1, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct net_json, ipv6_prefix_length, JSON_TOK_NUMBER),
};

static const struct json_obj_descr power_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct power_json, ignore_power_switch, JSON_TOK_TRUE),
	JSON_OBJ_DESCR_PRIM(struct power_json, on_boot, JSON_TOK_TRUE),
	JSON_OBJ_DESCR_PRIM(struct power_json, on_boot_delay, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct power_json, follow_usb, JSON_TOK_TRUE),
	JSON_OBJ_DESCR_PRIM(struct power_json, follow_usb_delay, JSON_TOK_NUMBER),
};

static const struct json_obj_descr http_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct http_json, enable_http, JSON_TOK_TRUE),
	JSON_OBJ_DESCR_PRIM(struct http_json, enable_https, JSON_TOK_TRUE),
	JSON_OBJ_DESCR_PRIM(struct http_json, http_port, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct http_json, https_port, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct http_json, use_custom_certificates, JSON_TOK_TRUE),
};

static const struct json_obj_descr env_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct env_json, use_external_fan_control, JSON_TOK_TRUE),
	JSON_OBJ_DESCR_PRIM(struct env_json, fan_update_interval_ms, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct env_json, fan_hysteresis_percent, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct env_json, primary_temp_source, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_OBJ_ARRAY(struct env_json, fan_curve, 5, fan_curve_count,
				 curve_descr, ARRAY_SIZE(curve_descr)),
};

static const struct json_obj_descr console_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct console_json, uart_enabled, JSON_TOK_TRUE),
	JSON_OBJ_DESCR_PRIM(struct console_json, usb_enabled, JSON_TOK_TRUE),
};

static const struct json_obj_descr settings_descr[] = {
	JSON_OBJ_DESCR_OBJECT(struct settings_json, network, net_descr),
	JSON_OBJ_DESCR_OBJECT(struct settings_json, power, power_descr),
	JSON_OBJ_DESCR_OBJECT(struct settings_json, http, http_descr),
	JSON_OBJ_DESCR_OBJECT(struct settings_json, environment, env_descr),
	JSON_OBJ_DESCR_OBJECT(struct settings_json, console, console_descr),
};

/* Top-level presence bits returned by json_obj_parse() (descriptor order). */
#define SJ_NETWORK     BIT(0)
#define SJ_POWER       BIT(1)
#define SJ_HTTP        BIT(2)
#define SJ_ENVIRONMENT BIT(3)
#define SJ_CONSOLE     BIT(4)

/* Copy a parsed string into a fixed settings field only if it was present. */
static void apply_str(char *dst, size_t cap, const char *src)
{
	if (src != NULL) {
		strncpy(dst, src, cap - 1);
		dst[cap - 1] = '\0';
	}
}

static int settings_handler(struct http_client_ctx *client, enum http_data_status status,
			    const struct http_request_ctx *request_ctx,
			    struct http_response_ctx *response_ctx, void *user_data)
{
	static char response_buffer[2048];
	static char post_payload_buf[512];
	static size_t cursor;
	enum http_method method = client->method;

	ARG_UNUSED(user_data);

	LOG_DBG("Settings handler status %d, method %d", status, method);

	if (method == HTTP_GET && status == HTTP_SERVER_DATA_FINAL) {
		int auth_result = http_check_auth(client);
		if (auth_result != 0) {
			LOG_WRN("Authentication failed for settings endpoint");
			http_send_auth_required_response(response_ctx);
			return 0;
		}

		struct openjbod_settings *current = openjbod_settings_get();

		int written = snprintf(response_buffer, sizeof(response_buffer),
			 "{"
			 "\"network\":{"
			 "\"ip_method\":\"%s\"," 
			 "\"ip_addr\":\"%s\"," 
			 "\"gw_addr\":\"%s\"," 
			 "\"ip_mask\":\"%s\"," 
			 "\"dns1\":\"%s\"," 
			 "\"hostname\":\"%s\"," 
			 "\"ipv6_mode\":\"%s\"," 
			 "\"ipv6_addr\":\"%s\"," 
			 "\"ipv6_prefix_length\":%u," 
			 "\"ipv6_gateway\":\"%s\"," 
			 "\"ipv6_dns1\":\"%s\""
			 "},"
			 "\"power\":{"
			 "\"ignore_power_switch\":%s,"
			 "\"on_boot\":%s,"
			 "\"on_boot_delay\":%u,"
			 "\"follow_usb\":%s,"
			 "\"follow_usb_delay\":%u"
			 "},"
			 "\"http\":{"
			 "\"enable_http\":%s,"
			 "\"enable_https\":%s,"
			 "\"http_port\":%u,"
			 "\"https_port\":%u,"
			 "\"use_custom_certificates\":%s"
			 "},"
			 "\"environment\":{"
			 "\"use_external_fan_control\":%s,"
			 "\"fan_update_interval_ms\":%u,"
			 "\"fan_hysteresis_percent\":%u,"
			 "\"primary_temp_source\":%u,"
			 "\"fan_curve\":["
			 "{\"temperature\":%.1f,\"fan_percent\":%u},"
			 "{\"temperature\":%.1f,\"fan_percent\":%u},"
			 "{\"temperature\":%.1f,\"fan_percent\":%u},"
			 "{\"temperature\":%.1f,\"fan_percent\":%u},"
			 "{\"temperature\":%.1f,\"fan_percent\":%u}"
			 "]"
			 "},"
			 "\"console\":{"
			 "\"uart_enabled\":%s,"
			 "\"usb_enabled\":%s"
			 "}"
			 "}",
			 current->network.ip_method == IP_METHOD_DHCP ? "dhcp" : "static",
			 current->network.ip_addr,
			 current->network.gw_addr,
			 current->network.ip_mask,
			 current->network.dns1,
			 current->network.hostname,
			 ipv6_mode_to_string(current->network.ipv6_mode),
			 current->network.ipv6_addr,
			 current->network.ipv6_prefix_length,
			 current->network.ipv6_gateway,
			 current->network.ipv6_dns1,
			 current->power.ignore_power_switch ? "true" : "false",
			 current->power.on_boot ? "true" : "false",
			 current->power.on_boot_delay,
			 current->power.follow_usb ? "true" : "false",
			 current->power.follow_usb_delay,
			 current->http.enable_http ? "true" : "false",
			 current->http.enable_https ? "true" : "false",
			 current->http.http_port,
			 current->http.https_port,
			 current->http.use_custom_certificates ? "true" : "false",
			 current->environment.use_external_fan_control ? "true" : "false",
			 current->environment.fan_update_interval_ms,
			 current->environment.fan_hysteresis_percent,
			 current->environment.primary_temp_source,
			 (double)current->environment.fan_curve[0].temperature,
			 current->environment.fan_curve[0].fan_percent,
			 (double)current->environment.fan_curve[1].temperature,
			 current->environment.fan_curve[1].fan_percent,
			 (double)current->environment.fan_curve[2].temperature,
			 current->environment.fan_curve[2].fan_percent,
			 (double)current->environment.fan_curve[3].temperature,
			 current->environment.fan_curve[3].fan_percent,
			 (double)current->environment.fan_curve[4].temperature,
			 current->environment.fan_curve[4].fan_percent,
			 current->console.uart_enabled ? "true" : "false",
			 current->console.usb_enabled ? "true" : "false");

		if (written < 0 || written >= (int)sizeof(response_buffer)) {
			LOG_ERR("Settings JSON truncated (%d/%zu bytes)", written, sizeof(response_buffer));
			written = snprintf(response_buffer, sizeof(response_buffer),
				 "{\"status\":\"error\",\"message\":\"settings response too large\"}");
			response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
		}

		response_ctx->body = response_buffer;
		response_ctx->body_len = strlen(response_buffer);
		response_ctx->final_chunk = true;
	} else if (method == HTTP_POST) {
		if (status == HTTP_SERVER_DATA_FINAL) {
			int auth_result = http_check_auth(client);
			if (auth_result != 0) {
				LOG_WRN("Authentication failed for settings endpoint");
				http_send_auth_required_response(response_ctx);
				cursor = 0;
				return 0;
			}
		}

		if (request_ctx->data_len + cursor > sizeof(post_payload_buf) - 1) {
			cursor = 0;
			return -ENOMEM;
		}

		memcpy(post_payload_buf + cursor, request_ctx->data, request_ctx->data_len);
		cursor += request_ctx->data_len;

		if (status == HTTP_SERVER_DATA_FINAL) {
			post_payload_buf[cursor] = '\0';

			struct openjbod_settings *current = openjbod_settings_get();
			struct network_settings new_network = current->network;
			struct power_settings new_power = current->power;
			struct http_settings new_http = current->http;
			struct environment_settings new_environment = current->environment;
			struct console_settings new_console = current->console;
			bool network_changed = false;
			bool power_changed = false;
			bool http_changed = false;
			bool environment_changed = false;
			bool console_changed = false;

			/* Pre-seed the parse target from current settings so any field a
			 * page omits keeps its current value (strings stay NULL = keep).
			 */
			struct settings_json sj;

			memset(&sj, 0, sizeof(sj));
			sj.network.ipv6_prefix_length = current->network.ipv6_prefix_length;
			sj.power.ignore_power_switch = current->power.ignore_power_switch;
			sj.power.on_boot = current->power.on_boot;
			sj.power.on_boot_delay = current->power.on_boot_delay;
			sj.power.follow_usb = current->power.follow_usb;
			sj.power.follow_usb_delay = current->power.follow_usb_delay;
			sj.http.enable_http = current->http.enable_http;
			sj.http.enable_https = current->http.enable_https;
			sj.http.http_port = current->http.http_port;
			sj.http.https_port = current->http.https_port;
			sj.http.use_custom_certificates = current->http.use_custom_certificates;
			sj.environment.use_external_fan_control = current->environment.use_external_fan_control;
			sj.environment.fan_update_interval_ms = current->environment.fan_update_interval_ms;
			sj.environment.fan_hysteresis_percent = current->environment.fan_hysteresis_percent;
			sj.environment.primary_temp_source = current->environment.primary_temp_source;
			for (int i = 0; i < 5; i++) {
				sj.environment.fan_curve[i].temperature =
					current->environment.fan_curve[i].temperature;
				sj.environment.fan_curve[i].fan_percent =
					current->environment.fan_curve[i].fan_percent;
			}
			sj.console.uart_enabled = current->console.uart_enabled;
			sj.console.usb_enabled = current->console.usb_enabled;

			int64_t parsed = json_obj_parse(post_payload_buf, strlen(post_payload_buf),
							settings_descr, ARRAY_SIZE(settings_descr), &sj);
			if (parsed < 0) {
				LOG_WRN("Failed to parse settings JSON: %lld", parsed);
				snprintf(response_buffer, sizeof(response_buffer),
					 "{\"status\":\"settings_error\",\"error\":\"Invalid JSON body\"}");
				response_ctx->status = HTTP_400_BAD_REQUEST;
				response_ctx->body = response_buffer;
				response_ctx->body_len = strlen(response_buffer);
				response_ctx->final_chunk = true;
				cursor = 0;
				return 0;
			}

			if (parsed & SJ_NETWORK) {
				network_changed = true;
				if (sj.network.ip_method) {
					new_network.ip_method =
						(strcmp(sj.network.ip_method, "static") == 0)
							? IP_METHOD_STATIC : IP_METHOD_DHCP;
				}
				apply_str(new_network.hostname, sizeof(new_network.hostname), sj.network.hostname);
				apply_str(new_network.ip_addr, sizeof(new_network.ip_addr), sj.network.ip_addr);
				apply_str(new_network.gw_addr, sizeof(new_network.gw_addr), sj.network.gw_addr);
				apply_str(new_network.ip_mask, sizeof(new_network.ip_mask), sj.network.ip_mask);
				apply_str(new_network.dns1, sizeof(new_network.dns1), sj.network.dns1);
				if (sj.network.ipv6_mode) {
					new_network.ipv6_mode = ipv6_mode_from_string(
						sj.network.ipv6_mode, current->network.ipv6_mode);
				}
				apply_str(new_network.ipv6_addr, sizeof(new_network.ipv6_addr), sj.network.ipv6_addr);
				new_network.ipv6_prefix_length = (uint8_t)sj.network.ipv6_prefix_length;
				apply_str(new_network.ipv6_gateway, sizeof(new_network.ipv6_gateway), sj.network.ipv6_gateway);
				apply_str(new_network.ipv6_dns1, sizeof(new_network.ipv6_dns1), sj.network.ipv6_dns1);
			}
			if (parsed & SJ_POWER) {
				power_changed = true;
				new_power.ignore_power_switch = sj.power.ignore_power_switch;
				new_power.on_boot = sj.power.on_boot;
				new_power.on_boot_delay = (uint32_t)sj.power.on_boot_delay;
				new_power.follow_usb = sj.power.follow_usb;
				new_power.follow_usb_delay = (uint32_t)sj.power.follow_usb_delay;
			}
			if (parsed & SJ_HTTP) {
				http_changed = true;
				new_http.enable_http = sj.http.enable_http;
				new_http.enable_https = sj.http.enable_https;
				new_http.http_port = (uint16_t)sj.http.http_port;
				new_http.https_port = (uint16_t)sj.http.https_port;
				new_http.use_custom_certificates = sj.http.use_custom_certificates;
			}
			if (parsed & SJ_ENVIRONMENT) {
				environment_changed = true;
				new_environment.use_external_fan_control = sj.environment.use_external_fan_control;
				new_environment.fan_update_interval_ms = (uint32_t)sj.environment.fan_update_interval_ms;
				new_environment.fan_hysteresis_percent = (uint8_t)sj.environment.fan_hysteresis_percent;
				new_environment.primary_temp_source = (uint8_t)sj.environment.primary_temp_source;
				for (int i = 0; i < 5; i++) {
					new_environment.fan_curve[i].temperature =
						sj.environment.fan_curve[i].temperature;
					new_environment.fan_curve[i].fan_percent =
						(uint8_t)sj.environment.fan_curve[i].fan_percent;
				}
			}
			if (parsed & SJ_CONSOLE) {
				console_changed = true;
				new_console.uart_enabled = sj.console.uart_enabled;
				new_console.usb_enabled = sj.console.usb_enabled;
			}

			int ret = 0;
			if (network_changed) {
				ret = openjbod_settings_set_network(&new_network);
				if (ret != 0) {
					LOG_ERR("Failed to save network settings: %d", ret);
				}
			}
			if (power_changed && ret == 0) {
				ret = openjbod_settings_set_power(&new_power);
				if (ret != 0) {
					LOG_ERR("Failed to save power settings: %d", ret);
				}
			}
			if (http_changed && ret == 0) {
				ret = openjbod_settings_set_http(&new_http);
				if (ret != 0) {
					LOG_ERR("Failed to save HTTP settings: %d", ret);
				} else {
					LOG_INF("HTTP settings changed - restarting server");
					restart_http_server();
				}
			}
			if (environment_changed && ret == 0) {
				ret = openjbod_settings_set_environment(&new_environment);
				if (ret != 0) {
					LOG_ERR("Failed to save environment settings: %d", ret);
				} else {
					LOG_INF("Environment settings updated successfully");
				}
			}
			if (console_changed && ret == 0) {
				ret = openjbod_settings_set_console(&new_console);
				if (ret != 0) {
					LOG_ERR("Failed to save console settings: %d", ret);
				} else {
					LOG_INF("Console settings updated (reboot to apply)");
				}
			}

			if (ret == 0) {
				snprintf(response_buffer, sizeof(response_buffer),
					 "{\"status\":\"settings_updated\",\"result\":\"success\"}");
			} else {
				snprintf(response_buffer, sizeof(response_buffer),
					 "{\"status\":\"settings_error\",\"error\":\"Failed to save settings\"}");
				response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
			}

			cursor = 0;
			response_ctx->body = response_buffer;
			response_ctx->body_len = strlen(response_buffer);
			response_ctx->final_chunk = true;
		}
	}

	if (status == HTTP_SERVER_DATA_FINAL && method != HTTP_GET && method != HTTP_POST) {
		response_ctx->status = HTTP_405_METHOD_NOT_ALLOWED;
		response_ctx->final_chunk = true;
	}

	return 0;
}

struct http_resource_detail_dynamic settings_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET) | BIT(HTTP_POST),
		.content_type = "application/json",
	},
	.cb = settings_handler,
	.user_data = NULL,
};
