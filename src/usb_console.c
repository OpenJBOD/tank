/*
 * Copyright (c) 2025 The OpenJBOD Project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Secondary Zephyr shell/console over USB CDC-ACM. The hardware UART0 stays the
 * primary chosen console/shell (brought up automatically by Zephyr's SYS_INIT);
 * this file stands up a SECOND, independent shell instance bound to the CDC-ACM
 * device, mirroring zephyr/subsys/shell/backends/shell_uart.c.
 *
 * It is initialized explicitly from main() (thread context, after the kernel and
 * settings are up) rather than a SYS_INIT, so USB is enabled only once
 * everything it depends on exists. A prior attempt that enabled a USB console
 * via the chosen-node/SYS_INIT path hard-faulted; this avoids that ordering.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>
#include <zephyr/logging/log.h>

#include <sample_usbd.h>

#include "usb_console.h"

LOG_MODULE_REGISTER(tank_usb_console, LOG_LEVEL_INF);

#define CDC_ACM_DEV DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0))

/* Second serial shell instance, bound to the CDC-ACM device at runtime.
 * Reuses the same ring-buffer sizing as the primary serial shell backend.
 */
SHELL_UART_DEFINE(shell_transport_usb);
SHELL_DEFINE(shell_usb, "usb:~$ ", &shell_transport_usb,
	     CONFIG_SHELL_BACKEND_SERIAL_LOG_MESSAGE_QUEUE_SIZE,
	     CONFIG_SHELL_BACKEND_SERIAL_LOG_MESSAGE_QUEUE_TIMEOUT,
	     SHELL_FLAG_OLF_CRLF);

int tank_usb_console_init(void)
{
	const struct device *cdc = CDC_ACM_DEV;
	struct usbd_context *usbd;
	int err;

	if (!device_is_ready(cdc)) {
		LOG_ERR("CDC-ACM device not ready");
		return -ENODEV;
	}

	/* Reuse the canonical sample helper (its header invites reuse as a
	 * template; Kconfig.sample_usbd is sourced by this app's Kconfig).
	 */
	usbd = sample_usbd_init_device(NULL);
	if (usbd == NULL) {
		LOG_ERR("Failed to initialize USB device");
		return -ENODEV;
	}

	err = usbd_enable(usbd);
	if (err) {
		LOG_ERR("Failed to enable USB device: %d", err);
		return err;
	}

	static const struct shell_backend_config_flags cfg_flags =
		SHELL_DEFAULT_BACKEND_CONFIG_FLAGS;

	/* log_backend=true so logs also appear on the USB console. */
	err = shell_init(&shell_usb, cdc, cfg_flags, true, LOG_LEVEL_INF);
	if (err) {
		LOG_ERR("Failed to start USB shell: %d", err);
		return err;
	}

	LOG_INF("USB CDC-ACM console started");
	return 0;
}
