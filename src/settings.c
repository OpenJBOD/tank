/*
 * Copyright (c) 2024 OpenJBOD Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
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
		.primary_temp_source = 0,        /* 0 = onboard DS18B20 (GPIO18) */             \
		.fan_curve = {                                                                 \
			{20.0f, 0},    /* Below 20°C: 0% fan speed */                                 \
			{30.0f, 20},   /* 30°C: 20% fan speed */                                      \
			{40.0f, 40},   /* 40°C: 40% fan speed */                                      \
			{50.0f, 70},   /* 50°C: 70% fan speed */                                      \
			{60.0f, 100}   /* 60°C and above: 100% fan speed */                            \
		}                                                                               \
	},                                                                                  \
	.console = {                                                                        \
		.uart_enabled = true,                                                           \
		.usb_enabled = true                                                             \
	}                                                                                   \
}

static const struct openjbod_settings default_settings = OPENJBOD_DEFAULT_SETTINGS_INITIALIZER;
static struct openjbod_settings current_settings = OPENJBOD_DEFAULT_SETTINGS_INITIALIZER;

/* Replace a heap-allocated settings string, freeing the previous value and taking
 * ownership of newbuf (which may be NULL to clear). Used for the optional custom
 * TLS certificate/key, which are kept off the always-resident settings struct.
 */
static void http_take_str(char **field, char *newbuf)
{
	free(*field);
	*field = newbuf;
}

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

static int console_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg);
static int console_settings_commit(void);
static int console_settings_export(int (*cb)(const char *name, const void *value, size_t val_len));

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

SETTINGS_STATIC_HANDLER_DEFINE(console, "console", NULL, console_settings_set,
			       console_settings_commit, console_settings_export);

/* ---- Table-driven settings fields -----------------------------------------
 * Most settings groups are just a list of flat scalar/string fields. Describe
 * them in a table and load/export them generically instead of repeating a
 * strncmp chain and an export list per field. (Nested/variable fields - the
 * fan curve, the heap-allocated certs, and the auth user array - are still
 * handled explicitly by their group handlers.)
 */
enum sfield_type { SF_BLOB, SF_STR };

struct sfield {
	const char *name;   /* leaf key under the group */
	size_t offset;      /* offset into struct openjbod_settings */
	size_t size;        /* sizeof the field */
	enum sfield_type type;
};

#define SF_BLOB_ENTRY(grp, fld) \
	{ #fld, offsetof(struct openjbod_settings, grp.fld), \
	  sizeof(((struct openjbod_settings *)0)->grp.fld), SF_BLOB }
#define SF_STR_ENTRY(grp, fld) \
	{ #fld, offsetof(struct openjbod_settings, grp.fld), \
	  sizeof(((struct openjbod_settings *)0)->grp.fld), SF_STR }

/* Load one leaf via the table. Returns 0 if handled, -ENOENT if not in table. */
static int sfield_set(const struct sfield *tbl, size_t n, const char *name,
		      size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const char *next;
	size_t nlen = settings_name_next(name, &next);

	if (next) {
		return -ENOENT;  /* nested key (e.g. fan_curveN/...) - caller handles */
	}

	for (size_t i = 0; i < n; i++) {
		if (strlen(tbl[i].name) != nlen || strncmp(name, tbl[i].name, nlen) != 0) {
			continue;
		}

		void *dst = (uint8_t *)&current_settings + tbl[i].offset;

		if (tbl[i].type == SF_BLOB) {
			if (len != tbl[i].size) {
				return -EINVAL;
			}
			return read_cb(cb_arg, dst, tbl[i].size) >= 0 ? 0 : -EIO;
		}

		/* SF_STR: stored without a trailing NUL; read len bytes and terminate. */
		if (len >= tbl[i].size) {
			return -EINVAL;
		}
		int rc = read_cb(cb_arg, dst, len);
		if (rc >= 0) {
			((char *)dst)[len] = '\0';
		}
		return rc >= 0 ? 0 : rc;
	}

	return -ENOENT;
}

/* Export all table fields under "<group>/<leaf>". */
static int sfield_export(const char *group, const struct sfield *tbl, size_t n,
			 int (*cb)(const char *name, const void *value, size_t val_len))
{
	char key[64];

	for (size_t i = 0; i < n; i++) {
		const void *src = (const uint8_t *)&current_settings + tbl[i].offset;
		size_t vlen = (tbl[i].type == SF_STR) ? strlen((const char *)src) : tbl[i].size;

		snprintf(key, sizeof(key), "%s/%s", group, tbl[i].name);
		(void)cb(key, src, vlen);
	}
	return 0;
}

static const struct sfield network_fields[] = {
	SF_BLOB_ENTRY(network, ip_method),
	SF_STR_ENTRY(network, ip_addr),
	SF_STR_ENTRY(network, gw_addr),
	SF_STR_ENTRY(network, ip_mask),
	SF_STR_ENTRY(network, dns1),
	SF_STR_ENTRY(network, hostname),
	SF_BLOB_ENTRY(network, ipv6_mode),
	SF_STR_ENTRY(network, ipv6_addr),
	SF_BLOB_ENTRY(network, ipv6_prefix_length),
	SF_STR_ENTRY(network, ipv6_gateway),
	SF_STR_ENTRY(network, ipv6_dns1),
};

static const struct sfield power_fields[] = {
	SF_BLOB_ENTRY(power, ignore_power_switch),
	SF_BLOB_ENTRY(power, on_boot),
	SF_BLOB_ENTRY(power, on_boot_delay),
	SF_BLOB_ENTRY(power, follow_usb),
	SF_BLOB_ENTRY(power, follow_usb_delay),
};

/* http is not table-driven: enable_http/enable_https carry load-time validation
 * (can't disable both) and custom_certificate/_private_key are heap-allocated. */

static const struct sfield environment_fields[] = {
	SF_BLOB_ENTRY(environment, use_external_fan_control),
	SF_BLOB_ENTRY(environment, fan_update_interval_ms),
	SF_BLOB_ENTRY(environment, fan_hysteresis_percent),
	SF_BLOB_ENTRY(environment, primary_temp_source),
};

static const struct sfield console_fields[] = {
	SF_BLOB_ENTRY(console, uart_enabled),
	SF_BLOB_ENTRY(console, usb_enabled),
};

static int network_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	return sfield_set(network_fields, ARRAY_SIZE(network_fields), name, len, read_cb, cb_arg);
}

static int network_settings_commit(void)
{
	LOG_INF("Network settings loaded successfully");
	return 0;
}

static int network_settings_export(int (*cb)(const char *name, const void *value, size_t val_len))
{
	return sfield_export("network", network_fields, ARRAY_SIZE(network_fields), cb);
}

static int power_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	return sfield_set(power_fields, ARRAY_SIZE(power_fields), name, len, read_cb, cb_arg);
}

static int power_settings_commit(void)
{
	LOG_INF("Power settings loaded successfully");
	return 0;
}

static int power_settings_export(int (*cb)(const char *name, const void *value, size_t val_len))
{
	return sfield_export("power", power_fields, ARRAY_SIZE(power_fields), cb);
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
				LOG_DBG("Loaded user %d username: %s", user_idx, current_settings.auth.users[user_idx].username);
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
				LOG_DBG("Loaded user %d password hash", user_idx);
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
				LOG_DBG("Loaded user %d salt", user_idx);
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
				LOG_DBG("Loaded enable_http: %s", current_settings.http.enable_http ? "true" : "false");
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
				LOG_DBG("Loaded enable_https: %s", current_settings.http.enable_https ? "true" : "false");
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
				LOG_DBG("Loaded http_port: %u", current_settings.http.http_port);
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
				LOG_DBG("Loaded https_port: %u", current_settings.http.https_port);
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
				LOG_DBG("Loaded use_custom_certificates: %s", current_settings.http.use_custom_certificates ? "true" : "false");
			}
			return 0;
		}
		if (!strncmp(name, "custom_certificate", name_len)) {
			if (len >= CERTIFICATE_HEX_MAX_LEN) {
				LOG_ERR("Certificate data too large: %zu >= %d", len, CERTIFICATE_HEX_MAX_LEN);
				return -EINVAL;
			}
			char *buf = malloc(len + 1);
			if (buf == NULL) {
				return -ENOMEM;
			}
			rc = read_cb(cb_arg, buf, len);
			if (rc < 0) {
				free(buf);
				return rc;
			}
			buf[rc] = '\0';
			http_take_str(&current_settings.http.custom_certificate, buf);
			LOG_DBG("Loaded custom_certificate: %d chars", rc);
			return 0;
		}
		if (!strncmp(name, "custom_private_key", name_len)) {
			if (len >= PRIVATE_KEY_HEX_MAX_LEN) {
				LOG_ERR("Private key data too large: %zu >= %d", len, PRIVATE_KEY_HEX_MAX_LEN);
				return -EINVAL;
			}
			char *buf = malloc(len + 1);
			if (buf == NULL) {
				return -ENOMEM;
			}
			rc = read_cb(cb_arg, buf, len);
			if (rc < 0) {
				free(buf);
				return rc;
			}
			buf[rc] = '\0';
			http_take_str(&current_settings.http.custom_private_key, buf);
			LOG_DBG("Loaded custom_private_key: %d chars", rc);
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
	if (current_settings.http.custom_certificate != NULL &&
	    current_settings.http.custom_certificate[0] != '\0') {
		(void)cb("http/custom_certificate", current_settings.http.custom_certificate,
			 strlen(current_settings.http.custom_certificate));
	}
	if (current_settings.http.custom_private_key != NULL &&
	    current_settings.http.custom_private_key[0] != '\0') {
		(void)cb("http/custom_private_key", current_settings.http.custom_private_key,
			 strlen(current_settings.http.custom_private_key));
	}

	return 0;
}

static int environment_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	int rc;

	rc = sfield_set(environment_fields, ARRAY_SIZE(environment_fields), name, len, read_cb, cb_arg);
	if (rc != -ENOENT) {
		return rc;
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
						LOG_DBG("Loaded fan_curve[%d].temperature: %.2f",
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
						LOG_DBG("Loaded fan_curve[%d].fan_percent: %u", 
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
	sfield_export("environment", environment_fields, ARRAY_SIZE(environment_fields), cb);

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

static int console_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	return sfield_set(console_fields, ARRAY_SIZE(console_fields), name, len, read_cb, cb_arg);
}

static int console_settings_commit(void)
{
	LOG_INF("Console settings loaded successfully");
	return 0;
}

static int console_settings_export(int (*cb)(const char *name, const void *value, size_t val_len))
{
	return sfield_export("console", console_fields, ARRAY_SIZE(console_fields), cb);
}

static int ensure_default_user(void);

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

	/* Initialize PSA crypto once here (used for password hashing) rather than
	 * on every hash call. psa_crypto_init() is idempotent.
	 */
	psa_status_t ps = psa_crypto_init();
	if (ps != PSA_SUCCESS) {
		LOG_WRN("PSA crypto init returned %d", (int)ps);
	}

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
		/* Continue with in-RAM defaults; still provision a default user below. */
	} else {
		LOG_INF("Settings loaded: hostname=%s, ip_method=%d",
			current_settings.network.hostname, current_settings.network.ip_method);
	}

	/* Provision the default admin user once, here, rather than as a surprising
	 * side effect of the first credential check.
	 */
	ensure_default_user();
	return rc;
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

/* Persist a fixed-size value only if it differs from the cached copy, updating
 * the cache on success. Avoids rewriting (and reloading) unchanged keys on every
 * settings POST, which previously rewrote every field of a group and then reloaded
 * the entire settings tree from the FAT file.
 */
static int save_blob_if_changed(const char *key, const void *new_v, void *cur_v, size_t sz)
{
	if (memcmp(new_v, cur_v, sz) == 0) {
		return 0;
	}
	int rc = settings_save_one(key, new_v, sz);
	if (rc) {
		LOG_ERR("Failed to save %s: %d", key, rc);
		return rc;
	}
	memcpy(cur_v, new_v, sz);
	return 0;
}

/* As save_blob_if_changed, for NUL-terminated strings. Stored without the trailing
 * NUL to match the load handlers (which copy len bytes and terminate).
 */
static int save_str_if_changed(const char *key, const char *new_s, char *cur_s, size_t cap)
{
	if (strcmp(new_s, cur_s) == 0) {
		return 0;
	}
	int rc = settings_save_one(key, new_s, strlen(new_s));
	if (rc) {
		LOG_ERR("Failed to save %s: %d", key, rc);
		return rc;
	}
	strncpy(cur_s, new_s, cap - 1);
	cur_s[cap - 1] = '\0';
	return 0;
}

int openjbod_settings_set_network(const struct network_settings *net)
{
	if (!net) {
		return -EINVAL;
	}

	struct network_settings *cur = &current_settings.network;
	int rc;

	if ((rc = save_blob_if_changed("network/ip_method", &net->ip_method, &cur->ip_method, sizeof(cur->ip_method))) != 0 ||
	    (rc = save_str_if_changed("network/ip_addr", net->ip_addr, cur->ip_addr, sizeof(cur->ip_addr))) != 0 ||
	    (rc = save_str_if_changed("network/gw_addr", net->gw_addr, cur->gw_addr, sizeof(cur->gw_addr))) != 0 ||
	    (rc = save_str_if_changed("network/ip_mask", net->ip_mask, cur->ip_mask, sizeof(cur->ip_mask))) != 0 ||
	    (rc = save_str_if_changed("network/dns1", net->dns1, cur->dns1, sizeof(cur->dns1))) != 0 ||
	    (rc = save_str_if_changed("network/hostname", net->hostname, cur->hostname, sizeof(cur->hostname))) != 0 ||
	    (rc = save_blob_if_changed("network/ipv6_mode", &net->ipv6_mode, &cur->ipv6_mode, sizeof(cur->ipv6_mode))) != 0 ||
	    (rc = save_str_if_changed("network/ipv6_addr", net->ipv6_addr, cur->ipv6_addr, sizeof(cur->ipv6_addr))) != 0 ||
	    (rc = save_blob_if_changed("network/ipv6_prefix_length", &net->ipv6_prefix_length, &cur->ipv6_prefix_length, sizeof(cur->ipv6_prefix_length))) != 0 ||
	    (rc = save_str_if_changed("network/ipv6_gateway", net->ipv6_gateway, cur->ipv6_gateway, sizeof(cur->ipv6_gateway))) != 0 ||
	    (rc = save_str_if_changed("network/ipv6_dns1", net->ipv6_dns1, cur->ipv6_dns1, sizeof(cur->ipv6_dns1))) != 0) {
		return rc;
	}

	LOG_INF("Network settings saved (hostname=%s, ip_method=%d)", cur->hostname, cur->ip_method);
	return 0;
}

int openjbod_settings_set_power(const struct power_settings *power)
{
	if (!power) {
		return -EINVAL;
	}

	struct power_settings *cur = &current_settings.power;
	int rc;

	if ((rc = save_blob_if_changed("power/ignore_power_switch", &power->ignore_power_switch, &cur->ignore_power_switch, sizeof(cur->ignore_power_switch))) != 0 ||
	    (rc = save_blob_if_changed("power/on_boot", &power->on_boot, &cur->on_boot, sizeof(cur->on_boot))) != 0 ||
	    (rc = save_blob_if_changed("power/on_boot_delay", &power->on_boot_delay, &cur->on_boot_delay, sizeof(cur->on_boot_delay))) != 0 ||
	    (rc = save_blob_if_changed("power/follow_usb", &power->follow_usb, &cur->follow_usb, sizeof(cur->follow_usb))) != 0 ||
	    (rc = save_blob_if_changed("power/follow_usb_delay", &power->follow_usb_delay, &cur->follow_usb_delay, sizeof(cur->follow_usb_delay))) != 0) {
		return rc;
	}

	LOG_INF("Power settings saved");
	return 0;
}

int openjbod_settings_set_http(const struct http_settings *http)
{
	if (!http) {
		return -EINVAL;
	}

	struct http_settings *cur = &current_settings.http;
	int rc;

	if ((rc = save_blob_if_changed("http/enable_http", &http->enable_http, &cur->enable_http, sizeof(cur->enable_http))) != 0 ||
	    (rc = save_blob_if_changed("http/enable_https", &http->enable_https, &cur->enable_https, sizeof(cur->enable_https))) != 0 ||
	    (rc = save_blob_if_changed("http/http_port", &http->http_port, &cur->http_port, sizeof(cur->http_port))) != 0 ||
	    (rc = save_blob_if_changed("http/https_port", &http->https_port, &cur->https_port, sizeof(cur->https_port))) != 0 ||
	    (rc = save_blob_if_changed("http/use_custom_certificates", &http->use_custom_certificates, &cur->use_custom_certificates, sizeof(cur->use_custom_certificates))) != 0) {
		return rc;
	}

	/* Certificate/key are not modified through this path; they are managed
	 * separately via openjbod_settings_take_custom_certificate()/_private_key()
	 * (the /api/certificates upload path) to keep their large buffers off the
	 * always-resident settings struct.
	 */
	LOG_INF("HTTP settings saved");
	return 0;
}

void openjbod_settings_take_custom_certificate(char *hex_or_null)
{
	http_take_str(&current_settings.http.custom_certificate, hex_or_null);
}

void openjbod_settings_take_custom_private_key(char *hex_or_null)
{
	http_take_str(&current_settings.http.custom_private_key, hex_or_null);
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

	/* PSA crypto is initialized once in openjbod_settings_init(). */

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

/* Constant-time comparison of two len-byte ranges. Returns 0 iff equal, without
 * an early-out that would leak how many leading bytes matched.
 */
static int ct_memcmp(const void *a, const void *b, size_t len)
{
	const volatile uint8_t *pa = (const volatile uint8_t *)a;
	const volatile uint8_t *pb = (const volatile uint8_t *)b;
	uint8_t diff = 0;

	for (size_t i = 0; i < len; i++) {
		diff |= (uint8_t)(pa[i] ^ pb[i]);
	}
	return diff;
}

/* Create the default 'admin' user (password 'openjbod') if no users exist.
 * Called once at startup from openjbod_settings_load_all().
 */
static int ensure_default_user(void)
{
	for (int i = 0; i < MAX_USERS; i++) {
		if (strlen(current_settings.auth.users[i].username) > 0) {
			return 0;
		}
	}

	LOG_WRN("No users configured; creating default 'admin' user "
		"(password 'openjbod') - change it immediately");

	char salt[SALT_STR_LEN];
	char hash[PASSWORD_HASH_LEN];
	int rc = generate_salt(salt, sizeof(salt));
	if (rc) {
		LOG_ERR("Failed to generate salt: %d", rc);
		return rc;
	}
	rc = hash_password_with_salt("openjbod", salt, hash, sizeof(hash));
	if (rc) {
		LOG_ERR("Failed to hash default password: %d", rc);
		return rc;
	}

	struct user_entry *u = &current_settings.auth.users[0];
	strncpy(u->username, "admin", sizeof(u->username) - 1);
	u->username[sizeof(u->username) - 1] = '\0';
	strncpy(u->password_hash, hash, sizeof(u->password_hash) - 1);
	u->password_hash[sizeof(u->password_hash) - 1] = '\0';
	strncpy(u->salt, salt, sizeof(u->salt) - 1);
	u->salt[sizeof(u->salt) - 1] = '\0';

	rc = openjbod_settings_save_user(0, u);
	if (rc) {
		LOG_ERR("Failed to save default user: %d", rc);
	}
	return rc;
}

int openjbod_auth_verify_credentials(const char *username, const char *password)
{
	char calculated_hash[PASSWORD_HASH_LEN] = {0};
	char stored_hash[PASSWORD_HASH_LEN] = {0};
	int rc;
	int user_idx;

	if (!username || !password) {
		return -EINVAL;
	}

	user_idx = find_user_by_username(username);
	if (user_idx < 0) {
		LOG_WRN("Authentication failed: user '%s' not found", username);
		return -EACCES;
	}

	rc = hash_password_with_salt(password, current_settings.auth.users[user_idx].salt,
				     calculated_hash, sizeof(calculated_hash));
	if (rc) {
		LOG_ERR("Failed to hash provided password: %d", rc);
		return rc;
	}

	/* Constant-time compare over the full zero-padded buffers (the base64 hash
	 * is a fixed length, so this leaks nothing useful).
	 */
	strncpy(stored_hash, current_settings.auth.users[user_idx].password_hash,
		sizeof(stored_hash) - 1);
	if (ct_memcmp(calculated_hash, stored_hash, PASSWORD_HASH_LEN) != 0) {
		LOG_WRN("Authentication failed: invalid password for user '%s'", username);
		return -EACCES;
	}

	LOG_DBG("Authentication successful for user: %s", username);
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
	if (!username || !password) {
		return -EINVAL;
	}

	size_t ulen = strlen(username);
	size_t plen = strlen(password);

	/* Reject (don't silently truncate) out-of-range credentials. */
	if (ulen == 0 || ulen > USERNAME_MAX_CHARS) {
		return -EINVAL;
	}
	if (plen < PASSWORD_MIN_LEN || plen > PASSWORD_MAX_LEN) {
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

	struct environment_settings *cur = &current_settings.environment;
	int rc;

	if ((rc = save_blob_if_changed("environment/use_external_fan_control", &environment->use_external_fan_control, &cur->use_external_fan_control, sizeof(cur->use_external_fan_control))) != 0 ||
	    (rc = save_blob_if_changed("environment/fan_update_interval_ms", &environment->fan_update_interval_ms, &cur->fan_update_interval_ms, sizeof(cur->fan_update_interval_ms))) != 0 ||
	    (rc = save_blob_if_changed("environment/fan_hysteresis_percent", &environment->fan_hysteresis_percent, &cur->fan_hysteresis_percent, sizeof(cur->fan_hysteresis_percent))) != 0 ||
	    (rc = save_blob_if_changed("environment/primary_temp_source", &environment->primary_temp_source, &cur->primary_temp_source, sizeof(cur->primary_temp_source))) != 0) {
		return rc;
	}

	/* Fan curve points */
	for (int i = 0; i < 5; i++) {
		char key[64];

		snprintf(key, sizeof(key), "environment/fan_curve%d/temperature", i);
		rc = save_blob_if_changed(key, &environment->fan_curve[i].temperature,
					  &cur->fan_curve[i].temperature, sizeof(cur->fan_curve[i].temperature));
		if (rc) {
			return rc;
		}
		snprintf(key, sizeof(key), "environment/fan_curve%d/fan_percent", i);
		rc = save_blob_if_changed(key, &environment->fan_curve[i].fan_percent,
					  &cur->fan_curve[i].fan_percent, sizeof(cur->fan_curve[i].fan_percent));
		if (rc) {
			return rc;
		}
	}

	LOG_INF("Environment settings saved");
	return 0;
}

int openjbod_settings_set_console(const struct console_settings *console)
{
	int rc;

	if (!console) {
		return -EINVAL;
	}

	struct console_settings *cur = &current_settings.console;

	if ((rc = save_blob_if_changed("console/uart_enabled", &console->uart_enabled, &cur->uart_enabled, sizeof(cur->uart_enabled))) != 0 ||
	    (rc = save_blob_if_changed("console/usb_enabled", &console->usb_enabled, &cur->usb_enabled, sizeof(cur->usb_enabled))) != 0) {
		return rc;
	}

	LOG_INF("Console settings saved");
	return 0;
}

int openjbod_settings_reset_all(void)
{
	int rc = fs_unlink(CONFIG_SETTINGS_FILE_PATH);
	if (rc < 0 && rc != -ENOENT) {
		LOG_ERR("Failed to remove %s: %d", CONFIG_SETTINGS_FILE_PATH, rc);
		return rc;
	}

	/* Free heap-allocated fields before the struct copy clobbers their pointers. */
	http_take_str(&current_settings.http.custom_certificate, NULL);
	http_take_str(&current_settings.http.custom_private_key, NULL);

	current_settings = default_settings;
	LOG_INF("Settings reset to defaults in memory and %s removed", CONFIG_SETTINGS_FILE_PATH);

	return 0;
}

int openjbod_settings_update_user_password(const char *username, const char *new_password)
{
	if (!username || !new_password) {
		return -EINVAL;
	}

	size_t plen = strlen(new_password);

	if (plen < PASSWORD_MIN_LEN || plen > PASSWORD_MAX_LEN) {
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
