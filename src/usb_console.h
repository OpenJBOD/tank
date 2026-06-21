/*
 * Copyright (c) 2025 The OpenJBOD Project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Secondary Zephyr shell/console over USB CDC-ACM, alongside the hardware UART.
 */

#ifndef USB_CONSOLE_H__
#define USB_CONSOLE_H__

#include <zephyr/sys/util_macro.h>

#ifdef __cplusplus
extern "C" {
#endif

#if IS_ENABLED(CONFIG_OPENJBOD_USB_CONSOLE)
/**
 * Bring up the USB device and start a second shell instance on the CDC-ACM
 * port. Call once, from a thread context (e.g. main()), after the kernel and
 * settings are initialized.
 *
 * @return 0 on success, negative errno otherwise.
 */
int tank_usb_console_init(void);
#else
static inline int tank_usb_console_init(void) { return 0; }
#endif

#ifdef __cplusplus
}
#endif

#endif /* USB_CONSOLE_H__ */
