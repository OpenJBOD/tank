/*
 * Copyright (c) 2025 The OpenJBOD Project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Network-status LED (led0): off at boot, blinks while awaiting an IP address,
 * solid once the web server is up. A locator beacon can fast-blink it on demand,
 * overriding the status display until turned off.
 */

#ifndef STATUS_LED_H__
#define STATUS_LED_H__

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum status_led_state {
	STATUS_LED_OFF,       /* boot / no networking yet */
	STATUS_LED_AWAITING,  /* blink 1s on / 1s off - awaiting address */
	STATUS_LED_SOLID,     /* web server started */
};

/** Initialize the LED (turns it off). Safe to call if no LED is present. */
void status_led_init(void);

/** Set the network-status state. */
void status_led_set(enum status_led_state state);

/** Enable/disable the locator beacon (fast blink, overrides the status state). */
void status_led_set_beacon(bool on);

/** Whether the locator beacon is currently active. */
bool status_led_beacon_active(void);

#ifdef __cplusplus
}
#endif

#endif /* STATUS_LED_H__ */
