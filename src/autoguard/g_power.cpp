/* Copyright © 2024 Georgy E. All rights reserved. */

#include "gdefines.h"
#include "gconfig.h"

#include "gsystem.h"
#include "drivers.h"


#if !defined(GSYSTEM_NO_POWER_W) && !defined(GSYSTEM_NO_ADC_W)

extern const char SYSTEM_TAG[];

extern "C" void power_watchdog_check()
{
	if (!is_status(SYSTEM_HARDWARE_READY)) {
		return;
	}

	uint32_t voltage = get_system_power_v_x100();
	if (STM_MIN_VOLTAGEx100 <= voltage && voltage <= STM_MAX_VOLTAGEx100) {
		reset_error(POWER_ERROR);
	} else {
		printTagLog(SYSTEM_TAG, "NO POWER: %lu.%02lu", voltage / 100, voltage % 100);
		set_error(POWER_ERROR);
	}
}
#endif
