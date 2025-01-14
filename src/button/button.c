/* Copyright © 2024 Georgy E. All rights reserved. */

#include "button.h"

#include "bmacro.h"


#define BUTTON_DEBOUNCE_MS (60)


const uint32_t DEFAULT_HOLD_TIME_MS = 1000;


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

	button->_curr_state  = button_pressed(button);

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

	bool state = button_pressed(button);
	if (!state) {
		gtimer_start(&button->_hold, button->_hold_ms);
	} else if (!gtimer_wait(&button->_hold)) {
		gtimer_start(&button->_debounce, button->_debounce_ms);
		button->_curr_state = false;
		return;
	}
	if (state == button->_curr_state) {
		return;
	} else if (state) {
		gtimer_start(&button->_hold, button->_hold_ms);
	}
	gtimer_start(&button->_debounce, button->_debounce_ms);

	if (button->_holded) {
		button->_clicked = false;
	} else {
		button->_clicked = !state;
	}
	button->_curr_state = state;
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
	if (!button_pressed(button)) {
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
	bool state = HAL_GPIO_ReadPin(button->_pin.port, button->_pin.pin);
	return button->_inverse ? !state : state;
}
