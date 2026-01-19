#include "http/routes/routes_settings.h"

#include <errno.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static bool parse_json_bool(const char *value_start, bool *out)
{
	if (!value_start || !out) {
		return false;
	}

	while (isspace((unsigned char)*value_start)) {
		value_start++;
	}

	if (strncmp(value_start, "true", 4) == 0) {
		*out = true;
		return true;
	}

	if (strncmp(value_start, "false", 5) == 0) {
		*out = false;
		return true;
	}

	return false;
}

static char *parse_json_string_field(char *field_start, char *dest, size_t dest_size,
					 const char *field_name)
{
	if (!field_start) {
		return NULL;
	}

	while (*field_start == ' ' || *field_start == '\t') {
		field_start++;
	}

	if (*field_start != '"') {
		LOG_WRN("%s field found but no opening quote", field_name);
		return NULL;
	}

	field_start++;
	char *end = strchr(field_start, '"');
	if (!end) {
		LOG_WRN("%s field found but no closing quote", field_name);
		return NULL;
	}

	int len = end - field_start;
	if (len >= dest_size) {
		LOG_WRN("%s too long: %d >= %zu", field_name, len, dest_size);
		return NULL;
	}

	strncpy(dest, field_start, len);
	dest[len] = '\0';

	return dest;
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
		int auth_result = http_basic_auth_check(client);
		if (auth_result != 0) {
			LOG_WRN("Authentication failed for settings endpoint");
			http_send_auth_required_response(response_ctx);
			return 0;
		}

		struct openjbod_settings *current = openjbod_settings_get();

		snprintf(response_buffer, sizeof(response_buffer),
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
			 "\"fan_curve\":["
			 "{\"temperature\":%.1f,\"fan_percent\":%u},"
			 "{\"temperature\":%.1f,\"fan_percent\":%u},"
			 "{\"temperature\":%.1f,\"fan_percent\":%u},"
			 "{\"temperature\":%.1f,\"fan_percent\":%u},"
			 "{\"temperature\":%.1f,\"fan_percent\":%u}"
			 "]"
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
			 (double)current->environment.fan_curve[0].temperature,
			 current->environment.fan_curve[0].fan_percent,
			 (double)current->environment.fan_curve[1].temperature,
			 current->environment.fan_curve[1].fan_percent,
			 (double)current->environment.fan_curve[2].temperature,
			 current->environment.fan_curve[2].fan_percent,
			 (double)current->environment.fan_curve[3].temperature,
			 current->environment.fan_curve[3].fan_percent,
			 (double)current->environment.fan_curve[4].temperature,
			 current->environment.fan_curve[4].fan_percent);

		response_ctx->body = response_buffer;
		response_ctx->body_len = strlen(response_buffer);
		response_ctx->final_chunk = true;
	} else if (method == HTTP_POST) {
		if (status == HTTP_SERVER_DATA_FINAL) {
			int auth_result = http_basic_auth_check(client);
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
			bool network_changed = false;
			bool power_changed = false;
			bool http_changed = false;
			bool environment_changed = false;
			char *network_start = strstr(post_payload_buf, "\"network\":");

			if (network_start) {
				char *ip_method = strstr(network_start, "\"ip_method\":");
				if (ip_method) {
					ip_method += 12;
					while (*ip_method == ' ' || *ip_method == '\t') {
						ip_method++;
					}
					if (*ip_method == '"') {
						ip_method++;
						enum ip_method new_method;
						if (strncmp(ip_method, "dhcp", 4) == 0) {
							new_method = IP_METHOD_DHCP;
						} else if (strncmp(ip_method, "static", 6) == 0) {
							new_method = IP_METHOD_STATIC;
						} else {
							new_method = current->network.ip_method;
						}

						if (new_method != current->network.ip_method) {
							new_network.ip_method = new_method;
							network_changed = true;
							LOG_INF("ip_method changed to: %d", new_method);
						}
					}
				}

				char *ipv6_mode = strstr(network_start, "\"ipv6_mode\":");
				if (ipv6_mode) {
					char mode_str[16];
					if (parse_json_string_field(ipv6_mode + strlen("\"ipv6_mode\":"),
							 mode_str, sizeof(mode_str), "ipv6_mode")) {
						enum ipv6_mode new_mode = ipv6_mode_from_string(mode_str,
								current->network.ipv6_mode);
						if (new_mode != current->network.ipv6_mode) {
							new_network.ipv6_mode = new_mode;
							network_changed = true;
							LOG_INF("ipv6_mode changed to: %s", mode_str);
						}
					}
				}

				char *ip_addr = strstr(network_start, "\"ip_addr\":\"");
				if (ip_addr) {
					ip_addr += 11;
					char *end = strchr(ip_addr, '"');
					if (end && (size_t)(end - ip_addr) < sizeof(new_network.ip_addr)) {
						char temp_addr[sizeof(new_network.ip_addr)];
						strncpy(temp_addr, ip_addr, end - ip_addr);
						temp_addr[end - ip_addr] = '\0';

						if (strcmp(temp_addr, current->network.ip_addr) != 0) {
							strcpy(new_network.ip_addr, temp_addr);
							network_changed = true;
							LOG_INF("ip_addr changed to: %s", temp_addr);
						}
					}
				}

				char *gw_addr = strstr(network_start, "\"gw_addr\":\"");
				if (gw_addr) {
					gw_addr += 11;
					char *end = strchr(gw_addr, '"');
					if (end && (size_t)(end - gw_addr) < sizeof(new_network.gw_addr)) {
						char temp_addr[sizeof(new_network.gw_addr)];
						strncpy(temp_addr, gw_addr, end - gw_addr);
						temp_addr[end - gw_addr] = '\0';

						if (strcmp(temp_addr, current->network.gw_addr) != 0) {
							strcpy(new_network.gw_addr, temp_addr);
							network_changed = true;
							LOG_INF("gw_addr changed to: %s", temp_addr);
						}
					}
				}

				char *ip_mask = strstr(network_start, "\"ip_mask\":\"");
				if (ip_mask) {
					ip_mask += 11;
					char *end = strchr(ip_mask, '"');
					if (end && (size_t)(end - ip_mask) < sizeof(new_network.ip_mask)) {
						char temp_mask[sizeof(new_network.ip_mask)];
						strncpy(temp_mask, ip_mask, end - ip_mask);
						temp_mask[end - ip_mask] = '\0';

						if (strcmp(temp_mask, current->network.ip_mask) != 0) {
							strcpy(new_network.ip_mask, temp_mask);
							network_changed = true;
							LOG_INF("ip_mask changed to: %s", temp_mask);
						}
					}
				}

				char *dns1 = strstr(network_start, "\"dns1\":\"");
				if (dns1) {
					dns1 += strlen("\"dns1\":\"");
					char *end = strchr(dns1, '"');
					if (end && (size_t)(end - dns1) < sizeof(new_network.dns1)) {
						char temp_dns[sizeof(new_network.dns1)];
						strncpy(temp_dns, dns1, end - dns1);
						temp_dns[end - dns1] = '\0';

						if (strcmp(temp_dns, current->network.dns1) != 0) {
							strcpy(new_network.dns1, temp_dns);
							network_changed = true;
							LOG_INF("dns1 changed to: %s", temp_dns);
						}
					}
				}

				char *hostname_field = strstr(network_start, "\"hostname\":");
				if (hostname_field) {
					char temp_hostname[sizeof(new_network.hostname)];
					if (parse_json_string_field(hostname_field + 11, temp_hostname,
							      sizeof(temp_hostname), "hostname")) {
						if (strcmp(temp_hostname, current->network.hostname) != 0) {
							strcpy(new_network.hostname, temp_hostname);
							network_changed = true;
							LOG_INF("hostname changed to: %s", temp_hostname);
						}
					}
				}

				char *ipv6_addr = strstr(network_start, "\"ipv6_addr\":\"");
				if (ipv6_addr) {
					ipv6_addr += strlen("\"ipv6_addr\":\"");
					char *end = strchr(ipv6_addr, '"');
					if (end && (size_t)(end - ipv6_addr) < sizeof(new_network.ipv6_addr)) {
						char temp_addr[sizeof(new_network.ipv6_addr)];
						strncpy(temp_addr, ipv6_addr, end - ipv6_addr);
						temp_addr[end - ipv6_addr] = '\0';

						if (strcmp(temp_addr, current->network.ipv6_addr) != 0) {
							strcpy(new_network.ipv6_addr, temp_addr);
							network_changed = true;
							LOG_INF("ipv6_addr changed to: %s", temp_addr);
						}
					}
				}

				char *ipv6_prefix = strstr(network_start, "\"ipv6_prefix_length\":");
				if (ipv6_prefix) {
					ipv6_prefix += strlen("\"ipv6_prefix_length\":");
					unsigned long parsed = strtoul(ipv6_prefix, NULL, 10);
					if (parsed <= 128 &&
					    (uint8_t)parsed != current->network.ipv6_prefix_length) {
						new_network.ipv6_prefix_length = (uint8_t)parsed;
						network_changed = true;
						LOG_INF("ipv6_prefix_length changed to: %lu", parsed);
					}
				}

				char *ipv6_gateway = strstr(network_start, "\"ipv6_gateway\":\"");
				if (ipv6_gateway) {
					ipv6_gateway += strlen("\"ipv6_gateway\":\"");
					char *end = strchr(ipv6_gateway, '"');
					if (end && (size_t)(end - ipv6_gateway) < sizeof(new_network.ipv6_gateway)) {
						char temp_gw[sizeof(new_network.ipv6_gateway)];
						strncpy(temp_gw, ipv6_gateway, end - ipv6_gateway);
						temp_gw[end - ipv6_gateway] = '\0';

						if (strcmp(temp_gw, current->network.ipv6_gateway) != 0) {
							strcpy(new_network.ipv6_gateway, temp_gw);
							network_changed = true;
							LOG_INF("ipv6_gateway changed to: %s", temp_gw);
						}
					}
				}

				char *ipv6_dns = strstr(network_start, "\"ipv6_dns1\":\"");
				if (ipv6_dns) {
					ipv6_dns += strlen("\"ipv6_dns1\":\"");
					char *end = strchr(ipv6_dns, '"');
					if (end && (size_t)(end - ipv6_dns) < sizeof(new_network.ipv6_dns1)) {
						char temp_dns[sizeof(new_network.ipv6_dns1)];
						strncpy(temp_dns, ipv6_dns, end - ipv6_dns);
						temp_dns[end - ipv6_dns] = '\0';

						if (strcmp(temp_dns, current->network.ipv6_dns1) != 0) {
							strcpy(new_network.ipv6_dns1, temp_dns);
							network_changed = true;
							LOG_INF("ipv6_dns1 changed to: %s", temp_dns);
						}
					}
				}
			}

			char *power_start = strstr(post_payload_buf, "\"power\":");
			if (power_start) {
				char *ignore_switch = strstr(power_start, "\"ignore_power_switch\":");
				if (ignore_switch) {
					ignore_switch += strlen("\"ignore_power_switch\":");
					bool new_ignore = current->power.ignore_power_switch;
					if (parse_json_bool(ignore_switch, &new_ignore) &&
					    new_ignore != current->power.ignore_power_switch) {
						new_power.ignore_power_switch = new_ignore;
						power_changed = true;
						LOG_INF("ignore_power_switch changed to: %d", new_ignore);
					}
				}

				char *on_boot = strstr(power_start, "\"on_boot\":");
				if (on_boot) {
					on_boot += strlen("\"on_boot\":");
					bool new_on_boot = current->power.on_boot;
					if (parse_json_bool(on_boot, &new_on_boot) &&
					    new_on_boot != current->power.on_boot) {
						new_power.on_boot = new_on_boot;
						power_changed = true;
						LOG_INF("on_boot changed to: %d", new_on_boot);
					}
				}

				char *on_boot_delay = strstr(power_start, "\"on_boot_delay\":");
				if (on_boot_delay) {
					on_boot_delay += 16;
					uint32_t new_delay = (uint32_t)atoi(on_boot_delay);
					if (new_delay != current->power.on_boot_delay) {
						new_power.on_boot_delay = new_delay;
						power_changed = true;
						LOG_INF("on_boot_delay changed to: %u", new_delay);
					}
				}

				char *follow_usb = strstr(power_start, "\"follow_usb\":");
				if (follow_usb) {
					follow_usb += strlen("\"follow_usb\":");
					bool new_follow_usb = current->power.follow_usb;
					if (parse_json_bool(follow_usb, &new_follow_usb) &&
					    new_follow_usb != current->power.follow_usb) {
						new_power.follow_usb = new_follow_usb;
						power_changed = true;
						LOG_INF("follow_usb changed to: %d", new_follow_usb);
					}
				}

				char *follow_usb_delay = strstr(power_start, "\"follow_usb_delay\":");
				if (follow_usb_delay) {
					follow_usb_delay += 19;
					uint32_t new_delay = (uint32_t)atoi(follow_usb_delay);
					if (new_delay != current->power.follow_usb_delay) {
						new_power.follow_usb_delay = new_delay;
						power_changed = true;
						LOG_INF("follow_usb_delay changed to: %u", new_delay);
					}
				}
			}

			char *http_start = strstr(post_payload_buf, "\"http\":");
			if (http_start) {
				char *enable_http = strstr(http_start, "\"enable_http\":");
				if (enable_http) {
					enable_http += strlen("\"enable_http\":");
					bool new_enable_http = current->http.enable_http;
					if (parse_json_bool(enable_http, &new_enable_http) &&
					    new_enable_http != current->http.enable_http) {
						new_http.enable_http = new_enable_http;
						http_changed = true;
						LOG_INF("enable_http changed to: %d", new_enable_http);
					}
				}

				char *enable_https = strstr(http_start, "\"enable_https\":");
				if (enable_https) {
					enable_https += strlen("\"enable_https\":");
					bool new_enable_https = current->http.enable_https;
					if (parse_json_bool(enable_https, &new_enable_https) &&
					    new_enable_https != current->http.enable_https) {
						new_http.enable_https = new_enable_https;
						http_changed = true;
						LOG_INF("enable_https changed to: %d", new_enable_https);
					}
				}

				char *http_port = strstr(http_start, "\"http_port\":");
				if (http_port) {
					http_port += 12;
					uint16_t new_http_port = (uint16_t)atoi(http_port);
					if (new_http_port != current->http.http_port) {
						new_http.http_port = new_http_port;
						http_changed = true;
						LOG_INF("http_port changed to: %u", new_http_port);
					}
				}

				char *https_port = strstr(http_start, "\"https_port\":");
				if (https_port) {
					https_port += 13;
					uint16_t new_https_port = (uint16_t)atoi(https_port);
					if (new_https_port != current->http.https_port) {
						new_http.https_port = new_https_port;
						http_changed = true;
						LOG_INF("https_port changed to: %u", new_https_port);
					}
				}

				char *custom_certs = strstr(http_start, "\"use_custom_certificates\":");
				if (custom_certs) {
					custom_certs += strlen("\"use_custom_certificates\":");
					bool new_use_custom = current->http.use_custom_certificates;
					if (parse_json_bool(custom_certs, &new_use_custom) &&
					    new_use_custom != current->http.use_custom_certificates) {
						new_http.use_custom_certificates = new_use_custom;
						http_changed = true;
						LOG_INF("use_custom_certificates changed to: %d", new_use_custom);
					}
				}
			}

			char *environment_start = strstr(post_payload_buf, "\"environment\":");
			if (environment_start) {
				char *external_control = strstr(environment_start,
							      "\"use_external_fan_control\":");
				if (external_control) {
					external_control += strlen("\"use_external_fan_control\":");
					bool new_external = current->environment.use_external_fan_control;
					if (parse_json_bool(external_control, &new_external) &&
					    new_external != current->environment.use_external_fan_control) {
						new_environment.use_external_fan_control = new_external;
						environment_changed = true;
						LOG_INF("use_external_fan_control changed to: %d", new_external);
					}
				}

				char *update_interval = strstr(environment_start,
							      "\"fan_update_interval_ms\":");
				if (update_interval) {
					update_interval += strlen("\"fan_update_interval_ms\":");
					uint32_t new_interval = (uint32_t)atoi(update_interval);
					if (new_interval != current->environment.fan_update_interval_ms) {
						new_environment.fan_update_interval_ms = new_interval;
						environment_changed = true;
						LOG_INF("fan_update_interval_ms changed to: %u", new_interval);
					}
				}

				char *hysteresis = strstr(environment_start,
						      "\"fan_hysteresis_percent\":");
				if (hysteresis) {
					hysteresis += strlen("\"fan_hysteresis_percent\":");
					uint8_t new_hysteresis = (uint8_t)atoi(hysteresis);
					if (new_hysteresis != current->environment.fan_hysteresis_percent) {
						new_environment.fan_hysteresis_percent = new_hysteresis;
						environment_changed = true;
						LOG_INF("fan_hysteresis_percent changed to: %u", new_hysteresis);
					}
				}

				char *fan_curve = strstr(environment_start, "\"fan_curve\":");
				if (fan_curve) {
					char *fan_curve_array = strchr(fan_curve, '[');
					if (fan_curve_array) {
						char *curve_cursor = fan_curve_array;
						for (int i = 0; i < ARRAY_SIZE(current->environment.fan_curve); i++) {
							char *curve_obj = strchr(curve_cursor, '{');
							if (!curve_obj) {
								break;
							}

							char *curve_end = strchr(curve_obj, '}');
							if (!curve_end) {
								break;
							}

							char *temp_field = strstr(curve_obj, "\"temperature\":");
							if (temp_field && temp_field < curve_end) {
								temp_field += strlen("\"temperature\":");
								while (*temp_field == ' ' || *temp_field == '\t') {
									temp_field++;
								}
								float new_temp = strtof(temp_field, NULL);
								if (new_temp != current->environment.fan_curve[i].temperature) {
									new_environment.fan_curve[i].temperature = new_temp;
									environment_changed = true;
									LOG_INF("fan_curve[%d].temperature changed to: %.1f", i, (double)new_temp);
								}
							}

							char *percent_field = strstr(curve_obj, "\"fan_percent\":");
							if (percent_field && percent_field < curve_end) {
								percent_field += strlen("\"fan_percent\":");
								while (*percent_field == ' ' || *percent_field == '\t') {
									percent_field++;
								}
								uint8_t new_percent = (uint8_t)atoi(percent_field);
								if (new_percent != current->environment.fan_curve[i].fan_percent &&
								    new_percent <= 100) {
									new_environment.fan_curve[i].fan_percent = new_percent;
									environment_changed = true;
									LOG_INF("fan_curve[%d].fan_percent changed to: %u", i, new_percent);
								}
							}

							curve_cursor = curve_end + 1;
						}
					}
				}
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

			if (ret == 0) {
				snprintf(response_buffer, sizeof(response_buffer),
					 "{\"status\":\"settings_updated\",\"result\":\"success\"}");
			} else {
				snprintf(response_buffer, sizeof(response_buffer),
					 "{\"status\":\"settings_error\",\"error\":\"Failed to save settings\"}");
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
