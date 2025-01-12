/* Copyright © 2024 Georgy E. All rights reserved. */

#include "gconfig.h"

#include "gsystem.h"


#ifndef GSYSTEM_NO_SYS_TICK_W
extern "C" void sys_clock_watchdog_check()
{
	if (!is_error(SYS_TICK_ERROR) && !is_status(SYS_TICK_FAULT)) {
		return;
	}

	system_sys_tick_reanimation();
}
#endif
