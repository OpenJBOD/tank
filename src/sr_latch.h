/*
 * SR Latch Driver for OpenJBOD ATX Power Control
 */

#ifndef SR_LATCH_H
#define SR_LATCH_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Initialize the SR latch GPIO pins
 * 
 * @return 0 on success, negative error code on failure
 */
int sr_latch_init(void);

/**
 * @brief Turn on the SR latch (ATX power supply)
 */
void sr_latch_set_on(void);

/**
 * @brief Turn off the SR latch (ATX power supply)
 */
void sr_latch_set_off(void);

/**
 * @brief Get the current state of the SR latch
 * 
 * @return true if latch is on, false if off
 */
bool sr_latch_get_state(void);

#endif /* SR_LATCH_H */