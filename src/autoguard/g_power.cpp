/* Copyright © 2024 Georgy E. All rights reserved. */

#include "gconfig.h"

#include "gsystem.h"
#include "hal_defs.h"


#if !defined(GSYSTEM_NO_POWER_W) && !defined(GSYSTEM_NO_ADC_W)
extern "C" void power_watchdog_check()
{
	if (!is_system_ready()) {
		return;
	}

	uint32_t voltage = get_system_power();

	if (STM_MIN_VOLTAGEx10 <= voltage && voltage <= STM_MAX_VOLTAGEx10) {
		reset_error(POWER_ERROR);
	} else {
		set_error(POWER_ERROR);
	}
}
#endif