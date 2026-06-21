/*
 * Copyright (c) 2025 The OpenJBOD Project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Network-status LED driver for led0. A single self-rescheduling work item is the
 * only thing that touches the LED; status_led_set()/_set_beacon() just update the
 * desired state and kick the work to re-evaluate. The beacon overrides the status
 * state while active.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/logging/log.h>

#include "status_led.h"

LOG_MODULE_REGISTER(status_led, LOG_LEVEL_INF);

#define STATUS_LED_IDX        0     /* led0 = first LED in the gpio-leds node */
#define BLINK_AWAITING_MS     1000  /* 1s on / 1s off */
#define BLINK_BEACON_MS       500   /* 0.5s on / 0.5s off */

static const struct device *leds_dev = DEVICE_DT_GET_ANY(gpio_leds);

/* Desired state - written by callers (multiple contexts). */
static enum status_led_state s_state = STATUS_LED_OFF;
static bool s_beacon;

/* Current physical level - owned solely by the work handler. */
static bool s_level;

static void led_apply(bool on)
{
	if (leds_dev == NULL) {
		return;
	}
	if (on) {
		led_on(leds_dev, STATUS_LED_IDX);
	} else {
		led_off(leds_dev, STATUS_LED_IDX);
	}
	s_level = on;
}

static void blink_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(blink_work, blink_work_handler);

/* Re-evaluate the desired behaviour and drive the LED. Reschedules itself only
 * while a blinking mode is active; for solid/off it sets the level and stops
 * (a later status_led_set / status_led_set_beacon call kicks it again).
 */
static void blink_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (s_beacon) {
		led_apply(!s_level);
		LOG_DBG("beacon toggle -> %s", s_level ? "on" : "off");
		k_work_reschedule(&blink_work, K_MSEC(BLINK_BEACON_MS));
		return;
	}

	switch (s_state) {
	case STATUS_LED_SOLID:
		led_apply(true);
		break;
	case STATUS_LED_AWAITING:
		led_apply(!s_level);
		LOG_DBG("awaiting toggle -> %s", s_level ? "on" : "off");
		k_work_reschedule(&blink_work, K_MSEC(BLINK_AWAITING_MS));
		break;
	case STATUS_LED_OFF:
	default:
		led_apply(false);
		break;
	}
}

void status_led_init(void)
{
	if (leds_dev == NULL || !device_is_ready(leds_dev)) {
		LOG_WRN("LED device not ready; status LED disabled");
		leds_dev = NULL;
		return;
	}

	s_state = STATUS_LED_OFF;
	s_beacon = false;
	led_apply(false);
	LOG_INF("status LED: off");
}

void status_led_set(enum status_led_state state)
{
	s_state = state;
	LOG_INF("status LED: %s",
		state == STATUS_LED_SOLID    ? "solid" :
		state == STATUS_LED_AWAITING ? "awaiting (blinking)" : "off");
	k_work_reschedule(&blink_work, K_NO_WAIT);
}

void status_led_set_beacon(bool on)
{
	s_beacon = on;
	LOG_INF("locator beacon: %s", on ? "on" : "off");
	k_work_reschedule(&blink_work, K_NO_WAIT);
}

bool status_led_beacon_active(void)
{
	return s_beacon;
}
