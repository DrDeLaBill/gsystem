/* Copyright © 2024 Georgy E. All rights reserved. */

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

	static unsigned counter = 0;
	if (counter >= buttons_count) {
		counter = 0;
	}

	button_tick(&buttons[counter++]);
}

#endif