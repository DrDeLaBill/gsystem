/*
 * @file gdefines.h
 * @brief Global configuration macros and debug utilities for gsystem.
 *
 * Defines compile-time configuration options (ADC count, button count, etc),
 * debug/logging macros, status reporting utilities, and feature guards
 * (GSYSTEM_NO_*_W) to conditionally include/exclude components.
 *
 * Debug output requires GSYSTEM_BEDUG to be enabled via DEBUG or GBEDUG_FORCE.
 *
 * Copyright © 2025 Georgy E. All rights reserved.
 */

#ifndef _DEFINES_H_
#define _DEFINES_H_


#ifdef __cplusplus
extern "C" {
#endif


#include "gconfig.h"

/*
 * @def SYSTEM_CASE_STATUS(TARGET, STATUS)
 * @brief Helper macro used in switch statement in get_custom_status_name() 
 *        to convert status enums into human-readable strings.
 * @param TARGET (char[]) - Destination string buffer.
 * @param STATUS (enum) - Status enum identifier to convert.
 */
#define SYSTEM_CASE_STATUS(TARGET, STATUS) \
                                        case STATUS:                 \
                                            snprintf(                \
                                                TARGET,              \
                                                sizeof(TARGET) - 1,  \
                                                "%s",                \
                                                __STR_DEF__(STATUS)  \
                                            );                       \
                                            break;


/*
 * @def SYSTEM_CANARY_WORD
 * @brief Canary value used for simple integrity checks in memory structures.
 *        Use this constant when embedding a sentinel word to detect corruption.
 */
#define SYSTEM_CANARY_WORD              ((uint32_t)0xBEDAC0DE)


#if !defined(STM32F1) && defined(GSYSTEM_NO_I2C_W)
    #undef GSYSTEM_NO_I2C_W
#endif

#ifndef GSYSTEM_ADC_VOLTAGE_COUNT
    #define GSYSTEM_ADC_VOLTAGE_COUNT   (1)
#endif

#ifndef GSYSTEM_BUTTONS_COUNT
    #define GSYSTEM_BUTTONS_COUNT       (10)
#endif

#ifndef BUILD_VERSION
    #define BUILD_VERSION               "v0.0.0"
#endif

#if defined(DEBUG)
    #define GSYSTEM_BEDUG 1
#elif defined(GBEDUG_FORCE)
    #define GSYSTEM_BEDUG 1
#endif

#ifdef GSYSTEM_NO_BEDUG
    #undef GSYSTEM_BEDUG
#endif

#if GSYSTEM_BEDUG
extern const char SYSTEM_TAG[];
#endif

#if !GSYSTEM_RESET_TIMEOUT_MS
    #ifdef GSYSTEM_RESET_TIMEOUT_MS
        #undef GSYSTEM_RESET_TIMEOUT_MS
    #endif
    #define GSYSTEM_RESET_TIMEOUT_MS (30 * SECOND_MS)
#endif

#if defined(GSYSTEM_BEDUG_UART) && !GSYSTEM_BEDUG
    #undef GSYSTEM_BEDUG_UART
#endif

#if defined(GSYSTEM_DS1302_CLOCK) || defined(GSYSTEM_DS1307_CLOCK)
    #define GSYSTEM_DS130X_CLOCK
#endif
#if defined(GSYSTEM_DS130X_CLOCK)
    #define SYSTEM_BKUP_SIZE (DS130X_REG_RAM_END - DS130X_REG_RAM_BEGIN - sizeof(SOUL_STATUS))
#elif !defined(GSYSTEM_NO_RTC_W)
    #define SYSTEM_BKUP_SIZE (RTC_BKP_NUMBER - RTC_BKP_DR2 - sizeof(SOUL_STATUS))
#endif

#if !defined(GSYSTEM_NO_RTC_INTERNAL_W) && (defined(GSYSTEM_DS1302_CLOCK) || defined(GSYSTEM_DS1307_CLOCK))
    #define GSYSTEM_DOUBLE_BKCP_ENABLE
#endif

#ifndef GSYSTEM_POCESSES_COUNT
   #define GSYSTEM_POCESSES_COUNT (32)
#endif


// Min gsystem proccesses counter
#ifndef GSYSTEM_NO_MEMORY_W
    #define __GC_CNT_MEMORY (1)
#else
    #define __GC_CNT_MEMORY (0)
#endif
#ifndef GSYSTEM_NO_SYS_TICK_W
    #define __GC_CNT_SYS_TICK (1)
#else
    #define __GC_CNT_SYS_TICK (0)
#endif
#ifndef GSYSTEM_NO_RAM_W
    #define __GC_CNT_RAM (1)
#else
    #define __GC_CNT_RAM (0)
#endif
#if !defined(GSYSTEM_NO_ADC_W)
    #define __GC_CNT_ADC (1)
#else
    #define __GC_CNT_ADC (0)
#endif
#if defined(STM32F1) && !defined(GSYSTEM_NO_I2C_W)
    #define __GC_CNT_I2C (1)
#else
    #define __GC_CNT_I2C (0)
#endif
#if !defined(GSYSTEM_NO_POWER_W) && !defined(GSYSTEM_NO_ADC_W)
    #define __GC_CNT_POWER (1)
#else
    #define __GC_CNT_POWER (0)
#endif
#ifndef GSYSTEM_NO_RTC_W
    #define __GC_CNT_RTC (1)
#else
    #define __GC_CNT_RTC (0)
#endif
#ifndef GSYSTEM_NO_DEVICE_SETTINGS
    #define __GC_CNT_SETTINGS (1)
#else
    #define __GC_CNT_SETTINGS (0)
#endif

#define GSYSTEM_MIN_PROCCESS_CNT \
    (4 + __GC_CNT_MEMORY + __GC_CNT_SYS_TICK + __GC_CNT_RAM + __GC_CNT_ADC + __GC_CNT_I2C + __GC_CNT_POWER + __GC_CNT_RTC + __GC_CNT_SETTINGS)


#ifdef __cplusplus
}
#endif

#endif
