/*
 * @file soul.c
 * @brief Centralized status and error bitmap implementation.
 *
 * This compilation unit stores SOUL_STATUS bitmaps and provides accessor
 * helpers used throughout the system to set/reset/check status and error
 * flags. Internal data structures and debug flags are file-local to avoid
 * accidental external manipulation.
 * 
 * Copyright © 2024 Georgy E. All rights reserved.
 */

#include "soul.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "gdefines.h"
#include "gconfig.h"
#include "gsystem.h"

#include "glog.h"
#include "bmacro.h"


#if !defined(GSYSTEM_NO_STATUS_PRINT) && (defined(DEBUG) || defined(GBEDUG_FORCE))
	#define __G_SOUL_BEDUG
#endif


#if defined(__G_SOUL_BEDUG)
/* @brief Debug log tag for soul subsystem (only present in debug builds). */
static const char TAG[] = "SOUL";
#endif


typedef enum _type_t {
	TYPE_STATUS = 0,
	TYPE_ERROR
} type_t;


/* 
 * @brief Internal bitmap holding status flags and last error struct. Not exposed outside
 *        this file. The layout uses a compact bitset for storage efficiency. 
 */
typedef struct _soul_t {
#if defined(__G_SOUL_BEDUG)
	bool has_new_error_data;
	bool has_new_status_data;
#endif
	SOUL_STATUS last_err;
	uint8_t statuses[__div_up(SOUL_STATUSES_END, BITS_IN_BYTE)];
} soul_t;


static soul_t soul = {
#if defined(__G_SOUL_BEDUG)
	.has_new_error_data  = false,
	.has_new_status_data = false,
#endif
	.last_err            = 0,
	.statuses            = { 0 }
};


/* @brief Fallback name returned for unknown statuses when no custom name is set. */
const char *SOUL_UNKNOWN_STATUS = "UNKNOWN_STATUS";


bool _is_status(SOUL_STATUS status);
void _set_status(SOUL_STATUS status);
void _reset_status(SOUL_STATUS status);
void _show_not_status(type_t type, SOUL_STATUS status, unsigned line);


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
	_show_not_status(TYPE_ERROR, error, __LINE__);
	return false;
}

void set_internal_error(SOUL_STATUS error)
{
	if (error > ERRORS_START && error < ERRORS_END) {
#if defined(__G_SOUL_BEDUG)
		if (!_is_status(error)) {
			soul.has_new_error_data = true;
		}
#endif
		_set_status(error);
	} else {
		_show_not_status(TYPE_ERROR, error, __LINE__);
	}
}

void reset_internal_error(SOUL_STATUS error)
{
	if (error > ERRORS_START && error < ERRORS_END) {
#if defined(__G_SOUL_BEDUG)
		if (_is_status(error)) {
			soul.has_new_error_data = true;
		}
#endif
		_reset_status(error);
	} else {
		_show_not_status(TYPE_ERROR, error, __LINE__);
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
	_show_not_status(TYPE_STATUS, status, __LINE__);
	return false;
}

void set_internal_status(SOUL_STATUS status)
{
	if (status > STATUSES_START && status < STATUSES_END) {
#if defined(__G_SOUL_BEDUG)
		if (!_is_status(status)) {
			soul.has_new_status_data = true;
		}
#endif
		_set_status(status);
	} else {
		_show_not_status(TYPE_STATUS, status, __LINE__);
	}
}

void reset_internal_status(SOUL_STATUS status)
{
	if (status > STATUSES_START && status < STATUSES_END) {
#if defined(__G_SOUL_BEDUG)
		if (_is_status(status)) {
			soul.has_new_status_data = true;
		}
#endif
		_reset_status(status);
	} else {
		_show_not_status(TYPE_STATUS, status, __LINE__);
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

void _show_not_status(type_t type, SOUL_STATUS status, unsigned line)
{
	BEDUG_ASSERT(status > SOUL_STATUSES_START && status < SOUL_STATUSES_END, "The value of the status is not in soul statuses array range");
	char* type_name = NULL;
	switch (type) {
	case TYPE_STATUS:
		type_name = __STR_DEF__(TYPE_STATUS);
		break;
	case TYPE_ERROR:
		type_name = __STR_DEF__(TYPE_ERROR);
		break;
	default:
		BEDUG_ASSERT(false, "Unknown type of soul statuses");
		return;
	}
	SYSTEM_BEDUG("Soul status error: %s status is not %s. Line %u.", get_status_name(status), type_name, line);
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
#if defined(__G_SOUL_BEDUG)
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
	CASE_STATUS(SETTINGS_STOPPED)
	CASE_STATUS(NEED_LOAD_SETTINGS)
	CASE_STATUS(NEED_SAVE_SETTINGS)
	CASE_STATUS(GSYS_ADC_READY)
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
	case STATUSES_START:
	case STATUSES_END:
	case NO_ERROR:
	case ERRORS_START:
	case ERRORS_END:
	case SOUL_STATUSES_END:
		snprintf(name, sizeof(name) - 1, "EMPTY STATUS");
		break;
	case RESERVED_STATUS_01:
	case RESERVED_STATUS_02:
	case RESERVED_STATUS_03:
	case RESERVED_STATUS_04:
	case RESERVED_STATUS_05:
	case RESERVED_STATUS_06:
	case RESERVED_STATUS_07:
	case RESERVED_STATUS_08:
	case RESERVED_STATUS_09:
	case RESERVED_STATUS_10:
	case RESERVED_STATUS_11:
	case RESERVED_STATUS_12:
	case RESERVED_STATUS_13:
	case RESERVED_STATUS_14:
	case RESERVED_STATUS_15:
	case RESERVED_ERROR_01:
	case RESERVED_ERROR_02:
	case RESERVED_ERROR_03:
	case RESERVED_ERROR_04:
	case RESERVED_ERROR_05:
	case RESERVED_ERROR_06:
	case RESERVED_ERROR_07:
	case RESERVED_ERROR_08:
	case RESERVED_ERROR_09:
	case RESERVED_ERROR_10:
	case RESERVED_ERROR_11:
	case RESERVED_ERROR_12:
	case RESERVED_ERROR_13:
	case RESERVED_ERROR_14:
	case RESERVED_ERROR_15:
	default:
		snprintf(name, sizeof(name) - 1, "[%03u] %s", status, get_custom_status_name(status));
		break;
	}
	return name;
#else
	snprintf(name, sizeof(name) - 1, "[%03u] %s", status, SOUL_UNKNOWN_STATUS);
	return name;
#endif
}

__attribute__((weak)) char* get_custom_status_name(SOUL_STATUS status)
{
	(void)status;
	return (char*)SOUL_UNKNOWN_STATUS;
}
#undef CASE_STATUS

#if defined(__G_SOUL_BEDUG)

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

bool is_soul_bedug_enable()
{
	return true;
}

#else

bool has_new_error_data() {return false;}
bool has_new_status_data() {return false;}
void show_errors() {}
void show_statuses() {}
bool is_soul_bedug_enable() {return false;}

#endif
