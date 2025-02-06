/* Copyright © 2024 Georgy E. All rights reserved. */

#ifndef _SOUL_H_
#define _SOUL_H_

#ifdef __cplusplus
extern "C" {
#endif


#include <stdint.h>
#include <stdbool.h>

#include "gutils.h"
#include "hal_defs.h"


typedef enum _SOUK_STATUS {
	/* Device statuses start */
	STATUSES_START = 0,

	SYSTEM_ERROR_HANDLER_CALLED,
	SYSTEM_HARDWARE_STARTED,
	SYSTEM_HARDWARE_READY,
	SYSTEM_SOFTWARE_STARTED,
	SYSTEM_SOFTWARE_READY,
	SYSTEM_SAFETY_MODE,
	SYS_TICK_FAULT,
	RTC_READY,
	MEMORY_INITIALIZED,
	MEMORY_READ_FAULT,
	MEMORY_WRITE_FAULT,
	NEED_MEASURE,
	NEED_STANDBY,
	SETTINGS_INITIALIZED,
	NEED_LOAD_SETTINGS,
	NEED_SAVE_SETTINGS,
	MODBUS_FAULT,
	PUMP_FAULT,
	RTC_FAULT,
	CAN_FAULT,
	PLL_FAULT,

	RESERVED_STATUS_01,
	RESERVED_STATUS_02,
	RESERVED_STATUS_03,
	RESERVED_STATUS_04,
	RESERVED_STATUS_05,
	RESERVED_STATUS_06,
	RESERVED_STATUS_07,
	RESERVED_STATUS_08,
	RESERVED_STATUS_09,
	RESERVED_STATUS_10,
	RESERVED_STATUS_11,
	RESERVED_STATUS_12,
	RESERVED_STATUS_13,
	RESERVED_STATUS_14,
	RESERVED_STATUS_15,

	/* Device statuses end */
	STATUSES_END,

	/* Device errors start */
	NO_ERROR,

	ERRORS_START,

	RESERVED_ERROR_01,
	RESERVED_ERROR_02,
	RESERVED_ERROR_03,
	RESERVED_ERROR_04,
	RESERVED_ERROR_05,
	RESERVED_ERROR_06,
	RESERVED_ERROR_07,
	RESERVED_ERROR_08,
	RESERVED_ERROR_09,
	RESERVED_ERROR_10,
	RESERVED_ERROR_11,
	RESERVED_ERROR_12,
	RESERVED_ERROR_13,
	RESERVED_ERROR_14,
	RESERVED_ERROR_15,

	MCU_ERROR,
	SYS_TICK_ERROR,
	RTC_ERROR,
	POWER_ERROR,
	EXPECTED_MEMORY_ERROR,
	MEMORY_ERROR,
	STACK_ERROR,
	RAM_ERROR,
	SD_CARD_ERROR,
	USB_ERROR,
	SETTINGS_LOAD_ERROR,
	APP_MODE_ERROR,
	PUMP_ERROR,
	VALVE_ERROR,
	FATFS_ERROR,
	LOAD_ERROR,
	I2C_ERROR,

	NON_MASKABLE_INTERRUPT,
	HARD_FAULT,
	MEM_MANAGE,
	BUS_FAULT,
	USAGE_FAULT,

	ASSERT_ERROR,
	ERROR_HANDLER_CALLED,
	INTERNAL_ERROR,

	/* Device errors end */
	ERRORS_END,

	/* Paste device errors or statuses to the top */
	SOUL_STATUSES_END
} SOUL_STATUS;


typedef struct _soul_t {
#if defined(DEBUG) || defined(GBEDUG_FORCE)
	bool has_new_error_data;
	bool has_new_status_data;
#endif
	SOUL_STATUS last_err;
	uint8_t statuses[__div_up(SOUL_STATUSES_END, BITS_IN_BYTE)];
} soul_t;

extern const char *SOUL_UNKNOWN_STATUS;


SOUL_STATUS get_last_error();
void set_last_error(SOUL_STATUS error);

bool has_errors();

bool is_internal_error(SOUL_STATUS error);
void set_internal_error(SOUL_STATUS error);
void reset_internal_error(SOUL_STATUS error);
SOUL_STATUS get_first_error();

#ifndef is_error
#   define is_error(STATUS) (is_internal_error((SOUL_STATUS)STATUS))
#endif
#ifndef set_error
#   define set_error(STATUS) (set_internal_error((SOUL_STATUS)STATUS))
#endif
#ifndef reset_error
#   define reset_error(STATUS) (reset_internal_error((SOUL_STATUS)STATUS))
#endif

bool is_internal_status(SOUL_STATUS status);
void set_internal_status(SOUL_STATUS status);
void reset_internal_status(SOUL_STATUS status);

#ifndef is_status
#   define is_status(STATUS) (is_internal_status((SOUL_STATUS)STATUS))
#endif
#ifndef set_status
#   define set_status(STATUS) (set_internal_status((SOUL_STATUS)STATUS))
#endif
#ifndef reset_status
#   define reset_status(STATUS) (reset_internal_status((SOUL_STATUS)STATUS))
#endif


char* get_status_name(SOUL_STATUS status);
char* get_custom_status_name(SOUL_STATUS status);
bool has_new_error_data();
bool has_new_status_data();
void show_errors();
void show_statuses();


#ifdef __cplusplus
}
#endif


#endif
