/* Copyright © 2024 Georgy E. All rights reserved. */

#include "gdefines.h"
#include "gconfig.h"

#include "gsystem.h"
#include "drivers.h"


#if !defined(GSYSTEM_NO_POWER_W) && !defined(GSYSTEM_NO_ADC_W)

extern const char SYSTEM_TAG[];


#if !defined(GSYSTEM_NO_RTC_W)
extern "C" bool __internal_is_clock_ready();
extern "C" bool __internal_set_clock_ram(const uint8_t idx, uint8_t data);
#endif


extern "C" void power_watchdog_check()
{
	if (!is_status(GSYS_ADC_READY)) {
		return;
	}

	uint32_t voltage = get_system_power_v_x100();
	if (voltage > STM_MAX_VOLTAGEx100) {
		SYSTEM_BEDUG("WARNING! CPU POWER: %lu.%02lu V", voltage / 100, voltage % 100);
	}
	if (STM_MIN_VOLTAGEx100 <= voltage) {
		reset_error(POWER_ERROR);
	} else {
#if defined(GSYSTEM_DOUBLE_BKCP_ENABLE)
	    if (__internal_is_clock_ready()) {
	    	SOUL_STATUS error = POWER_ERROR;
	        for (uint8_t i = 0; i < sizeof(error); i++) {
	        	__internal_set_clock_ram(i, ((uint8_t*)&error)[i]);
	        }
	    }
#endif
		SYSTEM_BEDUG("CPU POWER ERROR: %lu.%02lu V", voltage / 100, voltage % 100);
		system_error_handler(POWER_ERROR);
	}
}
#endif
