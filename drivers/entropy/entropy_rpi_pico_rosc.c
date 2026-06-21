/*
 * Copyright (c) 2025 The OpenJBOD Project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Entropy driver for the RP2040 ring oscillator (ROSC). The ROSC RANDOMBIT
 * register is the output of a single ring-oscillator stage; on its own it is
 * weak and biased, so we (a) space successive samples to decorrelate them and
 * (b) apply von Neumann debiasing to remove bias. The result is fed into
 * Zephyr's CTR-DRBG CSPRNG (CONFIG_CTR_DRBG_CSPRNG_GENERATOR), which provides the
 * cryptographic strength for sys_csrand_get() - used for password salts and
 * web-session ids. This replaces CONFIG_TEST_RANDOM_GENERATOR (a non-crypto PRNG).
 */

#define DT_DRV_COMPAT raspberrypi_pico_rosc_entropy

#include <zephyr/device.h>
#include <zephyr/drivers/entropy.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <hardware/structs/rosc.h>

/* Decorrelation spacing between ROSC samples (CPU nops). The ROSC bit changes far
 * faster than this loop, so a short delay is enough to decorrelate samples. */
#define ROSC_SAMPLE_SPACING 24

/* Safety bound on von Neumann attempts per byte (8 output bits). With a healthy
 * ROSC this completes in tens of pairs; the bound guards against a stuck ROSC. */
#define ROSC_MAX_PAIRS_PER_BYTE 4096

static inline uint32_t rosc_sample_bit(void)
{
	for (volatile int i = 0; i < ROSC_SAMPLE_SPACING; i++) {
		arch_nop();
	}
	return rosc_hw->randombit & 1u;
}

static int rpi_pico_rosc_get_entropy(const struct device *dev, uint8_t *buffer, uint16_t length)
{
	ARG_UNUSED(dev);

	for (uint16_t i = 0; i < length; i++) {
		uint8_t byte = 0;
		int bits = 0;
		int pairs = 0;

		/* von Neumann debiasing: read pairs of bits, keep the first bit of a
		 * 01/10 pair, discard 00/11. Output bits are unbiased.
		 */
		while (bits < 8) {
			uint32_t a = rosc_sample_bit();
			uint32_t b = rosc_sample_bit();

			if (a != b) {
				byte = (uint8_t)((byte << 1) | a);
				bits++;
			}

			if (++pairs > ROSC_MAX_PAIRS_PER_BYTE) {
				return -EIO;
			}
		}
		buffer[i] = byte;
	}

	return 0;
}

static int rpi_pico_rosc_get_entropy_isr(const struct device *dev, uint8_t *buffer,
					 uint16_t length, uint32_t flags)
{
	ARG_UNUSED(flags);

	/* The sampling is busy-wait only (no blocking), so it is ISR-safe. */
	int rc = rpi_pico_rosc_get_entropy(dev, buffer, length);

	return rc == 0 ? (int)length : rc;
}

static int rpi_pico_rosc_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	/* ROSC is left enabled by the RP2040 clock setup (the system runs on the
	 * PLL, but ROSC remains powered), so RANDOMBIT is live. Nothing to do.
	 */
	return 0;
}

static DEVICE_API(entropy, rpi_pico_rosc_api) = {
	.get_entropy = rpi_pico_rosc_get_entropy,
	.get_entropy_isr = rpi_pico_rosc_get_entropy_isr,
};

DEVICE_DT_INST_DEFINE(0, rpi_pico_rosc_init, NULL, NULL, NULL,
		      PRE_KERNEL_1, CONFIG_ENTROPY_INIT_PRIORITY, &rpi_pico_rosc_api);
