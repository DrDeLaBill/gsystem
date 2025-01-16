/* Copyright © 2024 Georgy E. All rights reserved. */

#include "soul.h"

#include <stdint.h>
#include <stdbool.h>

#include "glog.h"
#include "bmacro.h"


#if defined(DEBUG) || defined(GBEDUG_FORCE)
static const char TAG[] = "SOUL";
#endif

static soul_t soul = {
#if defined(DEBUG) || defined(GBEDUG_FORCE)
	.has_new_error_data  = false,
	.has_new_status_data = false,
#endif
	.last_err            = 0,
	.statuses            = { 0 }
};

const char *SOUL_UNKNOWN_STATUS = "UNKNOWN_STATUS";


bool _is_status(SOUL_STATUS status);
void _set_status(SOUL_STATUS status);
void _reset_status(SOUL_STATUS status);


SOUL_STATUS get_last_error()
{
	return soul.last_err;
}

void set_last_error(SOUL_STATUS error)
{
	if (ERRORS_START < error && error < ERRORS_END) {
		soul.last_err = error;
	}
}

bool has_errors()
{
	for (unsigned i = ERRORS_START + 1; i < ERRORS_END; i++) {
		if (_is_status((SOUL_STATUS)(i))) {
			return true;
		}
	}
	return false;
}

bool is_internal_error(SOUL_STATUS error)
{
	if (error > ERRORS_START && error < ERRORS_END) {
		return _is_status(error);
	}
	return false;
}

void set_internal_error(SOUL_STATUS error)
{
	if (error > ERRORS_START && error < ERRORS_END) {
#if defined(DEBUG) || defined(GBEDUG_FORCE)
		if (!_is_status(error)) {
			soul.has_new_error_data = true;
		}
#endif
		_set_status(error);
	}
}

void reset_internal_error(SOUL_STATUS error)
{
	if (error > ERRORS_START && error < ERRORS_END) {
#if defined(DEBUG) || defined(GBEDUG_FORCE)
		if (_is_status(error)) {
			soul.has_new_error_data = true;
		}
#endif
		_reset_status(error);
	}
}

SOUL_STATUS get_first_error()
{
	for (unsigned i = ERRORS_START + 1; i < ERRORS_END; i++) {
		if (_is_status((SOUL_STATUS)(i))) {
			return i;
		}
	}
	return NO_ERROR;
}

bool is_internal_status(SOUL_STATUS status)
{
	if (status > STATUSES_START && status < STATUSES_END) {
		return _is_status(status);
	}
	return false;
}

void set_internal_status(SOUL_STATUS status)
{
	if (status > STATUSES_START && status < STATUSES_END) {
#if defined(DEBUG) || defined(GBEDUG_FORCE)
		if (!_is_status(status)) {
			soul.has_new_status_data = true;
		}
#endif
		_set_status(status);
	}
}

void reset_internal_status(SOUL_STATUS status)
{
	if (status > STATUSES_START && status < STATUSES_END) {
#if defined(DEBUG) || defined(GBEDUG_FORCE)
		if (_is_status(status)) {
			soul.has_new_status_data = true;
		}
#endif
		_reset_status(status);
	}
}

bool _is_status(SOUL_STATUS status)
{
	return (bool)(
		(
			soul.statuses[status / BITS_IN_BYTE] >>
			(status % BITS_IN_BYTE)
		) & 0x01
	);
}

void _set_status(SOUL_STATUS status)
{
	soul.statuses[status / BITS_IN_BYTE] |= (0x01 << (status % BITS_IN_BYTE));
}

void _reset_status(SOUL_STATUS status)
{
	soul.statuses[status / BITS_IN_BYTE] &= (uint8_t)~(0x01 << (status % BITS_IN_BYTE));
}

#define CASE_STATUS(SOUL_STATUS) case SOUL_STATUS:                \
	                                 snprintf(                    \
									     name,                    \
									     sizeof(name) - 1,        \
									     "[%03u] %s",             \
									     SOUL_STATUS,             \
									     __STR_DEF__(SOUL_STATUS) \
									 );                           \
									 break;
char* get_status_name(SOUL_STATUS status)
{
	static char name[35] = { 0 };
	memset(name, 0, sizeof(name));

	switch (status) {
	CASE_STATUS(SYSTEM_ERROR_HANDLER_CALLED)
	CASE_STATUS(SYSTEM_HARDWARE_STARTED)
	CASE_STATUS(SYSTEM_HARDWARE_READY)
	CASE_STATUS(SYSTEM_SOFTWARE_STARTED)
	CASE_STATUS(SYSTEM_SOFTWARE_READY)
	CASE_STATUS(SYSTEM_SAFETY_MODE)
	CASE_STATUS(SYS_TICK_FAULT)
	CASE_STATUS(MEMORY_INITIALIZED)
	CASE_STATUS(MEMORY_READ_FAULT)
	CASE_STATUS(MEMORY_WRITE_FAULT)
	CASE_STATUS(NEED_MEASURE)
	CASE_STATUS(NEED_STANDBY)
	CASE_STATUS(SETTINGS_INITIALIZED)
	CASE_STATUS(NEED_LOAD_SETTINGS)
	CASE_STATUS(NEED_SAVE_SETTINGS)
	CASE_STATUS(MODBUS_FAULT)
	CASE_STATUS(PUMP_FAULT)
	CASE_STATUS(RTC_FAULT)
	CASE_STATUS(CAN_FAULT)
	CASE_STATUS(PLL_FAULT)
	CASE_STATUS(RTC_READY)
	CASE_STATUS(MCU_ERROR)
	CASE_STATUS(SYS_TICK_ERROR)
	CASE_STATUS(RTC_ERROR)
	CASE_STATUS(POWER_ERROR)
	CASE_STATUS(EXPECTED_MEMORY_ERROR)
	CASE_STATUS(MEMORY_ERROR)
	CASE_STATUS(STACK_ERROR)
	CASE_STATUS(RAM_ERROR)
	CASE_STATUS(SD_CARD_ERROR)
	CASE_STATUS(USB_ERROR)
	CASE_STATUS(SETTINGS_LOAD_ERROR)
	CASE_STATUS(APP_MODE_ERROR)
	CASE_STATUS(PUMP_ERROR)
	CASE_STATUS(VALVE_ERROR)
	CASE_STATUS(FATFS_ERROR)
	CASE_STATUS(LOAD_ERROR)
	CASE_STATUS(I2C_ERROR)
	CASE_STATUS(NON_MASKABLE_INTERRUPT)
	CASE_STATUS(HARD_FAULT)
	CASE_STATUS(MEM_MANAGE)
	CASE_STATUS(BUS_FAULT)
	CASE_STATUS(USAGE_FAULT)
	CASE_STATUS(ASSERT_ERROR)
	CASE_STATUS(ERROR_HANDLER_CALLED)
	CASE_STATUS(INTERNAL_ERROR)
	default:
		snprintf(name, sizeof(name) - 1, "[%03u] %s", status, get_custom_status_name(status));
		break;
	}

	return name;
}

__attribute__((weak)) char* get_custom_status_name(SOUL_STATUS status)
{
	(void)status;
	return (char*)SOUL_UNKNOWN_STATUS;
}
#undef CASE_STATUS

#if defined(DEBUG) || defined(GBEDUG_FORCE)

bool has_new_error_data()
{
	return soul.has_new_error_data;
}

bool has_new_status_data()
{
	return soul.has_new_status_data;
}

void show_statuses()
{
	soul.has_new_status_data = false;
	printTagLog(TAG, "Current device statuses:");

	unsigned cnt = 0;
	for (SOUL_STATUS i = STATUSES_START + 1; i < STATUSES_END; i++) {
		if (!_is_status(i)) {
			continue;
		}
		cnt++;
		printPretty("%s\n", get_status_name(i));
	}
	if (!cnt) {
		printPretty("NO_STATUSES\n");
	}
}

void show_errors()
{
	soul.has_new_error_data = false;
	printTagLog(TAG, "Current device errors:");

	unsigned cnt = 0;
	for (SOUL_STATUS i = ERRORS_START + 1; i < ERRORS_END; i++) {
		if (!is_error(i)) {
			continue;
		}
		cnt++;
		printPretty("%s\n", get_status_name(i));
	}
	if (!cnt) {
		printPretty("%s\n", __STR_DEF__(NO_ERROR));
	}
}

#endif
