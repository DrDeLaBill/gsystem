/*
 * @file button.h
 * @brief Button input handling with debouncing, click detection, and hold timing.
 *
 * Provides utilities for managing physical button inputs with:
 * - Software debouncing with configurable delay
 * - Click detection and counting
 * - Hold time tracking with configurable threshold
 * - Integration with system timers for timing-accurate state tracking
 *
 * Copyright © 2025 Georgy E. All rights reserved.
 */

#ifndef _BUTTON_H_
#define _BUTTON_H_


#ifdef __cplusplus
extern "C" {
#endif


#include "gconfig.h"
#include "gdefines.h"

#include <stdint.h>

#include "drivers.h"


extern const uint32_t DEFAULT_HOLD_TIME_MS;


typedef struct _button_t {
	port_pin_t _pin;

	uint32_t   _debounce_ms;
	gtimer_t   _debounce;

	gtimer_t   _timeout;

	bool       _pressed;
	bool       _inverse;

	size_t     _clicks;
	size_t     _last_clicks;
	gtimer_t   _clicks_tim;

	bool       _next_click;
	uint32_t   _held;

	uint32_t   _hold_ms;
	gtimer_t   _held_tim;
} button_t;


/*
 * @brief Create and initialize a button with debounce and hold time settings.
 * @param button (button_t*) - Pointer to button struct to initialize.
 * @param pin (const port_pin_t*) - Hardware pin descriptor for the button.
 * @param inverse (bool) - If true, invert the logical button state (active low).
 * @param hold_ms (uint32_t) - Hold time threshold in milliseconds.
 * @return None
 * @example static button_t my_button; port_pin_t my_pin = {port, pin}; button_create(&my_button, &my_pin, true, 2000);
 */
void button_create(
	button_t*         button,
	const port_pin_t* pin,
	bool              inverse,
	uint32_t          hold_ms
);

/*
 * @brief Reset button internal state (click counters and timers).
 * @param button (button_t*) - Pointer to the button to reset.
 * @return None
 */
void button_reset(button_t* button);

/*
 * @brief Update button state; call this periodically from main loop or task.
 * @param button (button_t*) - Pointer to the button to update.
 * @return None
 */
void button_tick(button_t* button);

/*
 * @brief Get the number of clicks detected since last reset.
 * @param button (button_t*) - Pointer to the button.
 * @return uint32_t - Click count.
 */
uint32_t button_clicks(button_t* button);

/*
 * @brief Get how long the button has been held in milliseconds.
 * @param button (button_t*) - Pointer to the button.
 * @return uint32_t - Hold duration in milliseconds.
 */
uint32_t button_held_ms(button_t* button);

/*
 * @brief Check if the button is currently pressed.
 * @param button (button_t*) - Pointer to the button.
 * @return bool - true if button is pressed.
 */
bool button_pressed(button_t* button);

#endif

#ifdef __cplusplus
}
#endif
