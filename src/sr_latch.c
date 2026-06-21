/*
 * SR Latch Driver for OpenJBOD ATX Power Control
 * 
 * Controls the set-reset latch for ATX power supply using GPIO pins:
 * - GPIO13: LRESET (active low)
 * - GPIO14: LSET (active low) 
 * - GPIO15: LOUT (current state output)
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "sr_latch.h"

LOG_MODULE_REGISTER(sr_latch, LOG_LEVEL_DBG);

/* Direct GPIO pin definitions for SR latch */
#define LATCH_SET_PIN 14   /* GPIO14 - LSET */
#define LATCH_RESET_PIN 13 /* GPIO13 - LRESET */
#define LATCH_OUT_PIN 15   /* GPIO15 - LOUT */

/* Width of the set/reset pulse: long enough for the latch to settle. */
#define SR_LATCH_PULSE_MS 10

/* GPIO device */
static const struct device *gpio_dev;

/* Current state tracking */
static bool sr_latch_state = false;

int sr_latch_init(void)
{
	int ret;
	
	gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
	if (!device_is_ready(gpio_dev)) {
		LOG_ERR("GPIO device not ready");
		return -ENODEV;
	}
	
	ret = gpio_pin_configure(gpio_dev, LATCH_SET_PIN, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure latch SET pin: %d", ret);
		return ret;
	}
	
	ret = gpio_pin_configure(gpio_dev, LATCH_RESET_PIN, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure latch RESET pin: %d", ret);
		return ret;
	}
	
	ret = gpio_pin_configure(gpio_dev, LATCH_OUT_PIN, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure latch OUT pin: %d", ret);
		return ret;
	}
	
	/* Read initial state */
	sr_latch_state = gpio_pin_get(gpio_dev, LATCH_OUT_PIN);
	LOG_INF("SR latch initialized, current state: %s", sr_latch_state ? "ON" : "OFF");
	
	return 0;
}

void sr_latch_set_on(void)
{
	gpio_pin_set(gpio_dev, LATCH_SET_PIN, 1);
	gpio_pin_set(gpio_dev, LATCH_RESET_PIN, 0);
	k_msleep(SR_LATCH_PULSE_MS);
	gpio_pin_set(gpio_dev, LATCH_SET_PIN, 0);
	sr_latch_state = true;
	LOG_INF("SR latch turned ON");
}

void sr_latch_set_off(void)
{
	gpio_pin_set(gpio_dev, LATCH_SET_PIN, 0);
	gpio_pin_set(gpio_dev, LATCH_RESET_PIN, 1);
	k_msleep(SR_LATCH_PULSE_MS);
	gpio_pin_set(gpio_dev, LATCH_RESET_PIN, 0);
	sr_latch_state = false;
	LOG_INF("SR latch turned OFF");
}

bool sr_latch_get_state(void)
{
	return gpio_pin_get(gpio_dev, LATCH_OUT_PIN);
}