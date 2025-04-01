/* Copyright © 2024 Georgy E. All rights reserved. */

#include "gdefines.h"
#include "gconfig.h"


#if GSYSTEM_BUTTONS_COUNT > 0

#   include "button.h"


extern unsigned buttons_count;
extern button_t buttons[GSYSTEM_BUTTONS_COUNT];


extern "C" void btn_watchdog_check()
{
	for (unsigned i = 0; i < buttons_count; i++) {
		button_tick(&buttons[i]);
	}
}

extern "C" void system_add_button(GPIO_TypeDef* port, uint16_t pin, bool inverse)
{
    if (buttons_count >= __arr_len(buttons)) {
        return;
    }
    util_port_pin_t tmp_pin = {port, pin};
    button_create(&buttons[buttons_count++], &tmp_pin, inverse, DEFAULT_HOLD_TIME_MS);
}

extern "C" button_t* _find_button(GPIO_TypeDef* port, uint16_t pin)
{
    button_t* btn = NULL;
    for (unsigned i = 0; i < buttons_count; i++) {
        if (buttons[i]._pin.port == port && buttons[i]._pin.pin == pin) {
            btn = &buttons[i];
            break;
        }
    }
    return btn;
}

extern "C" bool system_button_clicked(GPIO_TypeDef* port, uint16_t pin)
{
    button_t* btn = _find_button(port, pin);
    if (!btn) {
        return false;
    }
    return button_one_click(btn);
}

extern "C" bool system_button_pressed(GPIO_TypeDef* port, uint16_t pin)
{
    button_t* btn = _find_button(port, pin);
    if (!btn) {
        return false;
    }
    return button_pressed(btn);
}

extern "C" bool system_button_holded(GPIO_TypeDef* port, uint16_t pin)
{
    button_t* btn = _find_button(port, pin);
    if (!btn) {
        return false;
    }
    return button_holded(btn);
}

extern "C" void system_buttons_reset()
{
	for (unsigned i = 0; i < buttons_count; i++) {
		button_reset(&buttons[i]);
	}
}

#endif
