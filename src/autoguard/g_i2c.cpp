/* Copyright © 2024 Georgy E. All rights reserved. */

#include "gdefines.h"
#include "gconfig.h"

#include "gsystem.h"


#ifndef GSYSTEM_NO_I2C_W
extern "C" void i2c_watchdog_check()
{
#ifdef GSYSTEM_DS1307_CLOCK
	if (!is_error(I2C_ERROR) && !is_error(RTC_ERROR)) {
#else
	if (!is_error(I2C_ERROR)) {
#endif
		return;
	}

	system_reset_i2c_errata();
}

#endif
