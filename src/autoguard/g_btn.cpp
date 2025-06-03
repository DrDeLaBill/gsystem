/* Copyright © 2024 Georgy E. All rights reserved. */

#include "gdefines.h"
#include "gconfig.h"


#if GSYSTEM_BUTTONS_COUNT

#   include "button.h"


unsigned buttons_count = 0;
button_t buttons[GSYSTEM_BUTTONS_COUNT] = {0};


extern "C" void btn_watchdog_check()
{
	for (unsigned i = 0; i < buttons_count; i++) {
		button_tick(&buttons[i]);
	}
}

extern "C" void system_add_button(port_pin_t pin, bool inverse)
{
    if (buttons_count >= __arr_len(buttons)) {
        return;
    }
    button_create(&buttons[buttons_count++], &pin, inverse, DEFAULT_HOLD_TIME_MS);
}

extern "C" button_t* _find_button(port_pin_t pin)
{
    button_t* btn = NULL;
    for (unsigned i = 0; i < buttons_count; i++) {
        if (buttons[i]._pin.port == pin.port && buttons[i]._pin.pin == pin.pin) {
            btn = &buttons[i];
            break;
        }
    }
    return btn;
}

extern "C" uint32_t system_button_clicks(port_pin_t pin)
{
    button_t* btn = _find_button({pin.port, pin.pin});
    if (!btn) {
        return false;
    }
    return button_clicks(btn);
}

extern "C" bool system_button_pressed(port_pin_t pin)
{
    button_t* btn = _find_button({pin.port, pin.pin});
    if (!btn) {
        return false;
    }
    return button_pressed(btn);
}

extern "C" uint32_t system_button_held_ms(port_pin_t pin)
{
    button_t* btn = _find_button({pin.port, pin.pin});
    if (!btn) {
        return false;
    }
    return button_held_ms(btn);
}

extern "C" void system_buttons_reset()
{
	for (unsigned i = 0; i < buttons_count; i++) {
		button_reset(&buttons[i]);
	}
}
#else


extern "C" void btn_watchdog_check() {}

extern "C" void system_add_button(port_pin_t, bool) {}

extern "C" uint32_t system_button_clicks(port_pin_t) { return 0; }

extern "C" bool system_button_pressed(port_pin_t) { return false; }

extern "C" uint32_t system_button_held_ms(port_pin_t) { return 0; }

extern "C" void system_buttons_reset() {}

#endif
