/*
 * Copyright (c) 2024 OpenJBOD Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SETTINGS_H__
#define SETTINGS_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HOSTNAME_MAX_LEN 32
#define IP_ADDR_MAX_LEN 16
#define IPV6_ADDR_MAX_LEN 46
#define USERNAME_MAX_LEN 32
#define USERNAME_MAX_CHARS (USERNAME_MAX_LEN - 1)  /* usable chars (buffer holds NUL) */
/* Plaintext password policy (the password itself is only ever stored hashed). The
 * max is kept well under the login/create JSON buffers so a password can never be
 * "settable but un-loginable". */
#define PASSWORD_MIN_LEN 8
#define PASSWORD_MAX_LEN 64
#define PASSWORD_HASH_LEN 64
#define SALT_LEN 16
#define SALT_STR_LEN (SALT_LEN * 2 + 1)  /* Hex string length */
#define MAX_USERS 5
#define CERTIFICATE_HEX_MAX_LEN 4096  /* Max certificate as hex string */
#define PRIVATE_KEY_HEX_MAX_LEN 2048  /* Max private key as hex string */

enum ip_method {
	IP_METHOD_DHCP = 0,
	IP_METHOD_STATIC = 1
};

enum ipv6_mode {
	IPV6_MODE_DISABLED = 0,
	IPV6_MODE_SLAAC = 1,
	IPV6_MODE_DHCPV6 = 2,
	IPV6_MODE_STATIC = 3,
};

struct network_settings {
	enum ip_method ip_method;
	char ip_addr[IP_ADDR_MAX_LEN];
	char gw_addr[IP_ADDR_MAX_LEN];
	char ip_mask[IP_ADDR_MAX_LEN];
	char dns1[IP_ADDR_MAX_LEN];
	char hostname[HOSTNAME_MAX_LEN];
	enum ipv6_mode ipv6_mode;
	char ipv6_addr[IPV6_ADDR_MAX_LEN];
	uint8_t ipv6_prefix_length;
	char ipv6_gateway[IPV6_ADDR_MAX_LEN];
	char ipv6_dns1[IPV6_ADDR_MAX_LEN];
};

struct power_settings {
	bool ignore_power_switch;
	bool on_boot;
	uint32_t on_boot_delay;
	bool follow_usb;
	uint32_t follow_usb_delay;
};

struct user_entry {
	char username[USERNAME_MAX_LEN];
	char password_hash[PASSWORD_HASH_LEN];
	char salt[SALT_STR_LEN];
};

struct auth_settings {
	struct user_entry users[MAX_USERS];
};

struct http_settings {
	bool enable_http;
	bool enable_https;
	uint16_t http_port;
	uint16_t https_port;
	bool use_custom_certificates;
	/* Heap-allocated hex strings, NULL when unset. Kept out of the struct (and
	 * thus out of always-resident RAM) since custom certs are the exception;
	 * CERTIFICATE_HEX_MAX_LEN / PRIVATE_KEY_HEX_MAX_LEN remain the size caps.
	 */
	char *custom_certificate;
	char *custom_private_key;
};

/* Fan curve point structure */
struct fan_curve_point {
	float temperature;  /* Temperature in Celsius */
	uint8_t fan_percent; /* Fan speed percentage (0-100) */
};

/* Primary temperature source selection (for fan control / reporting). */
enum temp_source {
	TEMP_SRC_ONBOARD = 0,  /* Onboard DS18B20 on GPIO18 */
	TEMP_SRC_HEADER  = 1,  /* External DS18B20 on the GPIO11 pin header */
};

struct environment_settings {
	bool use_external_fan_control;    /* If true, disable automatic fan control */
	uint32_t fan_update_interval_ms;  /* Fan control update interval in milliseconds */
	uint8_t fan_hysteresis_percent;   /* Hysteresis to prevent oscillation */
	uint8_t primary_temp_source;      /* enum temp_source: preferred probe */
	struct fan_curve_point fan_curve[5]; /* 5-point fan curve */

	/* EMC2301 hardware configuration (see emc2301.h). Defaults suit any
	 * Intel-spec 4-wire fan: 26 kHz PWM, 2 tach pulses per revolution. */
	uint32_t fan_pwm_base_hz;         /* 26000 | 19531 | 4882 | 2441 */
	uint8_t fan_pwm_divide;           /* 1..255, PWM = base / divide */
	uint8_t fan_tach_pulses_per_rev;  /* 1..4 (2 = standard) */
	uint8_t fan_tach_edges;           /* 0 = auto (2*ppr+1), or 3|5|7|9; fewer edges read slower fans */
	uint8_t fan_tach_range;           /* tach range multiplier 1|2|4|8 (min RPM 500*m) */
	uint8_t fan_min_drive_percent;    /* fan_control never drives below this when on */
	/* Results of the last fan characterization run (0 = never calibrated). */
	uint8_t fan_cal_min_spin_percent; /* lowest duty that started the fan from rest */
	uint16_t fan_cal_max_rpm;         /* RPM measured at 100 % duty */
};

/* Console (shell) backend enable flags. Both default true. */
struct console_settings {
	bool uart_enabled;  /* Hardware UART0 shell/console (pin header) */
	bool usb_enabled;   /* USB CDC-ACM shell/console */
};

struct openjbod_settings {
	struct network_settings network;
	struct power_settings power;
	struct auth_settings auth;
	struct http_settings http;
	struct environment_settings environment;
	struct console_settings console;
};

int openjbod_settings_init(void);
int openjbod_settings_load_all(void);
int openjbod_settings_save_all(void);
struct openjbod_settings *openjbod_settings_get(void);
int openjbod_settings_set_network(const struct network_settings *net);
int openjbod_settings_set_power(const struct power_settings *power);
int openjbod_settings_set_auth(const struct auth_settings *auth);
int openjbod_settings_set_http(const struct http_settings *http);
/* Set the optional custom TLS certificate/private key (hex strings). Takes
 * ownership of the heap-allocated argument (or NULL to clear); frees the previous
 * value. Call openjbod_settings_save_all() afterwards to persist. */
void openjbod_settings_take_custom_certificate(char *hex_or_null);
void openjbod_settings_take_custom_private_key(char *hex_or_null);
int openjbod_settings_set_environment(const struct environment_settings *environment);
/* Restore the fan hardware fields (fan_pwm_*, fan_tach_*, fan_min_drive_percent,
 * fan_cal_*) to their defaults and persist them. */
int openjbod_settings_reset_fan_hw(void);
int openjbod_settings_set_console(const struct console_settings *console);
int openjbod_settings_save_user(int user_idx, const struct user_entry *user);
int openjbod_settings_delete_user(int user_idx);
int openjbod_settings_create_user(const char *username, const char *password);
int openjbod_settings_update_user_password(const char *username, const char *new_password);
int openjbod_auth_verify_credentials(const char *username, const char *password);
int openjbod_settings_reset_all(void);

#ifdef __cplusplus
}
#endif

#endif /* SETTINGS_H__ */