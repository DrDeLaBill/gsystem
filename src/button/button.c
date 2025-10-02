/* Copyright © 2024 Georgy E. All rights reserved. */

#include "button.h"


#include "gconfig.h"
#include "gdefines.h"


#include "glog.h"
#include "bmacro.h"
#include "fsm_gc.h"
#include "gsystem.h"


#ifdef GSYSTEM_BUTTON_DEBOUNCE_MS
#   define BUTTON_DEBOUNCE_MS  (GSYSTEM_BUTTON_DEBOUNCE_MS)
#else
#   define BUTTON_DEBOUNCE_MS  (60)
#endif

#define BUTTON_CLICKS_DELAY_MS (400)


const uint32_t DEFAULT_HOLD_TIME_MS = 1000;


static bool _btn_pressed(button_t* button);
static void _btn_idle_action(button_t* button);
static void _btn_pressed_action(button_t* button);
static void _btn_held_action(button_t* button);
static void _btn_clicks_action(button_t* button);


void button_create(
	button_t*   button,
	port_pin_t* pin,
	bool        inverse,
	uint32_t    hold_ms
) {
	if (!button || !pin) {
		BEDUG_ASSERT(false, "button or pin is null pointer");
		return;
	}

	button->_debounce_ms = BUTTON_DEBOUNCE_MS;
	button->_pin.port    = pin->port;
	button->_pin.pin     = pin->pin;
	button->_inverse     = inverse;
	button->_clicks      = 0;
	button->_next_click  = false;
	button->_hold_ms     = hold_ms;
	button->_held        = false;

	button->_pressed     = _btn_pressed(button);

	gtimer_reset(&button->_debounce);
	gtimer_reset(&button->_held_tim);
	gtimer_reset(&button->_clicks_tim);
}

void button_tick(button_t* button)
{
	if (!button) {
		BEDUG_ASSERT(false, "button is null pointer");
		return;
	}

	if (gtimer_wait(&button->_debounce)) {
		return;
	}

	if (button->_clicks && !button->_next_click) {
		_btn_clicks_action(button);
	} else if (button->_held) {
		_btn_held_action(button);
	} else if (button->_pressed) {
		_btn_pressed_action(button);
	} else {
		_btn_idle_action(button);
	}
}

void button_reset(button_t* button)
{
	button->_clicks = 0;
	button->_held  = false;
	button->_pressed = false;
	gtimer_reset(&button->_debounce);
	gtimer_reset(&button->_held_tim);
	gtimer_reset(&button->_clicks_tim);
}

uint32_t button_clicks(button_t* button)
{
	if (!button) {
		BEDUG_ASSERT(false, "button is null pointer");
		return 0;
	}
	if (gtimer_wait(&button->_clicks_tim)) {
		return 0;
	}
	size_t tmp = button->_clicks;
	button->_clicks = 0;
	return tmp;
}

uint32_t button_held_ms(button_t* button)
{
	if (!button) {
		BEDUG_ASSERT(false, "button is null pointer");
		return false;
	}
	if (!_btn_pressed(button)) {
		gtimer_reset(&button->_held_tim);
		button->_held = false;
		return 0;
	}
	if (!gtimer_wait(&button->_held_tim)) {
		button->_held = true;
	}
	return (uint32_t)(button->_held_tim.start ? getMillis() - button->_held_tim.start : 0);
}

bool button_pressed(button_t* button)
{
	if (!button) {
		BEDUG_ASSERT(false, "button is null pointer");
		return false;
	}
	return gtimer_wait(&button->_debounce) ? button->_pressed : _btn_pressed(button);
}

bool _btn_pressed(button_t* button)
{
	if (!button) {
		BEDUG_ASSERT(false, "button is null pointer");
		return false;
	}
	bool state = g_pin_read(button->_pin);
	return button->_inverse ? !state : state;
}

void _btn_idle_action(button_t* button)
{
	gtimer_start(&button->_debounce, button->_debounce_ms);
	gtimer_start(&button->_held_tim, button->_hold_ms);
	button->_pressed = _btn_pressed(button);
	button->_clicks  = 0;
	button->_held  = false;
	if (button->_pressed) {
		SYSTEM_BEDUG("button [0x%08X-0x%02X]: pressed", (unsigned)button->_pin.port, button->_pin.pin);
		gtimer_start(&button->_clicks_tim, BUTTON_CLICKS_DELAY_MS);
	}
}

void _btn_pressed_action(button_t* button)
{
	if (gtimer_wait(&button->_debounce)) {
		return;
	}
	bool pressed = _btn_pressed(button);
	if (button->_pressed && !pressed) {
		SYSTEM_BEDUG("button [0x%08X-0x%02X]: clicked (%u times)", (unsigned)button->_pin.port, button->_pin.pin, button->_clicks);
		gtimer_start(&button->_timeout, 10 * SECOND_MS);
		gtimer_start(&button->_held_tim, button->_hold_ms);
		gtimer_start(&button->_clicks_tim, BUTTON_CLICKS_DELAY_MS);
		button->_next_click = false;
		button->_clicks++;
	} else if (!gtimer_wait(&button->_held_tim)) {
		SYSTEM_BEDUG("button [0x%08X-0x%02X]: held", (unsigned)button->_pin.port, button->_pin.pin);
		button->_held = true;
	}
	button->_pressed = pressed;
}

void _btn_held_action(button_t* button)
{
	button->_pressed = _btn_pressed(button);
	if (!button->_pressed) {
		SYSTEM_BEDUG("button [0x%08X-0x%02X]: not held", (unsigned)button->_pin.port, button->_pin.pin);
		gtimer_start(&button->_debounce, button->_debounce_ms);
		button->_held = false;
	}
}

void _btn_clicks_action(button_t* button)
{
	gtimer_start(&button->_held_tim, button->_hold_ms);
	button->_pressed = _btn_pressed(button);
	if (button->_pressed) {
		gtimer_start(&button->_debounce, button->_debounce_ms);
		gtimer_start(&button->_held_tim, button->_hold_ms);
		gtimer_start(&button->_clicks_tim, BUTTON_CLICKS_DELAY_MS);
		button->_next_click = true;
	}
	if (!gtimer_wait(&button->_timeout)) {
		SYSTEM_BEDUG("button [0x%08X-0x%02X]: click removed", (unsigned)button->_pin.port, button->_pin.pin);
		gtimer_start(&button->_debounce, button->_debounce_ms);
		button->_clicks = 0;
	}
}
