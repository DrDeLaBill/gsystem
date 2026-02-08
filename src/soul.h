/*
 * @file soul.h
 * @brief System status and error code definitions for gsystem library.
 *
 * Defines enumerations for device lifecycle statuses (hardware/software startup,
 * initialization phases, readiness states) and error codes used throughout the
 * system for state tracking and error reporting. These enums are returned by
 * system functions and used in status propagation and error handling.
 *
 * Copyright © 2024 Georgy E. All rights reserved.
 */

#ifndef _SOUL_H_
#define _SOUL_H_

#ifdef __cplusplus
extern "C" {
#endif


#include <stdint.h>
#include <stdbool.h>

#include "gutils.h"
#include "drivers.h"

/*
 * Macro helpers (preferred usage)
 *
 * These macros are the recommended way to work with `SOUL_STATUS` flags in
 * application code. They call the underlying internal functions but provide
 * a concise and consistent API. Use `is_status`, `set_status`, `reset_status`
 * and their `error` counterparts instead of calling the `_internal_` functions
 * directly.
 *
 * @def is_error(STATUS)
 * @brief Return true if `STATUS` represents an internal error.
 * @param STATUS (SOUL_STATUS) - Status to check.
 * @return bool
 *
 * @def set_error(STATUS)
 * @brief Mark `STATUS` as an internal error.
 * @param STATUS (SOUL_STATUS) - Error to set.
 * @return void
 *
 * @def reset_error(STATUS)
 * @brief Clear `STATUS` internal error flag.
 * @param STATUS (SOUL_STATUS) - Error to clear.
 * @return void
 * 
 * @def is_status(STATUS)
 * @brief Return true if `STATUS` represents an internal status flag.
 * @param STATUS (SOUL_STATUS) - Status to check.
 * @return bool
 *
 * @def set_status(STATUS)
 * @brief Set `STATUS` as an internal status flag.
 * @param STATUS (SOUL_STATUS) - Status to set.
 * @return void
 *
 * @def reset_status(STATUS)
 * @brief Clear `STATUS` internal status flag.
 * @param STATUS (SOUL_STATUS) - Status to clear.
 * @return void
 * 
 */
#ifndef is_error
#   define is_error(STATUS) (is_internal_error((SOUL_STATUS)STATUS))
#endif
#ifndef set_error
#   define set_error(STATUS) (set_internal_error((SOUL_STATUS)STATUS))
#endif
#ifndef reset_error
#   define reset_error(STATUS) (reset_internal_error((SOUL_STATUS)STATUS))
#endif
#ifndef is_status
#   define is_status(STATUS) (is_internal_status((SOUL_STATUS)STATUS))
#endif
#ifndef set_status
#   define set_status(STATUS) (set_internal_status((SOUL_STATUS)STATUS))
#endif
#ifndef reset_status
#   define reset_status(STATUS) (reset_internal_status((SOUL_STATUS)STATUS))
#endif


typedef enum _SOUK_STATUS {
	SOUL_STATUSES_START = 0,
	/* Device statuses start */
	STATUSES_START,

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
	SETTINGS_STOPPED,
	NEED_LOAD_SETTINGS,
	NEED_SAVE_SETTINGS,
	GSYS_ADC_READY,
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


extern const char *SOUL_UNKNOWN_STATUS;


/*
 * @brief Get the last recorded error status.
 *        Typically used after an error reboot.
 * @param None
 * @return SOUL_STATUS - Last error code.
 */
SOUL_STATUS get_last_error();

/*
 * @brief Set the last error status recorded by the system.
 *        Typically used before performing an error reboot.
 * @param error (SOUL_STATUS) - Error code to record.
 * @return None
 */
void set_last_error(SOUL_STATUS error);

/*
 * @brief Check whether any error flags are currently set.
 * @param None
 * @return bool - true if errors are present.
 */
bool has_errors();

/*
 * @brief Query if a status value represents an internal error.
 * @note Prefer using the `is_error` macro in application code instead of
 *       calling this function directly. The macro maps to this function.
 * @param error (SOUL_STATUS) - Status to check.
 * @return bool - true if `error` is an internal error.
 */
bool is_internal_error(SOUL_STATUS error);

/*
 * @brief Mark an internal error status as active.
 * @note Prefer using the `set_error` macro in application code. This
 *       function implements the underlying behavior used by the macro.
 * @param error (SOUL_STATUS) - Error to set.
 * @return None
 */
void set_internal_error(SOUL_STATUS error);

/*
 * @brief Clear an internal error status.
 * @note Prefer using the `reset_error` macro in application code. This
 *       function implements the underlying behavior used by the macro.
 * @param error (SOUL_STATUS) - Error to clear.
 * @return None
 */
void reset_internal_error(SOUL_STATUS error);

/*
 * @brief Get the first error recorded in the system soul now.
 * @param None
 * @return SOUL_STATUS - First error code.
 */
SOUL_STATUS get_first_error();

/*
 * @brief Check if a status value is considered an internal status flag.
 * @param status (SOUL_STATUS) - Status to evaluate.
 * @return bool - true if status is internal.
 */
bool is_internal_status(SOUL_STATUS status);

/*
 * @brief Set an internal status flag.
 * @param status (SOUL_STATUS) - Status to set.
 * @return None
 */
void set_internal_status(SOUL_STATUS status);

/*
 * @brief Reset/clear an internal status flag.
 * @param status (SOUL_STATUS) - Status to clear.
 * @return None
 */
void reset_internal_status(SOUL_STATUS status);


#define CASE_CUSTOM_STATUS(SOUL_STATUS) case SOUL_STATUS:                \
											snprintf(                    \
												name,                    \
												sizeof(name) - 1,        \
												"%s",                    \
												__STR_DEF__(SOUL_STATUS) \
											);                           \
										break;


/*
 * @brief Convert a `SOUL_STATUS` into a human-readable name.
 * @param status (SOUL_STATUS) - Status to convert.
 * @return char* - Pointer to a static string with the status name.
 */
char* get_status_name(SOUL_STATUS status);

/*
 * @brief Convert a `SOUL_STATUS` into a human-readable name.
 * @note A weak default implementation of `get_custom_status_name()` is
 *       provided by the library which returns a generic string. To supply
 *       application-specific human-readable names for custom statuses, implement
 *       a non-weak `get_custom_status_name(SOUL_STATUS)` in your project; the
 *       linker will prefer the user definition over the weak default.
 * @param status (SOUL_STATUS) - Custom status to convert.
 * @return char* - Pointer to a static string with the custom status name.
 * @example char* get_custom_status_name(SOUL_STATUS status) {
 *              static char name[35] = { 0 };
 *              memset(name, 0, sizeof(name))
 *              switch (status) {
 * 				case MY_CUSTOM_STATUS:
 *                  SYSTEM_CASE_STATUS(name, CUSTOM_STATUS_01)
 *              default:
 *                  snprintf(name, sizeof(name) - 1, "%s", SOUL_UNKNOWN_STATUS);
 *                  break;
 *              }
 *              return name;
 *  	    }
 */
char* get_custom_status_name(SOUL_STATUS status);

/*
 * @brief Check whether there is new error data that hasn't been processed.
 * @param None
 * @return bool - true if new error data is available.
 */
bool has_new_error_data();

/*
 * @brief Check whether there is new status data that hasn't been processed.
 * @param None
 * @return bool - true if new status data is available.
 */
bool has_new_status_data();

/*
 * @brief Print or log collected error records to the debug output.
 * @param None
 * @return None
 */
void show_errors();

/*
 * @brief Print or log collected status records to the debug output.
 * @param None
 * @return None
 */
void show_statuses();

/*
 * @brief Query whether the soul debug/logging facility is enabled.
 * @param None
 * @return bool - true if soul debug is enabled.
 */
bool is_soul_bedug_enable();


#ifdef __cplusplus
}
#endif


#endif
