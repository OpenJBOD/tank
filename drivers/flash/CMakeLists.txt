# SPDX-License-Identifier: Apache-2.0
zephyr_library()
zephyr_library_sources_ifdef(CONFIG_FLASH_RPI_PICO_FIXED flash_rpi_pico_fixed.c)
