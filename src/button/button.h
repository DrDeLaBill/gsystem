/* Copyright © 2024 Georgy E. All rights reserved. */

#ifndef _BUTTON_H_
#define _BUTTON_H_


#ifdef __cplusplus
extern "C" {
#endif


#include <stdint.h>

#include "gutils.h"


extern const uint32_t DEFAULT_HOLD_TIME_MS;


typedef struct _button_t {
	util_port_pin_t _pin;

	uint32_t        _debounce_ms;
	gtimer_t        _debounce;

	bool            _pressed;
	bool            _inverse;
	bool            _clicked;
	bool            _holded;

	uint32_t        _hold_ms;
	gtimer_t        _hold;
} button_t;


void button_create(
	button_t*        button,
	util_port_pin_t* pin,
	bool             inverse,
	uint32_t         hold_ms
);

void button_tick(button_t* button);

bool button_one_click(button_t* button);
bool button_holded(button_t* button);
bool button_pressed(button_t* button);

#ifdef __cplusplus
}
#endif


#endif
