/*
 * Copyright (c) 2025, OpenJBOD Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/hostname.h>
#include <zephyr/net/net_config.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/net/dhcpv4.h>
#if defined(CONFIG_NET_DHCPV6)
#include <zephyr/net/dhcpv6.h>
#endif
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/util_macro.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>

#include <ethernet/eth_stats.h>

#include "certificate.h"
#include "device_info.h"
#include "eeprom_mac.h"
#include "emc2301.h"
#include "fan_control.h"
#include "http/server_control.h"
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
#include "firmware_update.h"
#endif
#include "settings.h"
#include "sr_latch.h"
#include "status_led.h"
#include "temperature.h"
#include "usb_console.h"

LOG_MODULE_REGISTER(tank, LOG_LEVEL_INF);

static struct net_mgmt_event_callback mgmt_cb_ipv4;
#if defined(CONFIG_NET_IPV6)
static struct net_mgmt_event_callback mgmt_cb_ipv6;
#endif
static bool http_server_started;

static const struct gpio_dt_spec usb_sense = GPIO_DT_SPEC_GET_OR(DT_ALIAS(usb_sense), gpios, {0});
static struct gpio_callback usb_sense_cb_data;
static struct k_work_delayable usb_follow_work;
static bool last_usb_state;

static const struct gpio_dt_spec power_button = GPIO_DT_SPEC_GET_OR(DT_ALIAS(power_button), gpios, {0});
static struct gpio_callback power_button_cb_data;
static struct k_work_delayable power_button_debounce_work;
static struct k_work_delayable on_boot_power_work;

static int hex_char_to_nibble(char c, uint8_t *nibble)
{
	if (c >= '0' && c <= '9') {
		*nibble = (uint8_t)(c - '0');
		return 0;
	}

	c = (char)tolower((unsigned char)c);
	if (c >= 'a' && c <= 'f') {
		*nibble = (uint8_t)(10 + (c - 'a'));
		return 0;
	}

	return -EINVAL;
}

static int convert_hex_to_binary(const char *hex_string,
	 uint8_t *binary_data,
	 size_t binary_capacity,
	 size_t *binary_len)
{
	if (!hex_string || !binary_data || !binary_len) {
		return -EINVAL;
	}

	size_t hex_len = strlen(hex_string);
	if (hex_len == 0 || (hex_len % 2) != 0) {
		return -EINVAL;
	}

	size_t output_len = hex_len / 2;
	if (output_len > binary_capacity) {
		return -ENOMEM;
	}

	for (size_t i = 0; i < output_len; i++) {
		uint8_t high;
		uint8_t low;

		if (hex_char_to_nibble(hex_string[i * 2], &high) < 0 ||
		    hex_char_to_nibble(hex_string[i * 2 + 1], &low) < 0) {
			return -EINVAL;
		}

		binary_data[i] = (uint8_t)((high << 4) | low);
	}

	*binary_len = output_len;
	return 0;
}

static const char *net_addr_type_to_str(enum net_addr_type type)
{
	switch (type) {
	case NET_ADDR_AUTOCONF:
		return "autoconf";
	case NET_ADDR_DHCP:
		return "dhcp";
	case NET_ADDR_MANUAL:
		return "static";
	case NET_ADDR_OVERRIDABLE:
		return "overridable";
	default:
		return "unknown";
	}
}

static const char *ipv6_scope_label(const struct in6_addr *addr)
{
	if (net_ipv6_is_addr_loopback(addr)) {
		return "loopback";
	}
	if (net_ipv6_is_ll_addr(addr)) {
		return "link-local";
	}
	if (net_ipv6_is_addr_mcast(addr)) {
		return "multicast";
	}
	if (net_ipv6_is_global_addr(addr)) {
		return "global";
	}
	if ((addr->s6_addr[0] & 0xfeU) == 0xfcU) {
		return "unique-local";
	}
	return "other";
}

static void log_ipv6_address_event(struct net_if *iface,
		const struct in6_addr *addr,
		const char *action,
		const struct net_if_addr *ifaddr)
{
	char addr_str[INET6_ADDRSTRLEN];
	const char *scope = ipv6_scope_label(addr);
	const char *origin = ifaddr ? net_addr_type_to_str(ifaddr->addr_type) : "unknown";
	int iface_index = iface ? net_if_get_by_iface(iface) : -1;

	if (!net_addr_ntop(AF_INET6, addr, addr_str, sizeof(addr_str))) {
		snprintf(addr_str, sizeof(addr_str), "<invalid>");
	}

	if (iface_index < 0) {
		LOG_INF("IPv6 %s address %s: %s (origin: %s)", scope, action,
			addr_str, origin);
	} else {
		LOG_INF("IPv6 %s address %s on iface %d: %s (origin: %s)", scope,
			action, iface_index, addr_str, origin);
	}
}

static void http_server_try_start(struct net_if *iface);
static void on_boot_power_work_handler(struct k_work *work);

#if defined(CONFIG_NET_HTTPS_SERVICE) && defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS)
static void add_tls_credential(enum tls_credential_type type, const void *data,
			       size_t size, const char *label)
{
	int err = tls_credential_add(OPENJBOD_SERVER_CERTIFICATE_TAG, type, data, size);

	if (err < 0) {
		LOG_ERR("Failed to register %s: %d", label, err);
	} else {
		LOG_INF("Registered %s", label);
	}
}
#endif

static void setup_tls(void)
{
#if defined(CONFIG_NET_HTTPS_SERVICE)
#if defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS)
	const struct openjbod_settings *settings = openjbod_settings_get();

	static uint8_t custom_cert_data[CERTIFICATE_HEX_MAX_LEN / 2];
	static uint8_t custom_key_data[PRIVATE_KEY_HEX_MAX_LEN / 2];
	size_t custom_cert_size = 0;
	size_t custom_key_size = 0;
	bool use_embedded_certs = true;

	if (settings->http.use_custom_certificates) {
		LOG_INF("Attempting to load custom certificates from settings");

		if (settings->http.custom_certificate != NULL &&
		    settings->http.custom_private_key != NULL &&
		    strlen(settings->http.custom_certificate) > 0 &&
		    strlen(settings->http.custom_private_key) > 0) {
			int cert_result = convert_hex_to_binary(settings->http.custom_certificate,
					       custom_cert_data,
					       sizeof(custom_cert_data),
					       &custom_cert_size);
			int key_result = convert_hex_to_binary(settings->http.custom_private_key,
				      custom_key_data,
				      sizeof(custom_key_data),
				      &custom_key_size);

			if (cert_result == 0 && key_result == 0) {
				use_embedded_certs = false;
				LOG_INF("Using custom certificates from settings (cert: %zu bytes, key: %zu bytes)",
					custom_cert_size, custom_key_size);
			} else {
				LOG_WRN("Failed to convert hex certificates to binary (cert: %d, key: %d), falling back to embedded certificates",
					cert_result, key_result);
			}
		} else {
			LOG_WRN("Custom certificates enabled but no certificate data found in settings, falling back to embedded certificates");
		}
	}

	if (use_embedded_certs) {
		add_tls_credential(TLS_CREDENTIAL_PUBLIC_CERTIFICATE,
				   server_certificate, sizeof(server_certificate),
				   "embedded server certificate");
		add_tls_credential(TLS_CREDENTIAL_PRIVATE_KEY,
				   private_key, sizeof(private_key),
				   "embedded private key");
	} else {
		add_tls_credential(TLS_CREDENTIAL_PUBLIC_CERTIFICATE,
				   custom_cert_data, custom_cert_size,
				   "custom server certificate");
		add_tls_credential(TLS_CREDENTIAL_PRIVATE_KEY,
				   custom_key_data, custom_key_size,
				   "custom private key");
	}
#endif /* defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS) */
#endif /* defined(CONFIG_NET_HTTPS_SERVICE) */
}

static void http_server_try_start(struct net_if *iface)
{
	if (http_server_started) {
		return;
	}

	if (!iface) {
		iface = net_if_get_default();
	}

	if (!iface || !iface->config.ip.ipv4) {
		return;
	}

	struct net_if_ipv4 *ipv4 = iface->config.ip.ipv4;
	struct in_addr *preferred_addr = NULL;

	for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		if (ipv4->unicast[i].ipv4.addr_state == NET_ADDR_PREFERRED) {
			preferred_addr = &ipv4->unicast[i].ipv4.address.in_addr;
			break;
		}
	}

	if (!preferred_addr || preferred_addr->s_addr == 0U) {
		return;
	}

	const struct openjbod_settings *settings = openjbod_settings_get();
	if (!settings->http.enable_http && !settings->http.enable_https) {
		LOG_WRN("Both HTTP and HTTPS servers are disabled - no web interface available");
		http_server_started = true;
		/* An address is up even though no server is served. */
		status_led_set(STATUS_LED_SOLID);
		return;
	}

	restart_http_server();
	http_server_started = true;
	status_led_set(STATUS_LED_SOLID);

	/* The web server is up = the device booted healthy. If we just swapped to a
	 * pending (test) firmware image, confirm it now so MCUboot keeps it; an image
	 * that hangs before reaching here never confirms and is reverted on reboot.
	 */
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
	firmware_update_mark_healthy();
#endif

	char ip_buf[INET_ADDRSTRLEN];
	if (!net_addr_ntop(AF_INET, preferred_addr, ip_buf, sizeof(ip_buf))) {
		snprintf(ip_buf, sizeof(ip_buf), "<unknown>");
	}

	if (settings->http.enable_http && settings->http.enable_https) {
		LOG_INF("HTTP server available at http://%s:%u/ and https://%s:%u/",
			ip_buf, settings->http.http_port,
			ip_buf, settings->http.https_port);
	} else if (settings->http.enable_http) {
		LOG_INF("HTTP server available at http://%s:%u/",
			ip_buf, settings->http.http_port);
	} else {
		LOG_INF("HTTPS server available at https://%s:%u/",
			ip_buf, settings->http.https_port);
	}
}

static void net_event_handler(struct net_mgmt_event_callback *cb,
		uint64_t mgmt_event,
		struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_IPV4_ADDR_ADD:
	case NET_EVENT_IPV4_DHCP_BOUND:
		http_server_try_start(iface);
		break;
	case NET_EVENT_IPV4_ADDR_DEL:
	case NET_EVENT_IPV4_DHCP_STOP:
		http_server_started = false;
		/* Address lost - back to awaiting/blinking. */
		status_led_set(STATUS_LED_AWAITING);
		break;
	case NET_EVENT_IPV6_ADDR_ADD:
	case NET_EVENT_IPV6_DAD_SUCCEED: {
		if (cb->info && cb->info_length == sizeof(struct in6_addr)) {
			struct in6_addr addr;
			struct net_if *addr_iface = iface;
			const struct net_if_addr *ifaddr;

			memcpy(&addr, cb->info, sizeof(addr));
			ifaddr = net_if_ipv6_addr_lookup(&addr, &addr_iface);
			log_ipv6_address_event(addr_iface, &addr, "assigned", ifaddr);
		}
		break;
	}
	case NET_EVENT_IPV6_ADDR_DEL: {
		if (cb->info && cb->info_length == sizeof(struct in6_addr)) {
			struct in6_addr removed_addr;
			struct net_if *addr_iface = iface;
			const struct net_if_addr *ifaddr = NULL;

			memcpy(&removed_addr, cb->info, sizeof(removed_addr));
			ifaddr = net_if_ipv6_addr_lookup(&removed_addr, &addr_iface);
			log_ipv6_address_event(addr_iface, &removed_addr, "removed", ifaddr);
		}
		break;
	}
	default:
		break;
	}
}


#if defined(CONFIG_NET_IPV6)
static bool ipv6_is_link_local(const struct in6_addr *addr)
{
	return (addr->s6_addr[0] == 0xfe) && ((addr->s6_addr[1] & 0xc0) == 0x80);
}

static void clear_ipv6_addresses(struct net_if *iface)
{
	struct net_if_ipv6 *ipv6_cfg = NULL;

	if (net_if_config_ipv6_get(iface, &ipv6_cfg) < 0 || ipv6_cfg == NULL) {
		return;
	}

	for (int i = 0; i < NET_IF_MAX_IPV6_ADDR; i++) {
		struct net_if_addr *ifaddr = &ipv6_cfg->unicast[i];

		if (!ifaddr->is_used || ifaddr->address.family != AF_INET6) {
			continue;
		}

		if (ipv6_is_link_local(&ifaddr->address.in6_addr)) {
			continue;
		}

		if (!net_if_ipv6_addr_rm(iface, &ifaddr->address.in6_addr)) {
			LOG_WRN("Failed to remove IPv6 address index %d", i);
		}
	}
}

static void clear_ipv6_prefixes(struct net_if *iface)
{
	struct net_if_ipv6 *ipv6_cfg = NULL;

	if (net_if_config_ipv6_get(iface, &ipv6_cfg) < 0 || ipv6_cfg == NULL) {
		return;
	}

	for (int i = 0; i < NET_IF_MAX_IPV6_PREFIX; i++) {
		struct net_if_ipv6_prefix *prefix = &ipv6_cfg->prefix[i];

		if (!prefix->is_used) {
			continue;
		}

		(void)net_if_ipv6_prefix_rm(iface, &prefix->prefix, prefix->len);
	}
}

static void clear_ipv6_routers(struct net_if *iface)
{
	struct net_if_router *router;

	while ((router = net_if_ipv6_router_find_default(iface, NULL)) != NULL) {
		if (!net_if_ipv6_router_rm(router)) {
			break;
		}
	}
}

static void clear_ipv6_state(struct net_if *iface)
{
	clear_ipv6_addresses(iface);
	clear_ipv6_prefixes(iface);
	clear_ipv6_routers(iface);
}

static void apply_ipv6_prefix_mask(struct in6_addr *addr, uint8_t prefix_len)
{
	uint8_t full_bytes = prefix_len / 8U;
	uint8_t remaining_bits = prefix_len % 8U;

	for (uint8_t i = 0U; i < 16U; i++) {
		if (i < full_bytes) {
			continue;
		}

		if (i == full_bytes && remaining_bits > 0U) {
			uint8_t mask = (uint8_t)(0xFFu << (8U - remaining_bits));
			addr->s6_addr[i] &= mask;
			continue;
		}

		addr->s6_addr[i] = 0U;
	}
}

static int configure_static_ipv6(struct net_if *iface, const struct network_settings *net)
{
	struct in6_addr addr;
	struct net_if_addr *ifaddr;
	uint8_t prefix_len = net->ipv6_prefix_length;
	int ret;

	if (strlen(net->ipv6_addr) == 0U) {
		LOG_WRN("IPv6 static mode selected without address");
		return -EINVAL;
	}

	ret = zsock_inet_pton(AF_INET6, net->ipv6_addr, &addr);
	if (ret != 1) {
		LOG_WRN("Invalid IPv6 address: %s", net->ipv6_addr);
		return -EINVAL;
	}

	if (prefix_len == 0U || prefix_len > 128U) {
		LOG_WRN("Invalid IPv6 prefix length %u, defaulting to /64", prefix_len);
		prefix_len = 64U;
	}

	ifaddr = net_if_ipv6_addr_add(iface, &addr, NET_ADDR_MANUAL, UINT32_MAX);
	if (!ifaddr) {
		LOG_ERR("Failed to add static IPv6 address");
		return -ENOMEM;
	}

	net_if_addr_set_lf(ifaddr, true);

	struct in6_addr prefix_addr = addr;
	apply_ipv6_prefix_mask(&prefix_addr, prefix_len);
	struct net_if_ipv6_prefix *prefix =
		net_if_ipv6_prefix_add(iface, &prefix_addr, prefix_len, UINT32_MAX);
	if (prefix) {
		net_if_ipv6_prefix_set_lf(prefix, true);
	} else {
		LOG_WRN("Failed to register IPv6 prefix %s/%u", net->ipv6_addr, prefix_len);
	}

	if (strlen(net->ipv6_gateway) > 0U) {
		struct in6_addr gateway;

		ret = zsock_inet_pton(AF_INET6, net->ipv6_gateway, &gateway);
		if (ret == 1) {
			struct net_if_router *router =
				net_if_ipv6_router_lookup(iface, &gateway);

			if (!router) {
				router = net_if_ipv6_router_add(iface, &gateway, UINT16_MAX);
			} else {
				net_if_ipv6_router_update_lifetime(router, UINT16_MAX);
			}

			if (router) {
				router->is_infinite = 1U;
				LOG_INF("IPv6 gateway set to %s", net->ipv6_gateway);
			} else {
				LOG_WRN("Failed to configure IPv6 gateway %s", net->ipv6_gateway);
			}
		} else {
			LOG_WRN("Invalid IPv6 gateway: %s", net->ipv6_gateway);
		}
	}

	LOG_INF("Static IPv6 configured: %s/%u", net->ipv6_addr, prefix_len);
	return 0;
}

static int configure_ipv6_mode(struct net_if *iface, struct openjbod_settings *settings)
{
	enum ipv6_mode mode = settings->network.ipv6_mode;

#if defined(CONFIG_NET_DHCPV6)
	net_dhcpv6_stop(iface);
#endif

	if (mode == IPV6_MODE_DISABLED) {
		clear_ipv6_state(iface);
		net_if_flag_set(iface, NET_IF_IPV6_NO_ND);
		LOG_INF("IPv6 disabled per configuration");
		return 0;
	}

	net_if_flag_clear(iface, NET_IF_IPV6_NO_ND);
	clear_ipv6_state(iface);

	switch (mode) {
	case IPV6_MODE_SLAAC:
		net_if_start_rs(iface);
		LOG_INF("IPv6 SLAAC triggered");
		return 0;
	case IPV6_MODE_DHCPV6:
#if defined(CONFIG_NET_DHCPV6)
	{
		struct net_dhcpv6_params params = {
			.request_addr = true,
			.request_prefix = true,
		};
		net_dhcpv6_start(iface, &params);
		LOG_INF("IPv6 DHCPv6 client started");
		return 0;
	}
#else
		LOG_WRN("DHCPv6 mode requested but CONFIG_NET_DHCPV6 is disabled");
		return -ENOTSUP;
#endif
	case IPV6_MODE_STATIC:
		return configure_static_ipv6(iface, &settings->network);
	default:
		LOG_WRN("Unknown IPv6 mode: %d", mode);
		return -EINVAL;
	}
}
#else
static int configure_ipv6_mode(struct net_if *iface, struct openjbod_settings *settings)
{
	ARG_UNUSED(iface);
	ARG_UNUSED(settings);
	return 0;
}
#endif /* CONFIG_NET_IPV6 */

static int configure_static_ip(struct net_if *iface, struct openjbod_settings *settings)
{
	struct in_addr addr, netmask, gw, dns;
	int ret;
	uint8_t prefix_len;

	/* Parse IP address */
	ret = zsock_inet_pton(AF_INET, settings->network.ip_addr, &addr);
	if (ret != 1) {
		LOG_ERR("Invalid IP address: %s", settings->network.ip_addr);
		return -EINVAL;
	}

	/* Parse netmask */
	ret = zsock_inet_pton(AF_INET, settings->network.ip_mask, &netmask);
	if (ret != 1) {
		LOG_ERR("Invalid netmask: %s", settings->network.ip_mask);
		return -EINVAL;
	}

	/* Parse gateway */
	ret = zsock_inet_pton(AF_INET, settings->network.gw_addr, &gw);
	if (ret != 1) {
		LOG_ERR("Invalid gateway: %s", settings->network.gw_addr);
		return -EINVAL;
	}

	/* Parse DNS server */
	ret = zsock_inet_pton(AF_INET, settings->network.dns1, &dns);
	if (ret != 1) {
		LOG_ERR("Invalid DNS server: %s", settings->network.dns1);
		return -EINVAL;
	}

	/* Convert netmask to prefix length */
	uint32_t mask = ntohl(netmask.s_addr);
	prefix_len = __builtin_popcount(mask);

	/* Set IP address with prefix length */
	struct net_if_addr *if_addr = net_if_ipv4_addr_add(iface,
							    &addr,
							    NET_ADDR_MANUAL,
							    prefix_len);
	if (!if_addr) {
		LOG_ERR("Failed to add IP address");
		return -ENOMEM;
	}

	/* Set default gateway */
	net_if_ipv4_set_gw(iface, &gw);

	/* Configure DNS server */
	/* Note: DNS configuration might require additional setup depending on your DNS requirements */

	LOG_INF("Static IP configured:");
	LOG_INF("  IP Address: %s/%d", settings->network.ip_addr, prefix_len);
	LOG_INF("  Netmask: %s", settings->network.ip_mask);
	LOG_INF("  Gateway: %s", settings->network.gw_addr);
	LOG_INF("  DNS: %s", settings->network.dns1);

	http_server_try_start(iface);

	return 0;
}

static int init_networking(void)
{
	struct openjbod_settings *settings;
	const char *hostname;
	struct net_if *iface;
	uint8_t mac_addr[6];
	int ret;

	/* Get hostname from settings */
	settings = openjbod_settings_get();
	hostname = settings->network.hostname;

	/* Subscribe to IPv4 management events */
	const uint64_t ipv4_events =
		NET_EVENT_IPV4_ADDR_ADD |
		NET_EVENT_IPV4_DHCP_BOUND |
		NET_EVENT_IPV4_DHCP_STOP |
		NET_EVENT_IPV4_ADDR_DEL;

	net_mgmt_init_event_callback(&mgmt_cb_ipv4, net_event_handler, ipv4_events);
	net_mgmt_add_event_callback(&mgmt_cb_ipv4);

#if defined(CONFIG_NET_IPV6)
	/* Subscribe to IPv6 management events */
	const uint64_t ipv6_events =
		NET_EVENT_IPV6_ADDR_ADD |
		NET_EVENT_IPV6_ADDR_DEL |
		NET_EVENT_IPV6_DAD_SUCCEED;

	net_mgmt_init_event_callback(&mgmt_cb_ipv6, net_event_handler, ipv6_events);
	net_mgmt_add_event_callback(&mgmt_cb_ipv6);
#endif

	/* Get Ethernet interface */
	iface = net_if_get_first_by_type(&NET_L2_GET_NAME(ETHERNET));
	if (!iface) {
		LOG_ERR("No Ethernet interface found");
		return -ENODEV;
	}

	/* Bringing up the network - blink until an address yields a running server. */
	status_led_set(STATUS_LED_AWAITING);

	/* Set MAC address from EEPROM before starting DHCP */
	ret = read_mac_address(mac_addr);
	if (ret == 0) {
		/* First set it on the network interface */
		ret = net_if_set_link_addr(iface, mac_addr, sizeof(mac_addr), NET_LINK_ETHERNET);
		if (ret < 0) {
			LOG_ERR("Failed to set MAC address on interface: %d", ret);
		} else {
			LOG_INF("MAC address set on interface: %02x:%02x:%02x:%02x:%02x:%02x",
				mac_addr[0], mac_addr[1], mac_addr[2],
				mac_addr[3], mac_addr[4], mac_addr[5]);
		}
		
		/* Now configure the hardware directly using Ethernet config API */
		struct ethernet_config ethcfg = {0};
		memcpy(ethcfg.mac_address.addr, mac_addr, sizeof(mac_addr));
		
		const struct device *eth_dev = net_if_get_device(iface);
		const struct ethernet_api *eth_api = (const struct ethernet_api *)eth_dev->api;
		
		if (eth_api && eth_api->set_config) {
			ret = eth_api->set_config(eth_dev, ETHERNET_CONFIG_TYPE_MAC_ADDRESS, &ethcfg);
			if (ret < 0) {
				LOG_ERR("Failed to configure hardware MAC address: %d", ret);
			} else {
				LOG_INF("Hardware MAC address configured successfully");
			}
		} else {
			LOG_WRN("Ethernet driver does not support configuration");
		}
	} else {
		LOG_ERR("Failed to read MAC address from EEPROM: %d", ret);
	}

	/* Set hostname from settings */
	ret = net_hostname_set((char *)hostname, strlen(hostname));
	if (ret < 0) {
		LOG_WRN("Failed to set hostname: %d", ret);
	} else {
		LOG_INF("Hostname set from settings to: %s", hostname);
	}

	/* Configure network based on IP method setting */
	if (settings->network.ip_method == IP_METHOD_DHCP) {
		LOG_INF("Starting DHCP configuration");
		net_dhcpv4_start(iface);
		LOG_INF("Network initialized, DHCP started");
	} else if (settings->network.ip_method == IP_METHOD_STATIC) {
		LOG_INF("Starting static IP configuration");
		ret = configure_static_ip(iface, settings);
		if (ret < 0) {
			LOG_ERR("Failed to configure static IP: %d", ret);
			LOG_WRN("Falling back to DHCP configuration");
			net_dhcpv4_start(iface);

			struct network_settings fallback = settings->network;
			fallback.ip_method = IP_METHOD_DHCP;
			int save_ret = openjbod_settings_set_network(&fallback);
			if (save_ret == 0) {
				settings->network.ip_method = IP_METHOD_DHCP;
				LOG_INF("Network settings reverted to DHCP after static configuration failure");
			} else {
				LOG_ERR("Failed to persist DHCP fallback settings: %d", save_ret);
			}

			return 0;
		}
		LOG_INF("Network initialized with static IP");
	} else {
		LOG_ERR("Invalid IP method: %d", settings->network.ip_method);
		return -EINVAL;
	}

#if defined(CONFIG_NET_IPV6)
	int ipv6_ret = configure_ipv6_mode(iface, settings);
	if (ipv6_ret < 0) {
		LOG_WRN("Failed to configure IPv6: %d", ipv6_ret);
	}
#endif

	return 0;
}

static void usb_follow_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	bool current_usb_state;
	int ret;

	if (!device_is_ready(usb_sense.port)) {
		LOG_ERR("USB sense GPIO port not ready");
		return;
	}

	/* Read current USB state */
	ret = gpio_pin_get_dt(&usb_sense);
	if (ret < 0) {
		LOG_ERR("Failed to read USB sense pin: %d", ret);
		return;
	}
	current_usb_state = (ret == 1);

	LOG_DBG("USB follow work: current state=%s, last state=%s", 
		current_usb_state ? "HIGH" : "LOW", 
		last_usb_state ? "HIGH" : "LOW");

	/* If state hasn't changed during delay, take action */
	if (current_usb_state == last_usb_state) {
		if (current_usb_state) {
			LOG_INF("USB power still detected - turning ATX power ON");
			sr_latch_set_on();
		} else {
			LOG_INF("USB power still removed - turning ATX power OFF");
			sr_latch_set_off();
		}
	} else {
		LOG_INF("USB state changed during delay, no action taken");
	}
}

static void on_boot_power_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	LOG_INF("Turning ATX power ON after delay");
	sr_latch_set_on();
}

static void usb_sense_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	struct openjbod_settings *settings = openjbod_settings_get();
	bool current_usb_state;
	int ret;

	if (!settings->power.follow_usb) {
		return;  /* USB following is disabled */
	}

	/* Read current USB state */
	ret = gpio_pin_get_dt(&usb_sense);
	if (ret < 0) {
		LOG_ERR("Failed to read USB sense pin: %d", ret);
		return;
	}
	current_usb_state = (ret == 1);
	last_usb_state = current_usb_state;

	/* Cancel any pending work */
	k_work_cancel_delayable(&usb_follow_work);

	if (settings->power.follow_usb_delay == 0) {
		/* No delay, take action immediately */
		if (current_usb_state) {
			LOG_INF("USB power detected - turning ATX power ON (immediate)");
			sr_latch_set_on();
		} else {
			LOG_INF("USB power removed - turning ATX power OFF (immediate)");
			sr_latch_set_off();
		}
	} else {
		/* Schedule delayed action */
		if (current_usb_state) {
			LOG_INF("USB power detected - turning ATX power ON in %u seconds", settings->power.follow_usb_delay);
		} else {
			LOG_INF("USB power removed - turning ATX power OFF in %u seconds", settings->power.follow_usb_delay);
		}
		k_work_schedule(&usb_follow_work, K_SECONDS(settings->power.follow_usb_delay));
	}
}

static void power_button_debounce_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	struct openjbod_settings *settings = openjbod_settings_get();
	
	/* Check if power button is ignored */
	if (settings->power.ignore_power_switch) {
		LOG_INF("Power button pressed but ignored due to settings");
		/* Re-enable interrupt */
		gpio_pin_interrupt_configure_dt(&power_button, GPIO_INT_EDGE_FALLING);
		return;
	}
	
	bool current_state = sr_latch_get_state();
	
	if (current_state) {
		LOG_INF("Power button pressed - turning ATX power OFF");
		sr_latch_set_off();
	} else {
		LOG_INF("Power button pressed - turning ATX power ON");
		sr_latch_set_on();
	}
	
	/* Re-enable interrupt */
	gpio_pin_interrupt_configure_dt(&power_button, GPIO_INT_EDGE_FALLING);
}

static void power_button_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	/* Disable interrupt to prevent bouncing */
	gpio_pin_interrupt_configure_dt(&power_button, GPIO_INT_DISABLE);
	
	/* Schedule debounced action after 200ms */
	k_work_schedule(&power_button_debounce_work, K_MSEC(200));
}

static int init_power_management(void)
{
	struct openjbod_settings *settings = openjbod_settings_get();
	int ret;

	LOG_INF("Initializing power management...");

	/* Initialize work handlers */
	k_work_init_delayable(&usb_follow_work, usb_follow_work_handler);
	k_work_init_delayable(&power_button_debounce_work, power_button_debounce_work_handler);
	k_work_init_delayable(&on_boot_power_work, on_boot_power_work_handler);

	/* Configure USB sense GPIO */
	if (!gpio_is_ready_dt(&usb_sense)) {
		LOG_ERR("USB sense GPIO port not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&usb_sense, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure USB sense GPIO: %d", ret);
		return ret;
	}

	/* Set up interrupt callback for USB sense changes if follow_usb is enabled */
	if (settings->power.follow_usb) {
		gpio_init_callback(&usb_sense_cb_data, usb_sense_callback, BIT(usb_sense.pin));
		ret = gpio_add_callback(usb_sense.port, &usb_sense_cb_data);
		if (ret < 0) {
			LOG_ERR("Failed to add USB sense callback: %d", ret);
			return ret;
		}

		ret = gpio_pin_interrupt_configure_dt(&usb_sense, GPIO_INT_EDGE_BOTH);
		if (ret < 0) {
			LOG_ERR("Failed to configure USB sense interrupt: %d", ret);
			return ret;
		}

		/* Read initial USB state */
		ret = gpio_pin_get_dt(&usb_sense);
		if (ret >= 0) {
			last_usb_state = (ret == 1);
			LOG_INF("USB follow enabled. Initial USB state: %s", 
				last_usb_state ? "HIGH" : "LOW");
		}
	}

	/* Configure power button GPIO */
	if (!gpio_is_ready_dt(&power_button)) {
		LOG_ERR("Power button GPIO port not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&power_button, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure power button GPIO: %d", ret);
		return ret;
	}

	/* Set up interrupt callback for power button */
	gpio_init_callback(&power_button_cb_data, power_button_callback, BIT(power_button.pin));
	ret = gpio_add_callback(power_button.port, &power_button_cb_data);
	if (ret < 0) {
		LOG_ERR("Failed to add power button callback: %d", ret);
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&power_button, GPIO_INT_EDGE_FALLING);
	if (ret < 0) {
		LOG_ERR("Failed to configure power button interrupt: %d", ret);
		return ret;
	}

	LOG_INF("Power button initialized on GPIO%d", power_button.pin);

	/* Handle on_boot power setting */
	if (settings->power.on_boot) {
		LOG_INF("On-boot power enabled with %u second delay", settings->power.on_boot_delay);
		if (settings->power.on_boot_delay == 0) {
			LOG_INF("Turning ATX power ON immediately");
			sr_latch_set_on();
		} else {
			LOG_INF("Scheduling ATX power ON in %u seconds", settings->power.on_boot_delay);
			k_work_schedule(&on_boot_power_work, K_SECONDS(settings->power.on_boot_delay));
		}
	} else {
		LOG_INF("On-boot power disabled");
	}

	LOG_INF("Power management initialized");
	return 0;
}

/* Apply persisted console settings: optionally bring up the USB CDC-ACM shell,
 * and optionally silence the hardware UART shell. Call after settings load.
 *
 * Note: the boot banner and early-init logs are emitted on the UART before
 * settings are available, so disabling the UART console only takes effect from
 * here onward. The settings-reset route remains a recovery path.
 */
static void tank_console_apply_settings(void)
{
	const struct openjbod_settings *s = openjbod_settings_get();

	if (s->console.usb_enabled) {
		(void)tank_usb_console_init();  /* no-op stub if compiled out */
	} else {
		LOG_INF("USB console disabled by settings");
	}

#if defined(CONFIG_SHELL_BACKEND_SERIAL)
	if (!s->console.uart_enabled) {
		const struct shell *uart_sh = shell_backend_uart_get_ptr();

		if (uart_sh != NULL) {
			shell_stop(uart_sh);
			LOG_INF("UART console disabled by settings");
		}
	}
#endif
}

int main(void)
{
	int ret;
	char serial_number[OPENJBOD_SERIAL_MAX_LEN];

	LOG_INF("Tank starting...");

	/* Initialize device info and display version/serial */
	ret = openjbod_device_info_init();
	if (ret < 0) {
		LOG_ERR("Failed to initialize device info: %d", ret);
	} else {
		/* Display device information */
		ret = openjbod_device_info_get_serial(serial_number, sizeof(serial_number));
		if (ret == 0) {
			LOG_INF("Device Serial Number: %s", serial_number);
		}
		LOG_INF("Firmware Version: %s", openjbod_device_info_get_version());
		LOG_INF("Build Info: %s", openjbod_device_info_get_build_info());
	}

	/* Initialize settings subsystem */
	ret = openjbod_settings_init();
	if (ret < 0) {
		LOG_ERR("Settings initialization failed: %d", ret);
		return ret;
	}
	
	/* Load settings from storage */
	ret = openjbod_settings_load_all();
	if (ret < 0) {
		LOG_WRN("Settings load failed, using defaults: %d", ret);
		/* Continue with default settings */
	}

	/* Apply console settings (USB CDC-ACM second shell, UART enable/disable). */
	tank_console_apply_settings();

	/* Status LED off at boot; init_networking() switches it to blinking. */
	status_led_init();

	init_networking();

	ret = sr_latch_init();
	if (ret < 0) {
		LOG_ERR("Failed to initialize SR latch: %d", ret);
		return ret;
	}
	
	/* Initialize power management features */
	ret = init_power_management();
	if (ret < 0) {
		LOG_ERR("Failed to initialize power management: %d", ret);
		return ret;
	}

	ret = temperature_init();
	if (ret < 0) {
		LOG_WRN("Temperature sensors initialization failed: %d", ret);
		/* Don't return error - continue without temperature sensors */
	}

	ret = emc2301_init();
	if (ret < 0) {
		LOG_WRN("EMC2301 fan controller initialization failed: %d", ret);
		/* Don't return error - continue without fan controller */
	}

	/* Initialize fan control subsystem */
	ret = fan_control_init();
	if (ret < 0) {
		LOG_WRN("Fan control initialization failed: %d", ret);
		/* Don't return error - continue without automatic fan control */
	} else {
		/* Start fan control background task */
		ret = fan_control_start();
		if (ret < 0) {
			LOG_WRN("Failed to start fan control task: %d", ret);
		} else {
			LOG_INF("Automatic fan control started");
		}
	}

	/* Note: Default certificate handling now integrated into TLS setup via settings system */

	setup_tls();

	LOG_INF("Initialization complete - waiting for network events");

	while (1) {
		/* Periodically retry in case network events were missed */
		http_server_try_start(NULL);
		k_sleep(K_SECONDS(60));
	}
	
	return 0;
}
