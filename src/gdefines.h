/* Copyright © 2025 Georgy E. All rights reserved. */

#ifndef _DEFINES_H_
#define _DEFINES_H_


#ifdef __cplusplus
extern "C" {
#endif


#include "gconfig.h"


#define SYSTEM_CASE_STATUS(TARGET, STATUS) case STATUS:                 \
                                               snprintf(                \
                                                   TARGET,              \
                                                   sizeof(TARGET) - 1,  \
                                                   "%s",                \
                                                   __STR_DEF__(STATUS)  \
                                               );                       \
                                               break;

#define SYSTEM_CANARY_WORD                 ((uint32_t)0xBEDAC0DE)

#if !defined(STM32F1) && defined(GSYSTEM_NO_I2C_W)
#   undef GSYSTEM_NO_I2C_W
#endif

#ifndef GSYSTEM_ADC_VOLTAGE_COUNT
#   define GSYSTEM_ADC_VOLTAGE_COUNT (1)
#endif

#ifndef GSYSTEM_BUTTONS_COUNT
#   define GSYSTEM_BUTTONS_COUNT     (10)
#endif

#ifndef BUILD_VERSION
#   define BUILD_VERSION             "v0.0.0"
#endif

#define GSYSTEM_BEDUG (defined(DEBUG) || defined(GBEDUG_FORCE))
#if GSYSTEM_BEDUG
extern const char SYSTEM_TAG[];
#endif

#if !GSYSTEM_RESET_TIMEOUT_MS
#   ifdef GSYSTEM_RESET_TIMEOUT_MS
#       undef GSYSTEM_RESET_TIMEOUT_MS
#   endif
#   define GSYSTEM_RESET_TIMEOUT_MS (30 * SECOND_MS)
#endif

#if GSYSTEM_BEDUG
#   define SYSTEM_BEDUG(FORMAT, ...) printTagLog(SYSTEM_TAG, FORMAT __VA_OPT__(,) __VA_ARGS__);
#else
#   define SYSTEM_BEDUG(FORMAT, ...)
#endif

#if defined(GSYSTEM_BEDUG_UART) && !GSYSTEM_BEDUG
#   undef GSYSTEM_BEDUG_UART
#endif

#if defined(GSYSTEM_DS1307_CLOCK)
#   define SYSTEM_BKUP_SIZE (DS1307_REG_RAM_END - DS1307_REG_RAM_BEGIN - sizeof(SOUL_STATUS))
#elif !defined(GSYSTEM_NO_RTC_W)
#   define SYSTEM_BKUP_SIZE (RTC_BKP_NUMBER - RTC_BKP_DR2 - sizeof(SOUL_STATUS))
#endif


#ifndef GSYSTEM_POCESSES_COUNT
#   define GSYSTEM_POCESSES_COUNT (32)
#endif


#ifdef __cplusplus
}
#endif

#endif
