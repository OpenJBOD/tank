/*
 * Copyright (c) 2025 The OpenJBOD Project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Custom `tank` UART shell commands for development & debugging. These wrap the
 * existing hardware drivers so power/fan/temperature can be exercised over the
 * serial console without the web UI. Built only when CONFIG_SHELL is enabled.
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <stdlib.h>

#include "sr_latch.h"
#include "fan_control.h"
#include "emc2301.h"
#include "temperature.h"
#include "device_info.h"
#include "settings.h"

/* Print a float as "<int>.<2dp>" without relying on %f / FP printf support. */
static void print_fixed(const struct shell *sh, const char *label, float v, const char *unit)
{
	int milli = (int)(v * 100.0f + (v >= 0 ? 0.5f : -0.5f));
	shell_print(sh, "  %-14s %d.%02d %s", label, milli / 100, abs(milli % 100), unit);
}

/* --- power -------------------------------------------------------------- */
static int cmd_power(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2 || strcmp(argv[1], "status") == 0) {
		shell_print(sh, "ATX power: %s", sr_latch_get_state() ? "ON" : "OFF");
		return 0;
	}
	if (strcmp(argv[1], "on") == 0) {
		sr_latch_set_on();
		shell_print(sh, "ATX power -> ON");
		return 0;
	}
	if (strcmp(argv[1], "off") == 0) {
		sr_latch_set_off();
		shell_print(sh, "ATX power -> OFF");
		return 0;
	}
	shell_error(sh, "usage: tank power [on|off|status]");
	return -EINVAL;
}

/* --- fan ---------------------------------------------------------------- */
static int cmd_fan(const struct shell *sh, size_t argc, char **argv)
{
	if (argc >= 2 && strcmp(argv[1], "set") == 0) {
		if (argc < 3) {
			shell_error(sh, "usage: tank fan set <0-100>");
			return -EINVAL;
		}
		int pct = atoi(argv[2]);
		if (pct < 0 || pct > 100) {
			shell_error(sh, "percent must be 0-100");
			return -EINVAL;
		}
		int rc = emc2301_set_pwm_duty(emc2301_percent_to_duty((uint8_t)pct));
		if (rc) {
			shell_error(sh, "set failed: %d", rc);
			return rc;
		}
		shell_print(sh, "fan drive -> %d%% (auto control may override)", pct);
		return 0;
	}

	struct emc2301_data d = {0};
	int rc = emc2301_get_status(&d);
	if (rc) {
		shell_error(sh, "fan read failed: %d", rc);
		return rc;
	}
	shell_print(sh, "fan: %u RPM, drive %u%% (duty %u/255)%s",
		    d.fan_rpm, emc2301_duty_to_percent(d.pwm_duty), d.pwm_duty,
		    d.fan_fault ? ", FAULT" : "");
	return 0;
}

/* --- temp --------------------------------------------------------------- */
static int cmd_temp(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc); ARG_UNUSED(argv);
	struct temperature_data t = {0};
	int rc = temperature_read(&t);
	if (rc) {
		shell_error(sh, "temperature read failed: %d", rc);
		return rc;
	}
	if (t.ds18b20_valid) {
		print_fixed(sh, "onboard", t.ds18b20_temp, "C");
	} else {
		shell_print(sh, "  %-14s n/a", "onboard");
	}
	if (t.ds18b20_ext_valid) {
		print_fixed(sh, "header", t.ds18b20_ext_temp, "C");
	} else {
		shell_print(sh, "  %-14s %s", "header",
			    temperature_ext_present() ? "n/a" : "not present");
	}
	if (t.rp2040_valid) {
		print_fixed(sh, "RP2040 die", t.rp2040_temp, "C");
	} else {
		shell_print(sh, "  %-14s n/a", "RP2040 die");
	}

	const struct openjbod_settings *s = openjbod_settings_get();
	float at = 0.0f;
	const char *as = "none";
	(void)temperature_get_active(&t, s->environment.primary_temp_source, &at, &as);
	shell_print(sh, "  %-14s %s (setting=%u)", "active source", as,
		    s->environment.primary_temp_source);
	return 0;
}

/* --- tempsrc ------------------------------------------------------------ */
static int cmd_tempsrc(const struct shell *sh, size_t argc, char **argv)
{
	struct openjbod_settings *s = openjbod_settings_get();

	if (argc < 2) {
		shell_print(sh, "primary temp source: %u (%s)",
			    s->environment.primary_temp_source,
			    s->environment.primary_temp_source == TEMP_SOURCE_HEADER ?
				    "header" : "onboard");
		return 0;
	}

	struct environment_settings env = s->environment;
	if (strcmp(argv[1], "onboard") == 0) {
		env.primary_temp_source = TEMP_SOURCE_ONBOARD;
	} else if (strcmp(argv[1], "header") == 0) {
		env.primary_temp_source = TEMP_SOURCE_HEADER;
	} else {
		shell_error(sh, "usage: tank tempsrc <onboard|header>");
		return -EINVAL;
	}

	int rc = openjbod_settings_set_environment(&env);
	if (rc) {
		shell_error(sh, "save failed: %d", rc);
		return rc;
	}
	shell_print(sh, "primary temp source -> %s", argv[1]);
	return 0;
}

/* --- console ------------------------------------------------------------ */
static int cmd_console(const struct shell *sh, size_t argc, char **argv)
{
	struct openjbod_settings *s = openjbod_settings_get();

	if (argc < 3) {
		shell_print(sh, "console: uart=%s usb=%s (reboot to apply changes)",
			    s->console.uart_enabled ? "on" : "off",
			    s->console.usb_enabled ? "on" : "off");
		return 0;
	}

	bool on;
	if (strcmp(argv[2], "on") == 0) {
		on = true;
	} else if (strcmp(argv[2], "off") == 0) {
		on = false;
	} else {
		shell_error(sh, "usage: tank console <uart|usb> <on|off>");
		return -EINVAL;
	}

	struct console_settings c = s->console;
	if (strcmp(argv[1], "uart") == 0) {
		c.uart_enabled = on;
	} else if (strcmp(argv[1], "usb") == 0) {
		c.usb_enabled = on;
	} else {
		shell_error(sh, "usage: tank console <uart|usb> <on|off>");
		return -EINVAL;
	}

	int rc = openjbod_settings_set_console(&c);
	if (rc) {
		shell_error(sh, "save failed: %d", rc);
		return rc;
	}
	shell_print(sh, "console %s -> %s (reboot to apply)", argv[1], argv[2]);
	return 0;
}

/* --- status ------------------------------------------------------------- */
static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc); ARG_UNUSED(argv);
	char serial[OPENJBOD_SERIAL_MAX_LEN] = {0};
	char rev[OPENJBOD_BOARD_REV_MAX_LEN] = {0};

	openjbod_device_info_get_serial(serial, sizeof(serial));
	openjbod_device_info_get_board_revision(rev, sizeof(rev));

	shell_print(sh, "OpenJBOD Tank status");
	shell_print(sh, "  %-14s %s", "version", openjbod_device_info_get_build_info());
	shell_print(sh, "  %-14s %s", "serial", serial);
	shell_print(sh, "  %-14s %s", "board", rev);
	shell_print(sh, "  %-14s %lld s", "uptime", k_uptime_get() / 1000);
	shell_print(sh, "  %-14s %s", "ATX power", sr_latch_get_state() ? "ON" : "OFF");
	cmd_fan(sh, 1, NULL);
	cmd_temp(sh, 1, NULL);
	return 0;
}

/* --- registration ------------------------------------------------------- */
SHELL_STATIC_SUBCMD_SET_CREATE(tank_subcmds,
	SHELL_CMD_ARG(power,  NULL, "ATX power: on|off|status", cmd_power, 1, 1),
	SHELL_CMD_ARG(fan,    NULL, "fan: [set <0-100>] (no arg = read)", cmd_fan, 1, 2),
	SHELL_CMD(temp,       NULL, "read temperature sensors", cmd_temp),
	SHELL_CMD_ARG(tempsrc, NULL, "primary temp source: <onboard|header>", cmd_tempsrc, 1, 1),
	SHELL_CMD_ARG(console, NULL, "console toggle: <uart|usb> <on|off>", cmd_console, 1, 2),
	SHELL_CMD(status,     NULL, "summarize device status", cmd_status),
	SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(tank, &tank_subcmds, "OpenJBOD Tank debug commands", NULL);
