/* Copyright © 2024 Georgy E. All rights reserved. */

#include "gconfig.h"

#include "gsystem.h"


#ifndef GSYSTEM_NO_I2C_W
extern "C" void i2c_watchdog_check()
{
	if (!is_error(I2C_ERROR)) {
		return;
	}

	system_reset_i2c_errata();
}

#endif