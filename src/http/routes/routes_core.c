#include "http/routes/routes_core.h"

#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/data/json.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/util_macro.h>

#include <zephyr/logging/log.h>

#include "device_info.h"
#include "emc2301.h"
#include "http/auth.h"
#include "settings.h"
#include "sr_latch.h"
#include "temperature.h"

LOG_MODULE_REGISTER(tank_http_core, LOG_LEVEL_INF);

#define DEVICE_INFO_JSON_BUF_SIZE 512
#define STATUS_JSON_BUF_SIZE 1024

struct led_command {
	int led_num;
	bool led_state;
};

static const struct json_obj_descr led_command_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct led_command, led_num, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct led_command, led_state, JSON_TOK_TRUE),
};

static const struct device *leds_dev = DEVICE_DT_GET_ANY(gpio_leds);

static void copy_with_fallback(char *dest, size_t dest_len, const char *fallback)
{
	if (dest_len == 0) {
		return;
	}

	int written = snprintf(dest, dest_len, "%s", fallback ? fallback : "");
	if (written < 0 || written >= (int)dest_len) {
		dest[dest_len - 1] = '\0';
	}
}

static void fetch_device_identity(char *serial_buf,
	size_t serial_len,
	char *board_rev_buf,
	size_t board_rev_len)
{
	int ret = openjbod_device_info_get_serial(serial_buf, serial_len);
	if (ret < 0) {
		copy_with_fallback(serial_buf, serial_len, "Unknown");
	}

	ret = openjbod_device_info_get_board_revision(board_rev_buf, board_rev_len);
	if (ret < 0) {
		copy_with_fallback(board_rev_buf, board_rev_len, "Unknown");
	}
}

static void format_mac_address(struct net_if *iface, char *mac_buf, size_t mac_len)
{
	if (mac_len == 0) {
		return;
	}

	if (!iface) {
		copy_with_fallback(mac_buf, mac_len, "Unknown");
		return;
	}

	const struct net_linkaddr *link_addr = net_if_get_link_addr((struct net_if *)iface);
	if (!link_addr || link_addr->len < 6) {
		copy_with_fallback(mac_buf, mac_len, "Unknown");
		return;
	}

	int written = snprintf(mac_buf, mac_len,
		"%02X:%02X:%02X:%02X:%02X:%02X",
		link_addr->addr[0], link_addr->addr[1],
		link_addr->addr[2], link_addr->addr[3],
		link_addr->addr[4], link_addr->addr[5]);
	if (written < 0 || written >= (int)mac_len) {
		mac_buf[mac_len - 1] = '\0';
	}
}

static void describe_ipv4_network(struct net_if *iface,
	char *ip_buf,
	size_t ip_len,
	char *subnet_buf,
	size_t subnet_len,
	char *gateway_buf,
	size_t gateway_len)
{
	if (ip_len > 0) {
		copy_with_fallback(ip_buf, ip_len, "Unknown");
	}

	if (subnet_len > 0) {
		copy_with_fallback(subnet_buf, subnet_len, "Unknown");
	}

	if (gateway_len > 0) {
		copy_with_fallback(gateway_buf, gateway_len, "Unknown");
	}

	if (!iface || !iface->config.ip.ipv4) {
		return;
	}

	struct net_if_ipv4 *ipv4 = iface->config.ip.ipv4;
	for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		if (ipv4->unicast[i].ipv4.addr_state == NET_ADDR_PREFERRED) {
			net_addr_ntop(AF_INET,
				      &ipv4->unicast[i].ipv4.address.in_addr,
				      ip_buf, ip_len);
			net_addr_ntop(AF_INET,
				      &ipv4->unicast[i].netmask,
				      subnet_buf, subnet_len);
			break;
		}
	}

	if (ipv4->gw.s_addr != 0U) {
		net_addr_ntop(AF_INET, &ipv4->gw, gateway_buf, gateway_len);
	}
}

static void build_ipv6_address_json(struct net_if *iface,
	char *buffer,
	size_t buffer_len)
{
	if (buffer_len == 0) {
		return;
	}

	buffer[0] = '\0';

#if defined(CONFIG_NET_IPV6)
	if (!iface || !iface->config.ip.ipv6) {
		copy_with_fallback(buffer, buffer_len, "[]");
		return;
	}

	size_t offset = 0;
	int written = snprintf(buffer, buffer_len, "[");
	if (written < 0 || (size_t)written >= buffer_len) {
		copy_with_fallback(buffer, buffer_len, "[]");
		return;
	}

	offset = (size_t)written;
	bool first_entry = true;

	struct net_if_ipv6 *ipv6 = iface->config.ip.ipv6;
	for (int i = 0; i < NET_IF_MAX_IPV6_ADDR; i++) {
		struct net_if_addr *ifaddr = &ipv6->unicast[i];

		if (!ifaddr->is_used || ifaddr->address.family != AF_INET6) {
			continue;
		}

		char addr_buf[INET6_ADDRSTRLEN];
		if (!net_addr_ntop(AF_INET6, &ifaddr->address.in6_addr,
			       addr_buf, sizeof(addr_buf))) {
			continue;
		}

		uint8_t prefix_len = 128U;
#if defined(CONFIG_NET_NATIVE_IPV6)
		struct net_if_ipv6_prefix *prefix =
			net_if_ipv6_prefix_get(iface, &ifaddr->address.in6_addr);
		if (prefix) {
			prefix_len = prefix->len;
		}
#endif

		char entry_buf[INET6_ADDRSTRLEN + 8];
		int entry_len = snprintf(entry_buf, sizeof(entry_buf), "%s/%u",
			addr_buf, prefix_len);
		if (entry_len < 0 || entry_len >= (int)sizeof(entry_buf)) {
			continue;
		}

		const char *separator = first_entry ? "" : ",";
		first_entry = false;

		if (offset < buffer_len) {
			written = snprintf(buffer + offset, buffer_len - offset,
				"%s\"%s\"", separator, entry_buf);
			if (written < 0 || (size_t)written >= buffer_len - offset) {
				buffer[buffer_len - 1] = '\0';
				offset = buffer_len - 1;
				break;
			}
			offset += (size_t)written;
		}
	}

	if (offset < buffer_len) {
		written = snprintf(buffer + offset, buffer_len - offset, "]");
		if (written < 0 || (size_t)written >= buffer_len - offset) {
			buffer[buffer_len - 1] = '\0';
		}
	} else {
		buffer[buffer_len - 1] = '\0';
	}

	if (first_entry) {
		copy_with_fallback(buffer, buffer_len, "[]");
	}
#else
	ARG_UNUSED(iface);
	copy_with_fallback(buffer, buffer_len, "[]");
#endif
}

static void parse_led_post(uint8_t *buf, size_t len)
{
	int ret;
	struct led_command cmd;
	const int expected_return_code = BIT_MASK(ARRAY_SIZE(led_command_descr));

	ret = json_obj_parse(buf, len, led_command_descr,
			       ARRAY_SIZE(led_command_descr), &cmd);
	if (ret != expected_return_code) {
		LOG_WRN("Failed to fully parse JSON payload, ret=%d", ret);
		return;
	}

	LOG_INF("POST request setting LED %d to state %d", cmd.led_num, cmd.led_state);

	if (leds_dev != NULL) {
		if (cmd.led_state) {
			led_on(leds_dev, cmd.led_num);
		} else {
			led_off(leds_dev, cmd.led_num);
		}
	}
}

static int uptime_handler(struct http_client_ctx *client, enum http_data_status status,
			  const struct http_request_ctx *request_ctx,
			  struct http_response_ctx *response_ctx,
			  void *user_data)
{
	int ret;
	static uint8_t uptime_buf[sizeof(STRINGIFY(INT64_MAX))];

	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	/* A payload is not expected with the GET request. Ignore any data and wait until
	 * final callback before sending response
	 */
	if (status == HTTP_SERVER_DATA_FINAL) {
		ret = snprintf(uptime_buf, sizeof(uptime_buf), "%" PRId64, k_uptime_get());
		if (ret < 0) {
			LOG_ERR("Failed to snprintf uptime, err %d", ret);
			return ret;
		}

		response_ctx->body = uptime_buf;
		response_ctx->body_len = ret;
		response_ctx->final_chunk = true;
	}

	return 0;
}

static int device_info_handler(struct http_client_ctx *client, enum http_data_status status,
			       const struct http_request_ctx *request_ctx,
			       struct http_response_ctx *response_ctx,
			       void *user_data)
{
	static char response_buffer[DEVICE_INFO_JSON_BUF_SIZE];
	static char serial_buf[OPENJBOD_SERIAL_MAX_LEN];
	static char board_rev_buf[OPENJBOD_BOARD_REV_MAX_LEN];
	static char ip_buf[INET_ADDRSTRLEN];
	static char subnet_buf[INET_ADDRSTRLEN];
	static char gateway_buf[INET_ADDRSTRLEN];
	static char mac_buf[18];

	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_DATA_FINAL) {
		struct net_if *iface = net_if_get_default();

		fetch_device_identity(serial_buf, sizeof(serial_buf),
				  board_rev_buf, sizeof(board_rev_buf));
		describe_ipv4_network(iface,
				      ip_buf, sizeof(ip_buf),
				      subnet_buf, sizeof(subnet_buf),
				      gateway_buf, sizeof(gateway_buf));
		format_mac_address(iface, mac_buf, sizeof(mac_buf));

		const char *version = openjbod_device_info_get_build_info();
		int written = snprintf(response_buffer, sizeof(response_buffer),
			 "{"
			 "\"serial\":\"%s\","\
			 "\"version\":\"%s\","\
			 "\"board_revision\":\"%s\","\
			 "\"ip_address\":\"%s\","\
			 "\"subnet_mask\":\"%s\","\
			 "\"gateway\":\"%s\","\
			 "\"mac_address\":\"%s\""
			 "}",
			 serial_buf,
			 version ? version : "Unknown",
			 board_rev_buf,
			 ip_buf,
			 subnet_buf,
			 gateway_buf,
			 mac_buf);

		if (written < 0) {
			LOG_ERR("Failed to format device info response");
			return written;
		}

		if (written >= (int)sizeof(response_buffer)) {
			LOG_WRN("Device info response truncated to %zu bytes", sizeof(response_buffer) - 1);
			response_ctx->body_len = sizeof(response_buffer) - 1;
		} else {
			response_ctx->body_len = (size_t)written;
		}

		response_ctx->body = response_buffer;
		response_ctx->final_chunk = true;
	}

	return 0;
}

static int led_handler(struct http_client_ctx *client, enum http_data_status status,
		       const struct http_request_ctx *request_ctx,
		       struct http_response_ctx *response_ctx,
		       void *user_data)
{
	static uint8_t post_payload_buf[32];
	static size_t cursor;

	ARG_UNUSED(user_data);

	LOG_DBG("LED handler status %d, size %zu", status, request_ctx->data_len);

	if (status == HTTP_SERVER_DATA_FINAL) {
		int auth_result = http_basic_auth_check(client);
		if (auth_result != 0) {
			LOG_WRN("Authentication failed for LED endpoint");
			http_send_auth_required_response(response_ctx);
			return 0;
		}
	}

	if (status == HTTP_SERVER_DATA_ABORTED) {
		cursor = 0;
		return 0;
	}

	if (request_ctx->data_len + cursor > sizeof(post_payload_buf)) {
		cursor = 0;
		return -ENOMEM;
	}

	memcpy(post_payload_buf + cursor, request_ctx->data, request_ctx->data_len);
	cursor += request_ctx->data_len;

	if (status == HTTP_SERVER_DATA_FINAL) {
		parse_led_post(post_payload_buf, cursor);
		cursor = 0;
	}

	return 0;
}

static const char *ipv6_mode_to_string(enum ipv6_mode mode)
{
	switch (mode) {
	case IPV6_MODE_DISABLED:
		return "Disabled";
	case IPV6_MODE_SLAAC:
		return "SLAAC";
	case IPV6_MODE_DHCPV6:
		return "DHCPv6";
	case IPV6_MODE_STATIC:
		return "Static";
	default:
		return "Unknown";
	}
}

static int status_handler(struct http_client_ctx *client, enum http_data_status status,
			 const struct http_request_ctx *request_ctx,
			 struct http_response_ctx *response_ctx,
			 void *user_data)
{
	static char response_buffer[STATUS_JSON_BUF_SIZE];
	struct temperature_data temp_data;
	char ipv6_json[256];

	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_DATA_FINAL) {
		int auth_result = http_basic_auth_check(client);
		if (auth_result != 0) {
			LOG_WRN("Authentication failed for status endpoint");
			http_send_auth_required_response(response_ctx);
			return 0;
		}
	}

	struct emc2301_data fan_data;
	char serial_buffer[OPENJBOD_SERIAL_MAX_LEN];
	char board_rev_buffer[OPENJBOD_BOARD_REV_MAX_LEN];
	struct net_if *iface;
	char mac_str[18];
	char ip_str[INET_ADDRSTRLEN];
	char subnet_str[INET_ADDRSTRLEN];
	char gateway_str[INET_ADDRSTRLEN];
	const char *ip_method_str;
	const struct openjbod_settings *settings;
	int ret;

	copy_with_fallback(ipv6_json, sizeof(ipv6_json), "[]");

	LOG_DBG("Status handler status %d", status);

	if (status == HTTP_SERVER_DATA_FINAL) {
		ret = temperature_read(&temp_data);
		bool temp_valid = (ret == 0);

		ret = emc2301_get_status(&fan_data);
		bool fan_valid = (ret == 0);

		fetch_device_identity(serial_buffer, sizeof(serial_buffer),
				  board_rev_buffer, sizeof(board_rev_buffer));

		settings = openjbod_settings_get();
		ip_method_str = (settings->network.ip_method == IP_METHOD_DHCP) ? "DHCP" : "Static";
		const char *ipv6_mode_str = ipv6_mode_to_string(settings->network.ipv6_mode);
		bool power_state = sr_latch_get_state();
		const char *power_state_str = power_state ? "on" : "off";

		iface = net_if_get_default();
		format_mac_address(iface, mac_str, sizeof(mac_str));
		describe_ipv4_network(iface,
				      ip_str, sizeof(ip_str),
				      subnet_str, sizeof(subnet_str),
				      gateway_str, sizeof(gateway_str));
		build_ipv6_address_json(iface, ipv6_json, sizeof(ipv6_json));

		const char *build_info = openjbod_device_info_get_build_info();
		int written = snprintf(response_buffer, sizeof(response_buffer),
			 "{"
			 "\"status\":\"success\","\
			 "\"temperature\":{"
			 "\"valid\":%s,"\
			 "\"ds18b20\":{"
			 "\"temperature\":%.3f,"\
			 "\"valid\":%s"
			 "},"\
			 "\"rp2040\":{"
			 "\"temperature\":%.3f,"\
			 "\"valid\":%s"
			 "}"
			 "},"\
			 "\"fan\":{"
			 "\"valid\":%s,"\
			 "\"rpm\":%d,"\
			 "\"speed_percent\":%d,"\
			 "\"fault\":%s"
			 "},"\
			 "\"device\":{"
			 "\"serial\":\"%s\","\
			 "\"version\":\"%s\","\
			 "\"board_revision\":\"%s\","\
			 "\"hostname\":\"%s\""
			 "},"\
			 "\"network\":{"
			 "\"mac_address\":\"%s\","\
			 "\"ip_address\":\"%s\","\
			 "\"subnet_mask\":\"%s\","\
			 "\"gateway\":\"%s\","\
			"\"ip_method\":\"%s\","\
			"\"ipv6_mode\":\"%s\","\
			 "\"ipv6_addresses\":%s"
			 "},"\
			 "\"power\":{"
			 "\"state\":\"%s\""
			 "}"
			 "}",
			 temp_valid ? "true" : "false",
			 (double)temp_data.ds18b20_temp,
			 temp_data.ds18b20_valid ? "true" : "false",
			 (double)temp_data.rp2040_temp,
			 temp_data.rp2040_valid ? "true" : "false",
			 fan_valid ? "true" : "false",
			 fan_valid ? fan_data.fan_rpm : 0,
			 fan_valid ? emc2301_duty_to_percent(fan_data.pwm_duty) : 0,
			 (fan_valid && fan_data.fan_fault) ? "true" : "false",
			 serial_buffer,
			 build_info ? build_info : "Unknown",
			 board_rev_buffer,
			 settings->network.hostname,
			 mac_str,
			 ip_str,
			 subnet_str,
			 gateway_str,
			 ip_method_str,
			 ipv6_mode_str,
			 ipv6_json,
			 power_state_str);

		if (written < 0) {
			LOG_ERR("Failed to format status response");
			return written;
		}

		if (written >= (int)sizeof(response_buffer)) {
			LOG_WRN("Status response truncated to %zu bytes", sizeof(response_buffer) - 1);
			response_ctx->body_len = sizeof(response_buffer) - 1;
		} else {
			response_ctx->body_len = (size_t)written;
		}

		response_ctx->body = response_buffer;
		response_ctx->final_chunk = true;
	}

	return 0;
}

struct http_resource_detail_dynamic uptime_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
	},
	.cb = uptime_handler,
	.user_data = NULL,
};

struct http_resource_detail_dynamic device_info_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
		.content_type = "application/json",
	},
	.cb = device_info_handler,
	.user_data = NULL,
};

struct http_resource_detail_dynamic led_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
	},
	.cb = led_handler,
	.user_data = NULL,
};

struct http_resource_detail_dynamic status_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
		.content_type = "application/json",
	},
	.cb = status_handler,
	.user_data = NULL,
};