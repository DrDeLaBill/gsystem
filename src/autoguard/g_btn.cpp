/* Copyright © 2024 Georgy E. All rights reserved. */

#include "gdefines.h"
#include "gconfig.h"


#if GSYSTEM_BUTTONS_COUNT > 0

#   include "button.h"

extern "C" void btn_watchdog_check()
{
	extern unsigned buttons_count;
	extern button_t buttons[GSYSTEM_BUTTONS_COUNT];

	if (!buttons_count) {
		return;
	}

	for (unsigned i = 0; i < buttons_count; i++) {
		button_tick(&buttons[i]);
	}
}

#endif
