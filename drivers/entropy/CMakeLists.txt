# SPDX-License-Identifier: Apache-2.0
zephyr_library()
zephyr_library_sources_ifdef(CONFIG_ENTROPY_RPI_PICO_ROSC entropy_rpi_pico_rosc.c)
