/* Copyright © 2024 Georgy E. All rights reserved. */

#ifndef _BUTTON_H_
#define _BUTTON_H_


#ifdef __cplusplus
extern "C" {
#endif

#include "gconfig.h"
#include "gdefines.h"

#if GSYSTEM_BUTTONS_COUNT

#include <stdint.h>

#include "g_hal.h"


extern const uint32_t DEFAULT_HOLD_TIME_MS;


typedef struct _button_t {
	port_pin_t _pin;

	uint32_t   _debounce_ms;
	gtimer_t   _debounce;

	gtimer_t   _timeout;

	bool       _pressed;
	bool       _inverse;
	bool       _clicked;
	bool       _holded;

	uint32_t   _hold_ms;
	gtimer_t   _hold;
} button_t;


void button_create(
	button_t*   button,
	port_pin_t* pin,
	bool        inverse,
	uint32_t    hold_ms
);
void button_reset(button_t* button);
void button_tick(button_t* button);

bool button_one_click(button_t* button);
bool button_holded(button_t* button);
bool button_pressed(button_t* button);

#endif

#ifdef __cplusplus
}
#endif


#endif
