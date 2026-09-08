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
#include "fan_calibrate.h"
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
	ARG_UNUSED(argc); ARG_UNUSED(argv);
	struct emc2301_data d = {0};
	struct emc2301_config c;
	int rc = emc2301_get_status(&d);

	if (rc) {
		shell_error(sh, "fan read failed: %d", rc);
		return rc;
	}
	emc2301_get_config(&c);
	shell_print(sh, "fan: %u RPM, drive %u%% (duty %u/255)%s%s%s%s",
		    d.fan_rpm, emc2301_duty_to_percent(d.pwm_duty), d.pwm_duty,
		    d.stall ? ", STALL" : "", d.drive_fail ? ", DRIVE_FAIL" : "",
		    d.spin_fail ? ", SPIN_FAIL" : "", d.watchdog ? ", WATCHDOG" : "");
	shell_print(sh, "  %-14s %u (%s)", "tach count", d.tach_count,
		    d.tach_valid ? "valid" : "no signal");
	shell_print(sh, "  %-14s %u Hz (base %u / %u)", "pwm",
		    emc2301_effective_pwm_hz(), c.pwm_base_hz, c.pwm_divide);
	shell_print(sh, "  %-14s %u pulses/rev, %u edges%s, range x%u (min %u RPM)", "tach cfg",
		    c.tach_pulses_per_rev, emc2301_config_edges(&c),
		    c.tach_edges ? "" : " (auto)", c.tach_range_mult, emc2301_config_min_rpm(&c));
	shell_print(sh, "  %-14s %s, target %u%%, min drive %u%%", "control",
		    fan_control_mode(), fan_control_current_target(),
		    openjbod_settings_get()->environment.fan_min_drive_percent);
	return 0;
}

static int cmd_fan_set(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_error(sh, "usage: tank fan set <0-100>");
		return -EINVAL;
	}
	int pct = atoi(argv[1]);
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

static void print_fan_cfg(const struct shell *sh, const struct environment_settings *e)
{
	struct emc2301_config c;

	fan_control_hw_config_from_settings(e, &c);
	shell_print(sh, "fan hw config: pwm %u Hz / %u, %u pulses/rev, %u edges%s, range x%u "
		    "(min %u RPM), min drive %u%%; last cal: min spin %u%%, max %u RPM",
		    e->fan_pwm_base_hz, e->fan_pwm_divide, e->fan_tach_pulses_per_rev,
		    emc2301_config_edges(&c), e->fan_tach_edges ? "" : " (auto)", e->fan_tach_range,
		    emc2301_config_min_rpm(&c), e->fan_min_drive_percent,
		    e->fan_cal_min_spin_percent, e->fan_cal_max_rpm);
}

static int save_and_apply_env(const struct shell *sh, const struct environment_settings *env)
{
	int rc = openjbod_settings_set_environment(env);
	if (rc) {
		shell_error(sh, "save failed: %d", rc);
		return rc;
	}
	rc = fan_control_apply_hw_config();
	if (rc && rc != -ENODEV) {
		shell_error(sh, "saved, but applying to the EMC2301 failed: %d", rc);
		return rc;
	}
	print_fan_cfg(sh, &openjbod_settings_get()->environment);
	return 0;
}

static int cmd_fan_cfg(const struct shell *sh, size_t argc, char **argv)
{
	struct environment_settings env = openjbod_settings_get()->environment;

	if (argc < 2) {
		print_fan_cfg(sh, &env);
		return 0;
	}
	if (strcmp(argv[1], "reset") == 0) {
		int rc = openjbod_settings_reset_fan_hw();
		if (rc) {
			shell_error(sh, "reset failed: %d", rc);
			return rc;
		}
		rc = fan_control_apply_hw_config();
		if (rc && rc != -ENODEV) {
			shell_error(sh, "apply failed: %d", rc);
			return rc;
		}
		print_fan_cfg(sh, &openjbod_settings_get()->environment);
		return 0;
	}
	if (argc < 3) {
		shell_error(sh, "usage: tank fan cfg [pwm <hz> [divide] | pulses <1-4> | "
			    "edges <0|3|5|7|9> | range <1|2|4|8> | mindrive <0-100> | reset]");
		return -EINVAL;
	}
	if (strcmp(argv[1], "pwm") == 0) {
		env.fan_pwm_base_hz = emc2301_snap_pwm_base_hz((uint32_t)atoi(argv[2]));
		if (argc >= 4) {
			int d = atoi(argv[3]);
			if (d < 1 || d > 255) {
				shell_error(sh, "divide must be 1-255");
				return -EINVAL;
			}
			env.fan_pwm_divide = (uint8_t)d;
		}
	} else if (strcmp(argv[1], "pulses") == 0) {
		env.fan_tach_pulses_per_rev = (uint8_t)atoi(argv[2]);
	} else if (strcmp(argv[1], "edges") == 0) {
		env.fan_tach_edges = (uint8_t)atoi(argv[2]);
	} else if (strcmp(argv[1], "range") == 0) {
		env.fan_tach_range = (uint8_t)atoi(argv[2]);
	} else if (strcmp(argv[1], "mindrive") == 0) {
		int v = atoi(argv[2]);
		if (v < 0 || v > 100) {
			shell_error(sh, "min drive must be 0-100");
			return -EINVAL;
		}
		env.fan_min_drive_percent = (uint8_t)v;
	} else {
		shell_error(sh, "unknown setting '%s'", argv[1]);
		return -EINVAL;
	}
	return save_and_apply_env(sh, &env);
}

static int cmd_fan_cal(const struct shell *sh, size_t argc, char **argv)
{
	const char *what = argc >= 2 ? argv[1] : "status";

	if (strcmp(what, "start") == 0) {
		int rc = fan_calibrate_request();
		switch (rc) {
		case 0:
			shell_print(sh, "fan characterization started (up to ~2 min); "
				    "'tank fan cal' shows progress");
			return 0;
		case -EBUSY:
			shell_error(sh, "already running");
			return rc;
		case -ENODEV:
			shell_error(sh, "fan controller not initialized");
			return rc;
		case -EHOSTDOWN:
			shell_error(sh, "temperature too high to run safely");
			return rc;
		default:
			shell_error(sh, "request failed: %d", rc);
			return rc;
		}
	}
	if (strcmp(what, "abort") == 0) {
		int rc = fan_calibrate_abort();
		if (rc) {
			shell_error(sh, "nothing to abort");
			return rc;
		}
		shell_print(sh, "abort requested");
		return 0;
	}
	if (strcmp(what, "status") != 0) {
		shell_error(sh, "usage: tank fan cal [start|abort|status]");
		return -EINVAL;
	}

	struct fan_cal_status st;
	fan_calibrate_get_status(&st);
	shell_print(sh, "fan cal: %s, phase %s, %u%%, %u ms%s%s",
		    fan_cal_state_str(st.state), fan_cal_phase_str(st.phase),
		    st.progress_percent, st.elapsed_ms,
		    st.abort_reason ? ", reason " : "", st.abort_reason ? st.abort_reason : "");
	if (st.state == FAN_CAL_DONE || st.state == FAN_CAL_ERROR || st.state == FAN_CAL_ABORTED) {
		const struct fan_cal_result *r = &st.result;
		shell_print(sh, "  tach %s, ATX %s, monotonic %s, retry %s, budget cutoff %s",
			    r->tach_present ? "present" : "absent", r->atx_power_on ? "on" : "off",
			    r->monotonic ? "yes" : "no", r->used_retry ? "yes" : "no",
			    r->budget_cutoff ? "yes" : "no");
		for (int i = 0; i < FAN_CAL_SWEEP_POINTS; i++) {
			shell_print(sh, "  %3u%% -> %5u RPM", r->sweep_percent[i], r->sweep_rpm[i]);
		}
		shell_print(sh, "  min spin %u%%, max %u RPM, pwm %u Hz, %u edges, range x%u, "
			    "ppr assumed %u suggested %u, confidence %u",
			    r->min_spin_percent, r->max_rpm, r->chosen_pwm_base_hz, r->chosen_edges,
			    r->chosen_range, r->assumed_ppr, r->suggested_ppr, r->confidence);
		shell_print(sh, "  note: %s", r->note);
	}
	return 0;
}

static int cmd_fan_regs(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc); ARG_UNUSED(argv);
	static const uint8_t regs[] = {
		0x20, 0x24, 0x25, 0x26, 0x27, 0x29, 0x2A, 0x2B, 0x2D, 0x30, 0x31, 0x32,
		0x33, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
		0xFD, 0xFE, 0xFF,
	};

	for (size_t i = 0; i < ARRAY_SIZE(regs); i++) {
		uint8_t v;
		int rc = emc2301_read_reg(regs[i], &v);
		if (rc) {
			shell_error(sh, "  0x%02x: read failed (%d)", regs[i], rc);
			continue;
		}
		shell_print(sh, "  0x%02x = 0x%02x", regs[i], v);
	}
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
SHELL_STATIC_SUBCMD_SET_CREATE(fan_subcmds,
	SHELL_CMD_ARG(set,  NULL, "set drive percent: <0-100>", cmd_fan_set, 2, 0),
	SHELL_CMD_ARG(cfg,  NULL, "show/set hw config: [pwm <hz> [div]|pulses <1-4>|edges <0|3|5|7|9>|range <1|2|4|8>|mindrive <0-100>|reset]",
		      cmd_fan_cfg, 1, 3),
	SHELL_CMD_ARG(cal,  NULL, "characterization: [start|abort|status]", cmd_fan_cal, 1, 1),
	SHELL_CMD(regs,     NULL, "dump EMC2301 registers", cmd_fan_regs),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(tank_subcmds,
	SHELL_CMD_ARG(power,  NULL, "ATX power: on|off|status", cmd_power, 1, 1),
	SHELL_CMD_ARG(fan,    &fan_subcmds, "fan status; subcommands: set cfg cal regs", cmd_fan, 1, 0),
	SHELL_CMD(temp,       NULL, "read temperature sensors", cmd_temp),
	SHELL_CMD_ARG(tempsrc, NULL, "primary temp source: <onboard|header>", cmd_tempsrc, 1, 1),
	SHELL_CMD_ARG(console, NULL, "console toggle: <uart|usb> <on|off>", cmd_console, 1, 2),
	SHELL_CMD(status,     NULL, "summarize device status", cmd_status),
	SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(tank, &tank_subcmds, "OpenJBOD Tank debug commands", NULL);
