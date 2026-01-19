/*
 * Copyright (c) 2024 OpenJBOD Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/settings/settings.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>
#include <zephyr/random/random.h>
#include <psa/crypto.h>
#include <zephyr/sys/base64.h>
#include "settings.h"

LOG_MODULE_REGISTER(tank_settings, LOG_LEVEL_INF);

#define OPENJBOD_DEFAULT_SETTINGS_INITIALIZER                                            \
{                                                                                       \
	.network = {                                                                        \
		.ip_method = IP_METHOD_DHCP,                                                     \
		.ip_addr = "192.168.1.100",                                                   \
		.gw_addr = "192.168.1.1",                                                    \
		.ip_mask = "255.255.255.0",                                                   \
		.dns1 = "8.8.8.8",                                                            \
		.hostname = "openjbod",                                                       \
		.ipv6_mode = IPV6_MODE_SLAAC,                                                   \
		.ipv6_addr = "",                                                             \
		.ipv6_prefix_length = 64,                                                       \
		.ipv6_gateway = "",                                                          \
		.ipv6_dns1 = ""                                                               \
	},                                                                                  \
	.power = {                                                                          \
		.ignore_power_switch = false,                                                   \
		.on_boot = false,                                                                \
		.on_boot_delay = 0,                                                              \
		.follow_usb = false,                                                            \
		.follow_usb_delay = 5                                                           \
	},                                                                                  \
	.http = {                                                                           \
		.enable_http = true,                                                            \
		.enable_https = true,                                                           \
		.http_port = 80,                                                                \
		.https_port = 443,                                                              \
		.use_custom_certificates = false                                                \
	},                                                                                  \
	.environment = {                                                                   \
		.use_external_fan_control = false,                                              \
		.fan_update_interval_ms = 5000,  /* Update every 5 seconds */                   \
		.fan_hysteresis_percent = 5,     /* 5% hysteresis to prevent oscillation */     \
		.fan_curve = {                                                                 \
			{20.0f, 0},    /* Below 20°C: 0% fan speed */                                 \
			{30.0f, 20},   /* 30°C: 20% fan speed */                                      \
			{40.0f, 40},   /* 40°C: 40% fan speed */                                      \
			{50.0f, 70},   /* 50°C: 70% fan speed */                                      \
			{60.0f, 100}   /* 60°C and above: 100% fan speed */                            \
		}                                                                               \
	}                                                                                   \
}

static const struct openjbod_settings default_settings = OPENJBOD_DEFAULT_SETTINGS_INITIALIZER;
static struct openjbod_settings current_settings = OPENJBOD_DEFAULT_SETTINGS_INITIALIZER;

static int network_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg);
static int network_settings_commit(void);
static int network_settings_export(int (*cb)(const char *name, const void *value, size_t val_len));

static int power_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg);
static int power_settings_commit(void);
static int power_settings_export(int (*cb)(const char *name, const void *value, size_t val_len));

static int auth_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg);
static int auth_settings_commit(void);
static int auth_settings_export(int (*cb)(const char *name, const void *value, size_t val_len));

static int http_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg);
static int http_settings_commit(void);
static int http_settings_export(int (*cb)(const char *name, const void *value, size_t val_len));

static int environment_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg);
static int environment_settings_commit(void);
static int environment_settings_export(int (*cb)(const char *name, const void *value, size_t val_len));

SETTINGS_STATIC_HANDLER_DEFINE(network, "network", NULL, network_settings_set,
			       network_settings_commit, network_settings_export);

SETTINGS_STATIC_HANDLER_DEFINE(power, "power", NULL, power_settings_set,
			       power_settings_commit, power_settings_export);

SETTINGS_STATIC_HANDLER_DEFINE(auth, "auth", NULL, auth_settings_set,
			       auth_settings_commit, auth_settings_export);

SETTINGS_STATIC_HANDLER_DEFINE(http, "http", NULL, http_settings_set,
			       http_settings_commit, http_settings_export);

SETTINGS_STATIC_HANDLER_DEFINE(environment, "environment", NULL, environment_settings_set,
			       environment_settings_commit, environment_settings_export);

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

static int network_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const char *next;
	size_t name_len;
	int rc;

	name_len = settings_name_next(name, &next);

	if (!next) {
		if (!strncmp(name, "ip_method", name_len)) {
			if (len != sizeof(current_settings.network.ip_method)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, &current_settings.network.ip_method, 
				     sizeof(current_settings.network.ip_method));
			if (rc >= 0) {
				if(current_settings.network.ip_method == 0) {
					LOG_INF("Loaded ip_method: DHCP");
				} else if(current_settings.network.ip_method == 1) {
					LOG_INF("Loaded ip_method: Static");
				} else {
					LOG_INF("??? illegal ip_method ???");
				}
			}
			return 0;
		}

		if (!strncmp(name, "ip_addr", name_len)) {
			if (len >= sizeof(current_settings.network.ip_addr)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, current_settings.network.ip_addr, len);
			if (rc >= 0) {
				current_settings.network.ip_addr[len] = '\0';
				LOG_INF("Loaded ip_addr: %s", current_settings.network.ip_addr);
			}
			return 0;
		}

		if (!strncmp(name, "gw_addr", name_len)) {
			if (len >= sizeof(current_settings.network.gw_addr)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, current_settings.network.gw_addr, len);
			if (rc >= 0) {
				current_settings.network.gw_addr[len] = '\0';
				LOG_INF("Loaded gw_addr: %s", current_settings.network.gw_addr);
			}
			return 0;
		}

		if (!strncmp(name, "ip_mask", name_len)) {
			if (len >= sizeof(current_settings.network.ip_mask)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, current_settings.network.ip_mask, len);
			if (rc >= 0) {
				current_settings.network.ip_mask[len] = '\0';
				LOG_INF("Loaded ip_mask: %s", current_settings.network.ip_mask);
			}
			return 0;
		}

		if (!strncmp(name, "dns1", name_len)) {
			if (len >= sizeof(current_settings.network.dns1)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, current_settings.network.dns1, len);
			if (rc >= 0) {
				current_settings.network.dns1[len] = '\0';
				LOG_INF("Loaded dns1: %s", current_settings.network.dns1);
			}
			return 0;
		}

		if (!strncmp(name, "hostname", name_len)) {
			if (len >= sizeof(current_settings.network.hostname)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, current_settings.network.hostname, len);
			if (rc >= 0) {
				current_settings.network.hostname[len] = '\0';
				LOG_INF("Loaded hostname: %s", current_settings.network.hostname);
			}
			return 0;
		}

		if (!strncmp(name, "ipv6_mode", name_len)) {
			if (len != sizeof(current_settings.network.ipv6_mode)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, &current_settings.network.ipv6_mode,
				     sizeof(current_settings.network.ipv6_mode));
			if (rc >= 0) {
				LOG_INF("Loaded ipv6_mode: %s",
					ipv6_mode_to_string(current_settings.network.ipv6_mode));
			}
			return 0;
		}

		if (!strncmp(name, "ipv6_addr", name_len)) {
			if (len >= sizeof(current_settings.network.ipv6_addr)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, current_settings.network.ipv6_addr, len);
			if (rc >= 0) {
				current_settings.network.ipv6_addr[len] = '\0';
				LOG_INF("Loaded ipv6_addr: %s", current_settings.network.ipv6_addr);
			}
			return 0;
		}

		if (!strncmp(name, "ipv6_prefix_length", name_len)) {
			if (len != sizeof(current_settings.network.ipv6_prefix_length)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, &current_settings.network.ipv6_prefix_length,
				     sizeof(current_settings.network.ipv6_prefix_length));
			if (rc >= 0) {
				LOG_INF("Loaded ipv6_prefix_length: %u",
					current_settings.network.ipv6_prefix_length);
			}
			return 0;
		}

		if (!strncmp(name, "ipv6_gateway", name_len)) {
			if (len >= sizeof(current_settings.network.ipv6_gateway)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, current_settings.network.ipv6_gateway, len);
			if (rc >= 0) {
				current_settings.network.ipv6_gateway[len] = '\0';
				LOG_INF("Loaded ipv6_gateway: %s", current_settings.network.ipv6_gateway);
			}
			return 0;
		}

		if (!strncmp(name, "ipv6_dns1", name_len)) {
			if (len >= sizeof(current_settings.network.ipv6_dns1)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, current_settings.network.ipv6_dns1, len);
			if (rc >= 0) {
				current_settings.network.ipv6_dns1[len] = '\0';
				LOG_INF("Loaded ipv6_dns1: %s", current_settings.network.ipv6_dns1);
			}
			return 0;
		}
	}
	return -ENOENT;
}

static int network_settings_commit(void)
{
	LOG_INF("Network settings loaded successfully");
	return 0;
}

static int network_settings_export(int (*cb)(const char *name, const void *value, size_t val_len))
{
	LOG_INF("Exporting network settings");
	
	(void)cb("network/ip_method", &current_settings.network.ip_method, 
		 sizeof(current_settings.network.ip_method));
	(void)cb("network/ip_addr", current_settings.network.ip_addr, 
		 strlen(current_settings.network.ip_addr));
	(void)cb("network/gw_addr", current_settings.network.gw_addr, 
		 strlen(current_settings.network.gw_addr));
	(void)cb("network/ip_mask", current_settings.network.ip_mask, 
		 strlen(current_settings.network.ip_mask));
	(void)cb("network/dns1", current_settings.network.dns1, 
		 strlen(current_settings.network.dns1));
	(void)cb("network/hostname", current_settings.network.hostname, 
		 strlen(current_settings.network.hostname));
	(void)cb("network/ipv6_mode", &current_settings.network.ipv6_mode,
		 sizeof(current_settings.network.ipv6_mode));
	(void)cb("network/ipv6_addr", current_settings.network.ipv6_addr,
		 strlen(current_settings.network.ipv6_addr));
	(void)cb("network/ipv6_prefix_length",
		 &current_settings.network.ipv6_prefix_length,
		 sizeof(current_settings.network.ipv6_prefix_length));
	(void)cb("network/ipv6_gateway", current_settings.network.ipv6_gateway,
		 strlen(current_settings.network.ipv6_gateway));
	(void)cb("network/ipv6_dns1", current_settings.network.ipv6_dns1,
		 strlen(current_settings.network.ipv6_dns1));

	return 0;
}

static int power_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const char *next;
	size_t name_len;
	int rc;

	name_len = settings_name_next(name, &next);

	if (!next) {
		if (!strncmp(name, "ignore_power_switch", name_len)) {
			if (len != sizeof(current_settings.power.ignore_power_switch)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, &current_settings.power.ignore_power_switch, 
				     sizeof(current_settings.power.ignore_power_switch));
			if (rc >= 0) {
				LOG_INF("Loaded ignore_power_switch: %s", current_settings.power.ignore_power_switch ? "true" : "false");
			}
			return 0;
		}

		if (!strncmp(name, "on_boot", name_len)) {
			if (len != sizeof(current_settings.power.on_boot)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, &current_settings.power.on_boot, 
				     sizeof(current_settings.power.on_boot));
			if (rc >= 0) {
				LOG_INF("Loaded on_boot: %s", current_settings.power.on_boot ? "true" : "false");
			}
			return 0;
		}

		if (!strncmp(name, "on_boot_delay", name_len)) {
			if (len != sizeof(current_settings.power.on_boot_delay)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, &current_settings.power.on_boot_delay, 
				     sizeof(current_settings.power.on_boot_delay));
			if (rc >= 0) {
				LOG_INF("Loaded on_boot_delay: %u", current_settings.power.on_boot_delay);
			}
			return 0;
		}

		if (!strncmp(name, "follow_usb", name_len)) {
			if (len != sizeof(current_settings.power.follow_usb)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, &current_settings.power.follow_usb, 
				     sizeof(current_settings.power.follow_usb));
			if (rc >= 0) {
				LOG_INF("Loaded follow_usb: %s", current_settings.power.follow_usb ? "true" : "false");
			}
			return 0;
		}

		if (!strncmp(name, "follow_usb_delay", name_len)) {
			if (len != sizeof(current_settings.power.follow_usb_delay)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, &current_settings.power.follow_usb_delay, 
				     sizeof(current_settings.power.follow_usb_delay));
			if (rc >= 0) {
				LOG_INF("Loaded follow_usb_delay: %u", current_settings.power.follow_usb_delay);
			}
			return 0;
		}
	}
	return -ENOENT;
}

static int power_settings_commit(void)
{
	LOG_INF("Power settings loaded successfully");
	return 0;
}

static int power_settings_export(int (*cb)(const char *name, const void *value, size_t val_len))
{
	LOG_INF("Exporting power settings");
	
	(void)cb("power/ignore_power_switch", &current_settings.power.ignore_power_switch, 
		 sizeof(current_settings.power.ignore_power_switch));
	(void)cb("power/on_boot", &current_settings.power.on_boot, 
		 sizeof(current_settings.power.on_boot));
	(void)cb("power/on_boot_delay", &current_settings.power.on_boot_delay, 
		 sizeof(current_settings.power.on_boot_delay));
	(void)cb("power/follow_usb", &current_settings.power.follow_usb, 
		 sizeof(current_settings.power.follow_usb));
	(void)cb("power/follow_usb_delay", &current_settings.power.follow_usb_delay, 
		 sizeof(current_settings.power.follow_usb_delay));

	return 0;
}

static int auth_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const char *next;
	size_t name_len;
	int rc;
	int user_idx;

	name_len = settings_name_next(name, &next);

	/* Handle old single-user format for migration compatibility */
	if (!next) {
		if (!strncmp(name, "username", name_len)) {
			if (len >= sizeof(current_settings.auth.users[0].username)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, current_settings.auth.users[0].username, len);
			if (rc >= 0) {
				current_settings.auth.users[0].username[len] = '\0';
				LOG_INF("Migrated old username to user slot 0: %s", current_settings.auth.users[0].username);
			}
			return 0;
		}
		if (!strncmp(name, "password_hash", name_len)) {
			if (len >= sizeof(current_settings.auth.users[0].password_hash)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, current_settings.auth.users[0].password_hash, len);
			if (rc >= 0) {
				current_settings.auth.users[0].password_hash[len] = '\0';
				LOG_INF("Migrated old password hash to user slot 0");
			}
			return 0;
		}
		if (!strncmp(name, "salt", name_len)) {
			if (len >= sizeof(current_settings.auth.users[0].salt)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, current_settings.auth.users[0].salt, len);
			if (rc >= 0) {
				current_settings.auth.users[0].salt[len] = '\0';
				LOG_INF("Migrated old salt to user slot 0");
			}
			return 0;
		}
	}

	/* Handle new multi-user format: user_X/... */
	if (next && sscanf(name, "user_%d", &user_idx) == 1 && user_idx >= 0 && user_idx < MAX_USERS) {
		const char *field_next;
		size_t field_name_len = settings_name_next(next, &field_next);

		if (!strncmp(next, "username", field_name_len)) {
			if (len >= sizeof(current_settings.auth.users[user_idx].username)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, current_settings.auth.users[user_idx].username, len);
			if (rc >= 0) {
				current_settings.auth.users[user_idx].username[len] = '\0';
				LOG_INF("Loaded user %d username: %s", user_idx, current_settings.auth.users[user_idx].username);
			}
			return 0;
		}

		if (!strncmp(next, "password_hash", field_name_len)) {
			if (len >= sizeof(current_settings.auth.users[user_idx].password_hash)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, current_settings.auth.users[user_idx].password_hash, len);
			if (rc >= 0) {
				current_settings.auth.users[user_idx].password_hash[len] = '\0';
				LOG_INF("Loaded user %d password hash", user_idx);
			}
			return 0;
		}

		if (!strncmp(next, "salt", field_name_len)) {
			if (len >= sizeof(current_settings.auth.users[user_idx].salt)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, current_settings.auth.users[user_idx].salt, len);
			if (rc >= 0) {
				current_settings.auth.users[user_idx].salt[len] = '\0';
				LOG_INF("Loaded user %d salt", user_idx);
			}
			return 0;
		}
	}

	return -ENOENT;
}

static int auth_settings_commit(void)
{
	LOG_INF("Auth settings loaded successfully");
	return 0;
}

static int auth_settings_export(int (*cb)(const char *name, const void *value, size_t val_len))
{
	LOG_INF("Exporting auth settings");
	
	char key[64];
	
	/* Export all user entries */
	for (int i = 0; i < MAX_USERS; i++) {
		if (strlen(current_settings.auth.users[i].username) > 0) {
			snprintf(key, sizeof(key), "auth/user_%d/username", i);
			(void)cb(key, current_settings.auth.users[i].username, 
				 strlen(current_settings.auth.users[i].username));
			
			snprintf(key, sizeof(key), "auth/user_%d/password_hash", i);
			(void)cb(key, current_settings.auth.users[i].password_hash, 
				 strlen(current_settings.auth.users[i].password_hash));
			
			snprintf(key, sizeof(key), "auth/user_%d/salt", i);
			(void)cb(key, current_settings.auth.users[i].salt, 
				 strlen(current_settings.auth.users[i].salt));
		}
	}

	return 0;
}

static int http_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const char *next;
	size_t name_len;
	int rc;
	bool new_value;

	name_len = settings_name_next(name, &next);

	if (!next) {
		if (!strncmp(name, "enable_http", name_len)) {
			if (len != sizeof(current_settings.http.enable_http)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, &new_value, sizeof(new_value));
			if (rc >= 0) {
				/* Validate that at least one server remains enabled */
				if (!new_value && !current_settings.http.enable_https) {
					LOG_WRN("Cannot disable HTTP server: HTTPS server is already disabled");
					return -EINVAL;
				}
				current_settings.http.enable_http = new_value;
				LOG_INF("Loaded enable_http: %s", current_settings.http.enable_http ? "true" : "false");
			}
			return 0;
		}
		if (!strncmp(name, "enable_https", name_len)) {
			if (len != sizeof(current_settings.http.enable_https)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, &new_value, sizeof(new_value));
			if (rc >= 0) {
				/* Validate that at least one server remains enabled */
				if (!new_value && !current_settings.http.enable_http) {
					LOG_WRN("Cannot disable HTTPS server: HTTP server is already disabled");
					return -EINVAL;
				}
				current_settings.http.enable_https = new_value;
				LOG_INF("Loaded enable_https: %s", current_settings.http.enable_https ? "true" : "false");
			}
			return 0;
		}
		if (!strncmp(name, "http_port", name_len)) {
			if (len != sizeof(current_settings.http.http_port)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, &current_settings.http.http_port, 
				     sizeof(current_settings.http.http_port));
			if (rc >= 0) {
				LOG_INF("Loaded http_port: %u", current_settings.http.http_port);
			}
			return 0;
		}
		if (!strncmp(name, "https_port", name_len)) {
			if (len != sizeof(current_settings.http.https_port)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, &current_settings.http.https_port, 
				     sizeof(current_settings.http.https_port));
			if (rc >= 0) {
				LOG_INF("Loaded https_port: %u", current_settings.http.https_port);
			}
			return 0;
		}
		if (!strncmp(name, "use_custom_certificates", name_len)) {
			if (len != sizeof(current_settings.http.use_custom_certificates)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, &current_settings.http.use_custom_certificates, 
				     sizeof(current_settings.http.use_custom_certificates));
			if (rc >= 0) {
				LOG_INF("Loaded use_custom_certificates: %s", current_settings.http.use_custom_certificates ? "true" : "false");
			}
			return 0;
		}
		if (!strncmp(name, "custom_certificate", name_len)) {
			if (len > sizeof(current_settings.http.custom_certificate)) {
				LOG_ERR("Certificate data too large: %zu > %zu", len, sizeof(current_settings.http.custom_certificate));
				return -EINVAL;
			}
			rc = read_cb(cb_arg, current_settings.http.custom_certificate, len);
			if (rc >= 0) {
				current_settings.http.custom_certificate[len] = '\0'; /* Ensure null termination */
				LOG_INF("Loaded custom_certificate: %zu chars", strlen(current_settings.http.custom_certificate));
			}
			return 0;
		}
		if (!strncmp(name, "custom_private_key", name_len)) {
			if (len > sizeof(current_settings.http.custom_private_key)) {
				LOG_ERR("Private key data too large: %zu > %zu", len, sizeof(current_settings.http.custom_private_key));
				return -EINVAL;
			}
			rc = read_cb(cb_arg, current_settings.http.custom_private_key, len);
			if (rc >= 0) {
				current_settings.http.custom_private_key[len] = '\0'; /* Ensure null termination */
				LOG_INF("Loaded custom_private_key: %zu chars", strlen(current_settings.http.custom_private_key));
			}
			return 0;
		}
	}

	return -ENOENT;
}

static int http_settings_commit(void)
{
	LOG_INF("HTTP settings loaded successfully");
	return 0;
}

static int http_settings_export(int (*cb)(const char *name, const void *value, size_t val_len))
{
	LOG_INF("Exporting HTTP settings");
	
	(void)cb("http/enable_http", &current_settings.http.enable_http, sizeof(current_settings.http.enable_http));
	(void)cb("http/enable_https", &current_settings.http.enable_https, sizeof(current_settings.http.enable_https));
	(void)cb("http/http_port", &current_settings.http.http_port, sizeof(current_settings.http.http_port));
	(void)cb("http/https_port", &current_settings.http.https_port, sizeof(current_settings.http.https_port));
	(void)cb("http/use_custom_certificates", &current_settings.http.use_custom_certificates, sizeof(current_settings.http.use_custom_certificates));
	
	/* Only export certificate data if it exists */
	if (strlen(current_settings.http.custom_certificate) > 0) {
		(void)cb("http/custom_certificate", current_settings.http.custom_certificate, strlen(current_settings.http.custom_certificate));
	}
	if (strlen(current_settings.http.custom_private_key) > 0) {
		(void)cb("http/custom_private_key", current_settings.http.custom_private_key, strlen(current_settings.http.custom_private_key));
	}

	return 0;
}

static int environment_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const char *next;
	size_t name_len;
	int rc;

	name_len = settings_name_next(name, &next);

	if (!next) {
		if (!strncmp(name, "use_external_fan_control", name_len)) {
			if (len != sizeof(current_settings.environment.use_external_fan_control)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, &current_settings.environment.use_external_fan_control, 
				     sizeof(current_settings.environment.use_external_fan_control));
			if (rc >= 0) {
				LOG_INF("Loaded use_external_fan_control: %s", 
					current_settings.environment.use_external_fan_control ? "true" : "false");
			}
			return 0;
		}
		if (!strncmp(name, "fan_update_interval_ms", name_len)) {
			if (len != sizeof(current_settings.environment.fan_update_interval_ms)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, &current_settings.environment.fan_update_interval_ms, 
				     sizeof(current_settings.environment.fan_update_interval_ms));
			if (rc >= 0) {
				LOG_INF("Loaded fan_update_interval_ms: %u", 
					current_settings.environment.fan_update_interval_ms);
			}
			return 0;
		}
		if (!strncmp(name, "fan_hysteresis_percent", name_len)) {
			if (len != sizeof(current_settings.environment.fan_hysteresis_percent)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, &current_settings.environment.fan_hysteresis_percent, 
				     sizeof(current_settings.environment.fan_hysteresis_percent));
			if (rc >= 0) {
				LOG_INF("Loaded fan_hysteresis_percent: %u", 
					current_settings.environment.fan_hysteresis_percent);
			}
			return 0;
		}
	}

	/* Handle fan curve points */
	if (!strncmp(name, "fan_curve", 9)) {
		const char *curve_next;
		settings_name_next(name, &curve_next);
		
		if (curve_next) {
			/* Parse curve index */
			int curve_idx = -1;
			if (sscanf(name, "fan_curve%d", &curve_idx) == 1 && curve_idx >= 0 && curve_idx < 5) {
				/* Parse temperature or fan_percent */
				if (!strncmp(curve_next, "temperature", strlen("temperature"))) {
					if (len != sizeof(current_settings.environment.fan_curve[curve_idx].temperature)) {
						return -EINVAL;
					}
					rc = read_cb(cb_arg, &current_settings.environment.fan_curve[curve_idx].temperature, 
						     sizeof(current_settings.environment.fan_curve[curve_idx].temperature));
					if (rc >= 0) {
						LOG_INF("Loaded fan_curve[%d].temperature: %.2f",
							curve_idx,
							(double)current_settings.environment.fan_curve[curve_idx].temperature);
					}
					return 0;
				}
				if (!strncmp(curve_next, "fan_percent", strlen("fan_percent"))) {
					if (len != sizeof(current_settings.environment.fan_curve[curve_idx].fan_percent)) {
						return -EINVAL;
					}
					rc = read_cb(cb_arg, &current_settings.environment.fan_curve[curve_idx].fan_percent, 
						     sizeof(current_settings.environment.fan_curve[curve_idx].fan_percent));
					if (rc >= 0) {
						LOG_INF("Loaded fan_curve[%d].fan_percent: %u", 
							curve_idx, current_settings.environment.fan_curve[curve_idx].fan_percent);
					}
					return 0;
				}
			}
		}
	}

	return -ENOENT;
}

static int environment_settings_commit(void)
{
	LOG_INF("Environment settings loaded successfully");
	return 0;
}

static int environment_settings_export(int (*cb)(const char *name, const void *value, size_t val_len))
{
	LOG_INF("Exporting environment settings");
	
	(void)cb("environment/use_external_fan_control", &current_settings.environment.use_external_fan_control, 
		 sizeof(current_settings.environment.use_external_fan_control));
	(void)cb("environment/fan_update_interval_ms", &current_settings.environment.fan_update_interval_ms, 
		 sizeof(current_settings.environment.fan_update_interval_ms));
	(void)cb("environment/fan_hysteresis_percent", &current_settings.environment.fan_hysteresis_percent, 
		 sizeof(current_settings.environment.fan_hysteresis_percent));
	
	/* Export fan curve points */
	for (int i = 0; i < 5; i++) {
		char temp_key[64];
		char percent_key[64];
		
		snprintf(temp_key, sizeof(temp_key), "environment/fan_curve%d/temperature", i);
		snprintf(percent_key, sizeof(percent_key), "environment/fan_curve%d/fan_percent", i);
		
		(void)cb(temp_key, &current_settings.environment.fan_curve[i].temperature, 
			 sizeof(current_settings.environment.fan_curve[i].temperature));
		(void)cb(percent_key, &current_settings.environment.fan_curve[i].fan_percent, 
			 sizeof(current_settings.environment.fan_curve[i].fan_percent));
	}

	return 0;
}

int openjbod_settings_init(void)
{
	int rc;

	LOG_INF("Initializing settings with FATFS file backend");

	rc = settings_subsys_init();
	if (rc) {
		LOG_ERR("Settings subsystem initialization failed: %d", rc);
		return rc;
	}

	LOG_INF("Settings subsystem initialized successfully");
	
	/* Test if FATFS file backend is accessible */
	int test_val = 42;
	rc = settings_save_one("test/init", &test_val, sizeof(test_val));
	if (rc) {
		LOG_WRN("FATFS file backend test write failed: %d", rc);
	} else {
		LOG_INF("FATFS file backend test write successful");
	}
	return 0;
}

int openjbod_settings_load_all(void)
{
	int rc;

	rc = settings_load();
	if (rc) {
		LOG_ERR("Settings load failed: %d", rc);
		return rc;
	}

	LOG_INF("Settings loaded: hostname=%s, ip_method=%d", 
		current_settings.network.hostname, current_settings.network.ip_method);
	return 0;
}

int openjbod_settings_save_all(void)
{
	int rc;

	rc = settings_save();
	if (rc) {
		LOG_ERR("Settings save failed: %d", rc);
		return rc;
	}

	LOG_INF("Settings saved to storage");
	return 0;
}

struct openjbod_settings *openjbod_settings_get(void)
{
	return &current_settings;
}

int openjbod_settings_set_network(const struct network_settings *net)
{
	if (!net) {
		return -EINVAL;
	}

	memcpy(&current_settings.network, net, sizeof(struct network_settings));
	
	/* Save individual settings */
	int rc;
	
	rc = settings_save_one("network/ip_method", &net->ip_method, sizeof(net->ip_method));
	if (rc) {
		LOG_ERR("Failed to save ip_method: %d", rc);
		return rc;
	}
	
	rc = settings_save_one("network/ip_addr", net->ip_addr, strlen(net->ip_addr));
	if (rc) {
		LOG_ERR("Failed to save ip_addr: %d", rc);
		return rc;
	}
	
	rc = settings_save_one("network/gw_addr", net->gw_addr, strlen(net->gw_addr));
	if (rc) {
		LOG_ERR("Failed to save gw_addr: %d", rc);
		return rc;
	}
	
	rc = settings_save_one("network/ip_mask", net->ip_mask, strlen(net->ip_mask));
	if (rc) {
		LOG_ERR("Failed to save ip_mask: %d", rc);
		return rc;
	}
	
	rc = settings_save_one("network/dns1", net->dns1, strlen(net->dns1));
	if (rc) {
		LOG_ERR("Failed to save dns1: %d", rc);
		return rc;
	}
	
	rc = settings_save_one("network/hostname", net->hostname, strlen(net->hostname));
	if (rc) {
		LOG_ERR("Failed to save hostname: %d", rc);
		return rc;
	}

	LOG_INF("Network settings saved: hostname=%s, ip_method=%d", net->hostname, net->ip_method);

	rc = settings_save_one("network/ipv6_mode", &net->ipv6_mode,
			       sizeof(net->ipv6_mode));
	if (rc) {
		LOG_ERR("Failed to save ipv6_mode: %d", rc);
		return rc;
	}

	rc = settings_save_one("network/ipv6_addr", net->ipv6_addr,
			       strlen(net->ipv6_addr));
	if (rc) {
		LOG_ERR("Failed to save ipv6_addr: %d", rc);
		return rc;
	}

	rc = settings_save_one("network/ipv6_prefix_length",
			       &net->ipv6_prefix_length,
			       sizeof(net->ipv6_prefix_length));
	if (rc) {
		LOG_ERR("Failed to save ipv6_prefix_length: %d", rc);
		return rc;
	}

	rc = settings_save_one("network/ipv6_gateway", net->ipv6_gateway,
			       strlen(net->ipv6_gateway));
	if (rc) {
		LOG_ERR("Failed to save ipv6_gateway: %d", rc);
		return rc;
	}

	rc = settings_save_one("network/ipv6_dns1", net->ipv6_dns1,
			       strlen(net->ipv6_dns1));
	if (rc) {
		LOG_ERR("Failed to save ipv6_dns1: %d", rc);
		return rc;
	}

	LOG_INF("IPv6 settings saved: mode=%d, addr=%s/%u", net->ipv6_mode,
		net->ipv6_addr, net->ipv6_prefix_length);
	
	/* Try to immediately reload to verify persistence */
	rc = settings_load();
	if (rc) {
		LOG_WRN("Failed to reload settings for verification: %d", rc);
	} else {
		LOG_INF("Verification load: hostname=%s, ip_method=%d", 
			current_settings.network.hostname, current_settings.network.ip_method);
	}
	
	return 0;
}

int openjbod_settings_set_power(const struct power_settings *power)
{
	if (!power) {
		return -EINVAL;
	}

	memcpy(&current_settings.power, power, sizeof(struct power_settings));
	
	/* Save individual settings */
	int rc;
	
	rc = settings_save_one("power/ignore_power_switch", &power->ignore_power_switch, sizeof(power->ignore_power_switch));
	if (rc) {
		LOG_ERR("Failed to save ignore_power_switch: %d", rc);
		return rc;
	}
	
	rc = settings_save_one("power/on_boot", &power->on_boot, sizeof(power->on_boot));
	if (rc) {
		LOG_ERR("Failed to save on_boot: %d", rc);
		return rc;
	}
	
	rc = settings_save_one("power/on_boot_delay", &power->on_boot_delay, sizeof(power->on_boot_delay));
	if (rc) {
		LOG_ERR("Failed to save on_boot_delay: %d", rc);
		return rc;
	}
	
	rc = settings_save_one("power/follow_usb", &power->follow_usb, sizeof(power->follow_usb));
	if (rc) {
		LOG_ERR("Failed to save follow_usb: %d", rc);
		return rc;
	}
	
	rc = settings_save_one("power/follow_usb_delay", &power->follow_usb_delay, sizeof(power->follow_usb_delay));
	if (rc) {
		LOG_ERR("Failed to save follow_usb_delay: %d", rc);
		return rc;
	}

	LOG_INF("Power settings saved: ignore_power_switch=%s, on_boot=%s, on_boot_delay=%u, follow_usb=%s, follow_usb_delay=%u", 
		power->ignore_power_switch ? "true" : "false",
		power->on_boot ? "true" : "false", power->on_boot_delay,
		power->follow_usb ? "true" : "false", power->follow_usb_delay);
	
	/* Try to immediately reload to verify persistence */
	rc = settings_load();
	if (rc) {
		LOG_WRN("Failed to reload settings for verification: %d", rc);
	} else {
		LOG_INF("Verification load successful");
	}
	
	return 0;
}

int openjbod_settings_set_http(const struct http_settings *http)
{
	if (!http) {
		return -EINVAL;
	}

	memcpy(&current_settings.http, http, sizeof(struct http_settings));
	
	/* Save individual settings */
	int rc;
	
	rc = settings_save_one("http/enable_http", &http->enable_http, sizeof(http->enable_http));
	if (rc) {
		LOG_ERR("Failed to save enable_http: %d", rc);
		return rc;
	}
	
	rc = settings_save_one("http/enable_https", &http->enable_https, sizeof(http->enable_https));
	if (rc) {
		LOG_ERR("Failed to save enable_https: %d", rc);
		return rc;
	}
	
	rc = settings_save_one("http/http_port", &http->http_port, sizeof(http->http_port));
	if (rc) {
		LOG_ERR("Failed to save http_port: %d", rc);
		return rc;
	}
	
	rc = settings_save_one("http/https_port", &http->https_port, sizeof(http->https_port));
	if (rc) {
		LOG_ERR("Failed to save https_port: %d", rc);
		return rc;
	}
	
	rc = settings_save_one("http/use_custom_certificates", &http->use_custom_certificates, sizeof(http->use_custom_certificates));
	if (rc) {
		LOG_ERR("Failed to save use_custom_certificates: %d", rc);
		return rc;
	}
	
	/* Save certificate data if it exists */
	if (strlen(http->custom_certificate) > 0) {
		rc = settings_save_one("http/custom_certificate", http->custom_certificate, strlen(http->custom_certificate));
		if (rc) {
			LOG_ERR("Failed to save custom_certificate: %d", rc);
			return rc;
		}
		LOG_INF("Saved custom certificate (%zu chars)", strlen(http->custom_certificate));
	}
	
	if (strlen(http->custom_private_key) > 0) {
		rc = settings_save_one("http/custom_private_key", http->custom_private_key, strlen(http->custom_private_key));
		if (rc) {
			LOG_ERR("Failed to save custom_private_key: %d", rc);
			return rc;
		}
		LOG_INF("Saved custom private key (%zu chars)", strlen(http->custom_private_key));
	}

	LOG_INF("HTTP settings saved: enable_http=%s, enable_https=%s, http_port=%u, https_port=%u, use_custom_certificates=%s", 
		http->enable_http ? "true" : "false",
		http->enable_https ? "true" : "false", 
		http->http_port, http->https_port,
		http->use_custom_certificates ? "true" : "false");
	
	/* Try to immediately reload to verify persistence */
	rc = settings_load();
	if (rc) {
		LOG_WRN("Failed to reload settings for verification: %d", rc);
	} else {
		LOG_INF("Verification load successful");
	}
	
	return 0;
}

static int generate_salt(char *salt_buf, size_t salt_len)
{
	uint8_t random_bytes[SALT_LEN];
	int rc;

	rc = sys_csrand_get(random_bytes, sizeof(random_bytes));
	if (rc) {
		LOG_ERR("Failed to generate random salt: %d", rc);
		return rc;
	}

	/* Convert to hex string */
	for (int i = 0; i < SALT_LEN && i * 2 < salt_len - 1; i++) {
		snprintf(&salt_buf[i * 2], 3, "%02x", random_bytes[i]);
	}
	salt_buf[salt_len - 1] = '\0';

	return 0;
}

static int hash_password_with_salt(const char *password, const char *salt, char *hash_output, size_t hash_len)
{
	psa_status_t status;
	psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
	uint8_t hash[PSA_HASH_LENGTH(PSA_ALG_SHA_256)];
	size_t hash_output_len;
	size_t b64_output_len;

	/* Initialize PSA crypto */
	status = psa_crypto_init();
	if (status != PSA_SUCCESS) {
		LOG_ERR("PSA crypto initialization failed: %d", status);
		return -EIO;
	}

	/* Start hash operation */
	status = psa_hash_setup(&operation, PSA_ALG_SHA_256);
	if (status != PSA_SUCCESS) {
		LOG_ERR("Hash setup failed: %d", status);
		return -EIO;
	}

	/* Update with salt */
	status = psa_hash_update(&operation, (const uint8_t*)salt, strlen(salt));
	if (status != PSA_SUCCESS) {
		LOG_ERR("Hash update with salt failed: %d", status);
		psa_hash_abort(&operation);
		return -EIO;
	}

	/* Update with password */
	status = psa_hash_update(&operation, (const uint8_t*)password, strlen(password));
	if (status != PSA_SUCCESS) {
		LOG_ERR("Hash update with password failed: %d", status);
		psa_hash_abort(&operation);
		return -EIO;
	}

	/* Finish hash */
	status = psa_hash_finish(&operation, hash, sizeof(hash), &hash_output_len);
	if (status != PSA_SUCCESS) {
		LOG_ERR("Hash finish failed: %d", status);
		return -EIO;
	}

	/* Encode to base64 */
	int rc = base64_encode((uint8_t*)hash_output, hash_len - 1, &b64_output_len,
			       (const uint8_t*)hash, hash_output_len);
	if (rc != 0) {
		LOG_ERR("Base64 encoding failed: %d", rc);
		return -EIO;
	}

	/* Ensure null termination */
	hash_output[b64_output_len] = '\0';

	return 0;
}

/* Find user by username, returns index or -1 if not found */
static int find_user_by_username(const char *username)
{
	for (int i = 0; i < MAX_USERS; i++) {
		if (strlen(current_settings.auth.users[i].username) > 0 &&
		    strcmp(current_settings.auth.users[i].username, username) == 0) {
			return i;
		}
	}
	return -1;
}

/* Find empty user slot, returns index or -1 if full */
static int find_empty_user_slot(void)
{
	for (int i = 0; i < MAX_USERS; i++) {
		if (strlen(current_settings.auth.users[i].username) == 0) {
			return i;
		}
	}
	return -1;
}

/* Save individual user to persistent storage */
int openjbod_settings_save_user(int user_idx, const struct user_entry *user)
{
	if (user_idx < 0 || user_idx >= MAX_USERS || !user) {
		return -EINVAL;
	}

	char key[64];
	int rc;

	/* Save username */
	snprintf(key, sizeof(key), "auth/user_%d/username", user_idx);
	rc = settings_save_one(key, user->username, strlen(user->username));
	if (rc) {
		LOG_ERR("Failed to save username for user %d: %d", user_idx, rc);
		return rc;
	}

	/* Save password hash */
	snprintf(key, sizeof(key), "auth/user_%d/password_hash", user_idx);
	rc = settings_save_one(key, user->password_hash, strlen(user->password_hash));
	if (rc) {
		LOG_ERR("Failed to save password_hash for user %d: %d", user_idx, rc);
		return rc;
	}

	/* Save salt */
	snprintf(key, sizeof(key), "auth/user_%d/salt", user_idx);
	rc = settings_save_one(key, user->salt, strlen(user->salt));
	if (rc) {
		LOG_ERR("Failed to save salt for user %d: %d", user_idx, rc);
		return rc;
	}

	return 0;
}

int openjbod_settings_set_auth(const struct auth_settings *auth)
{
	if (!auth) {
		return -EINVAL;
	}

	memcpy(&current_settings.auth, auth, sizeof(struct auth_settings));
	
	/* Save all users */
	int rc;
	for (int i = 0; i < MAX_USERS; i++) {
		rc = openjbod_settings_save_user(i, &auth->users[i]);
		if (rc) {
			LOG_ERR("Failed to save user %d: %d", i, rc);
			return rc;
		}
	}

	LOG_INF("Auth settings saved for all users");
	
	/* Try to immediately reload to verify persistence */
	rc = settings_load();
	if (rc) {
		LOG_WRN("Failed to reload settings for verification: %d", rc);
	} else {
		LOG_INF("Verification load successful");
	}
	
	return 0;
}

int openjbod_auth_verify_credentials(const char *username, const char *password)
{
	char calculated_hash[PASSWORD_HASH_LEN];
	int rc;
	int user_idx;

	if (!username || !password) {
		return -EINVAL;
	}

	/* Check if any users exist, if not create default admin user */
	bool has_users = false;
	for (int i = 0; i < MAX_USERS; i++) {
		if (strlen(current_settings.auth.users[i].username) > 0) {
			has_users = true;
			break;
		}
	}

	if (!has_users) {
		LOG_INF("No users configured, setting up default admin user");
		
		/* Generate salt for default password */
		char salt[SALT_STR_LEN];
		rc = generate_salt(salt, sizeof(salt));
		if (rc) {
			LOG_ERR("Failed to generate salt: %d", rc);
			return rc;
		}

		/* Hash the default password "openjbod" */
		rc = hash_password_with_salt("openjbod", salt, calculated_hash, sizeof(calculated_hash));
		if (rc) {
			LOG_ERR("Failed to hash default password: %d", rc);
			return rc;
		}

		/* Set up default admin user in slot 0 */
		strncpy(current_settings.auth.users[0].username, "admin", sizeof(current_settings.auth.users[0].username) - 1);
		strncpy(current_settings.auth.users[0].password_hash, calculated_hash, sizeof(current_settings.auth.users[0].password_hash) - 1);
		strncpy(current_settings.auth.users[0].salt, salt, sizeof(current_settings.auth.users[0].salt) - 1);
		current_settings.auth.users[0].username[sizeof(current_settings.auth.users[0].username) - 1] = '\0';
		current_settings.auth.users[0].password_hash[sizeof(current_settings.auth.users[0].password_hash) - 1] = '\0';
		current_settings.auth.users[0].salt[sizeof(current_settings.auth.users[0].salt) - 1] = '\0';

		/* Save to persistent storage */
		rc = openjbod_settings_save_user(0, &current_settings.auth.users[0]);
		if (rc) {
			LOG_ERR("Failed to save default user: %d", rc);
			return rc;
		}
	}

	/* Find user by username */
	user_idx = find_user_by_username(username);
	if (user_idx < 0) {
		LOG_WRN("Authentication failed: user '%s' not found", username);
		return -EACCES;
	}

	/* Hash provided password with stored salt */
	rc = hash_password_with_salt(password, current_settings.auth.users[user_idx].salt, calculated_hash, sizeof(calculated_hash));
	if (rc) {
		LOG_ERR("Failed to hash provided password: %d", rc);
		return rc;
	}

	/* Compare hashes */
	if (strcmp(calculated_hash, current_settings.auth.users[user_idx].password_hash) != 0) {
		LOG_WRN("Authentication failed: invalid password for user '%s'", username);
		return -EACCES;
	}

	LOG_INF("Authentication successful for user: %s", username);
	return 0;
}

int openjbod_settings_delete_user(int user_idx)
{
	if (user_idx < 0 || user_idx >= MAX_USERS) {
		return -EINVAL;
	}

	/* Clear user entry */
	memset(&current_settings.auth.users[user_idx], 0, sizeof(struct user_entry));

	/* Save the cleared entry to persistent storage */
	return openjbod_settings_save_user(user_idx, &current_settings.auth.users[user_idx]);
}

int openjbod_settings_create_user(const char *username, const char *password)
{
	if (!username || !password || strlen(username) == 0) {
		return -EINVAL;
	}

	/* Check if user already exists */
	if (find_user_by_username(username) >= 0) {
		LOG_WRN("User '%s' already exists", username);
		return -EEXIST;
	}

	/* Find empty slot */
	int user_idx = find_empty_user_slot();
	if (user_idx < 0) {
		LOG_WRN("No empty user slots available");
		return -ENOSPC;
	}

	/* Generate salt */
	char salt[SALT_STR_LEN];
	int rc = generate_salt(salt, sizeof(salt));
	if (rc) {
		LOG_ERR("Failed to generate salt: %d", rc);
		return rc;
	}

	/* Hash password */
	char password_hash[PASSWORD_HASH_LEN];
	rc = hash_password_with_salt(password, salt, password_hash, sizeof(password_hash));
	if (rc) {
		LOG_ERR("Failed to hash password: %d", rc);
		return rc;
	}

	/* Create user entry */
	struct user_entry new_user;
	strncpy(new_user.username, username, sizeof(new_user.username) - 1);
	strncpy(new_user.password_hash, password_hash, sizeof(new_user.password_hash) - 1);
	strncpy(new_user.salt, salt, sizeof(new_user.salt) - 1);
	new_user.username[sizeof(new_user.username) - 1] = '\0';
	new_user.password_hash[sizeof(new_user.password_hash) - 1] = '\0';
	new_user.salt[sizeof(new_user.salt) - 1] = '\0';

	/* Update current settings */
	memcpy(&current_settings.auth.users[user_idx], &new_user, sizeof(struct user_entry));

	/* Save to persistent storage */
	rc = openjbod_settings_save_user(user_idx, &new_user);
	if (rc) {
		LOG_ERR("Failed to save new user: %d", rc);
		return rc;
	}

	LOG_INF("Created user: %s in slot %d", username, user_idx);
	return user_idx;
}

int openjbod_settings_set_environment(const struct environment_settings *environment)
{
	if (!environment) {
		return -EINVAL;
	}

	memcpy(&current_settings.environment, environment, sizeof(struct environment_settings));
	
	/* Save individual settings */
	int rc;
	
	rc = settings_save_one("environment/use_external_fan_control", &environment->use_external_fan_control, 
			       sizeof(environment->use_external_fan_control));
	if (rc) {
		LOG_ERR("Failed to save use_external_fan_control: %d", rc);
		return rc;
	}
	
	rc = settings_save_one("environment/fan_update_interval_ms", &environment->fan_update_interval_ms, 
			       sizeof(environment->fan_update_interval_ms));
	if (rc) {
		LOG_ERR("Failed to save fan_update_interval_ms: %d", rc);
		return rc;
	}
	
	rc = settings_save_one("environment/fan_hysteresis_percent", &environment->fan_hysteresis_percent, 
			       sizeof(environment->fan_hysteresis_percent));
	if (rc) {
		LOG_ERR("Failed to save fan_hysteresis_percent: %d", rc);
		return rc;
	}
	
	/* Save fan curve points */
	for (int i = 0; i < 5; i++) {
		char temp_key[64];
		char percent_key[64];
		
		snprintf(temp_key, sizeof(temp_key), "environment/fan_curve%d/temperature", i);
		snprintf(percent_key, sizeof(percent_key), "environment/fan_curve%d/fan_percent", i);
		
		rc = settings_save_one(temp_key, &environment->fan_curve[i].temperature, 
				       sizeof(environment->fan_curve[i].temperature));
		if (rc) {
			LOG_ERR("Failed to save fan_curve[%d].temperature: %d", i, rc);
			return rc;
		}
		
		rc = settings_save_one(percent_key, &environment->fan_curve[i].fan_percent, 
				       sizeof(environment->fan_curve[i].fan_percent));
		if (rc) {
			LOG_ERR("Failed to save fan_curve[%d].fan_percent: %d", i, rc);
			return rc;
		}
	}

	LOG_INF("Environment settings saved: use_external_fan_control=%s, fan_update_interval_ms=%u, fan_hysteresis_percent=%u", 
		environment->use_external_fan_control ? "true" : "false",
		environment->fan_update_interval_ms, 
		environment->fan_hysteresis_percent);
	
	/* Try to immediately reload to verify persistence */
	rc = settings_load();
	if (rc) {
		LOG_WRN("Failed to reload settings for verification: %d", rc);
	} else {
		LOG_INF("Verification load successful");
	}
	
	return 0;
}

int openjbod_settings_reset_all(void)
{
	int rc = fs_unlink(CONFIG_SETTINGS_FILE_PATH);
	if (rc < 0 && rc != -ENOENT) {
		LOG_ERR("Failed to remove %s: %d", CONFIG_SETTINGS_FILE_PATH, rc);
		return rc;
	}

	current_settings = default_settings;
	LOG_INF("Settings reset to defaults in memory and %s removed", CONFIG_SETTINGS_FILE_PATH);

	return 0;
}

int openjbod_settings_update_user_password(const char *username, const char *new_password)
{
	if (!username || !new_password) {
		return -EINVAL;
	}

	/* Find user */
	int user_idx = find_user_by_username(username);
	if (user_idx < 0) {
		LOG_WRN("User '%s' not found", username);
		return -ENOENT;
	}

	/* Generate new salt */
	char salt[SALT_STR_LEN];
	int rc = generate_salt(salt, sizeof(salt));
	if (rc) {
		LOG_ERR("Failed to generate salt: %d", rc);
		return rc;
	}

	/* Hash new password */
	char password_hash[PASSWORD_HASH_LEN];
	rc = hash_password_with_salt(new_password, salt, password_hash, sizeof(password_hash));
	if (rc) {
		LOG_ERR("Failed to hash password: %d", rc);
		return rc;
	}

	/* Update user entry */
	strncpy(current_settings.auth.users[user_idx].password_hash, password_hash, 
		sizeof(current_settings.auth.users[user_idx].password_hash) - 1);
	strncpy(current_settings.auth.users[user_idx].salt, salt, 
		sizeof(current_settings.auth.users[user_idx].salt) - 1);
	current_settings.auth.users[user_idx].password_hash[sizeof(current_settings.auth.users[user_idx].password_hash) - 1] = '\0';
	current_settings.auth.users[user_idx].salt[sizeof(current_settings.auth.users[user_idx].salt) - 1] = '\0';

	/* Save to persistent storage */
	rc = openjbod_settings_save_user(user_idx, &current_settings.auth.users[user_idx]);
	if (rc) {
		LOG_ERR("Failed to save updated user: %d", rc);
		return rc;
	}

	LOG_INF("Updated password for user: %s", username);
	return 0;
}
