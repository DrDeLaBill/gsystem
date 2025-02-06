/* Copyright © 2024 Georgy E. All rights reserved. */

#include "button.h"


#include "gconfig.h"
#include "gdefines.h"


#if GSYSTEM_BUTTONS_COUNT

#include "glog.h"
#include "bmacro.h"
#include "fsm_gc.h"


#define BUTTON_DEBOUNCE_MS (60)


const uint32_t DEFAULT_HOLD_TIME_MS = 1000;


static bool _btn_pressed(button_t* button);
static void _btn_idle_action(button_t* button);
static void _btn_pressed_action(button_t* button);
static void _btn_holded_action(button_t* button);
static void _btn_clicked_action(button_t* button);


void button_create(
	button_t*        button,
	util_port_pin_t* pin,
	bool             inverse,
	uint32_t         hold_ms
) {
	if (!button || !pin) {
		BEDUG_ASSERT(false, "button or pin is null pointer");
		return;
	}

	button->_debounce_ms = BUTTON_DEBOUNCE_MS;
	button->_pin.port    = pin->port;
	button->_pin.pin     = pin->pin;
	button->_inverse     = inverse;
	button->_clicked     = false;
	button->_hold_ms     = hold_ms;
	button->_holded      = false;

	button->_pressed     = _btn_pressed(button);

	gtimer_reset(&button->_debounce);
	gtimer_reset(&button->_hold);
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

	if (button->_clicked) {
		_btn_clicked_action(button);
	} else if (button->_holded) {
		_btn_holded_action(button);
	} else if (button->_pressed) {
		_btn_pressed_action(button);
	} else {
		_btn_idle_action(button);
	}
}

bool button_one_click(button_t* button)
{
	if (!button) {
		BEDUG_ASSERT(false, "button is null pointer");
		return false;
	}
	if (button->_clicked) {
		button->_clicked = false;
		return true;
	}
	return false;
}

bool button_holded(button_t* button)
{
	if (!button) {
		BEDUG_ASSERT(false, "button is null pointer");
		return false;
	}
	if (!_btn_pressed(button)) {
		button->_holded = false;
		return false;
	}
	if (!gtimer_wait(&button->_hold)) {
		button->_holded = true;
	}
	return !gtimer_wait(&button->_hold);
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
	bool state = HAL_GPIO_ReadPin(button->_pin.port, button->_pin.pin);
	return button->_inverse ? !state : state;
}

void _btn_idle_action(button_t* button)
{
	gtimer_start(&button->_debounce, button->_debounce_ms);
	gtimer_start(&button->_hold, button->_hold_ms);
	button->_pressed = _btn_pressed(button);
	button->_clicked = false;
	button->_holded  = false;
	if (button->_pressed) {
#if GSYSTEM_BEDUG
		printTagLog(SYSTEM_TAG, "button [0x%08X-0x%02X]: pressed", (unsigned)button->_pin.port, button->_pin.pin);
#endif
	}
}

void _btn_pressed_action(button_t* button)
{
	if (gtimer_wait(&button->_debounce)) {
		return;
	}
	bool pressed = _btn_pressed(button);
	if (button->_pressed && !pressed) {
#if GSYSTEM_BEDUG
		printTagLog(SYSTEM_TAG, "button [0x%08X-0x%02X]: clicked", (unsigned)button->_pin.port, button->_pin.pin);
#endif
		gtimer_start(&button->_timeout, 10 * SECOND_MS);
		gtimer_start(&button->_hold, button->_hold_ms);
		button->_clicked = true;
	} else if (!gtimer_wait(&button->_hold)) {
#if GSYSTEM_BEDUG
		printTagLog(SYSTEM_TAG, "button [0x%08X-0x%02X]: holded", (unsigned)button->_pin.port, button->_pin.pin);
#endif
		button->_holded = true;
	}
	button->_pressed = pressed;
}

void _btn_holded_action(button_t* button)
{
	button->_pressed = _btn_pressed(button);
	if (!button->_pressed) {
#if GSYSTEM_BEDUG
		printTagLog(SYSTEM_TAG, "button [0x%08X-0x%02X]: not holded", (unsigned)button->_pin.port, button->_pin.pin);
#endif
		gtimer_start(&button->_debounce, button->_debounce_ms);
		button->_holded = false;
	}
}

void _btn_clicked_action(button_t* button)
{
	gtimer_start(&button->_hold, button->_hold_ms);
	button->_pressed = _btn_pressed(button);
	if (button->_pressed) {
		gtimer_start(&button->_debounce, button->_debounce_ms);
		button->_clicked = false;
	}
	if (!gtimer_wait(&button->_timeout)) {
#if GSYSTEM_BEDUG
		printTagLog(SYSTEM_TAG, "button [0x%08X-0x%02X]: click removed", (unsigned)button->_pin.port, button->_pin.pin);
#endif
		gtimer_start(&button->_debounce, button->_debounce_ms);
		button->_clicked = false;
	}
}

#endif
