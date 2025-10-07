/* Copyright © 2024 Georgy E. All rights reserved. */

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


void button_create(
	button_t*   button,
	port_pin_t* pin,
	bool        inverse,
	uint32_t    hold_ms
);
void button_reset(button_t* button);
void button_tick(button_t* button);

uint32_t button_clicks(button_t* button);
uint32_t button_held_ms(button_t* button);
bool button_pressed(button_t* button);

#endif

#ifdef __cplusplus
}
#endif
