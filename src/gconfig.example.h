/* Copyright © 2024 Georgy E. All rights reserved. */

#ifndef _G_SYSTEM_CONFIG_EXAMPLE_H_
#define _G_SYSTEM_CONFIG_EXAMPLE_H_


#include "soul.h"


#ifdef __cplusplus
extern "C" {
#endif


#include "soul.h"

//#define GSYSTEM_NO_RESTART_W
//#define GSYSTEM_NO_RTC_W
//#define GSYSTEM_NO_SYS_TICK_W
//#define GSYSTEM_NO_RAM_W
//#define GSYSTEM_NO_ADC_W
//#define GSYSTEM_NO_POWER_W
//#define GSYSTEM_NO_MEMORY_W

//#define GSYSTEM_ADC_VOLTAGE_COUNT (3)

//#define GSYSTEM_FLASH_MODE
//#define GSYSTEM_EEPROM_MODE

//#define GSYSTEM_DS1307_CLOCK

// #define GSYSTEM_TIMER             (TIM1)

// #define GSYSTEM_BEDUG_UART        (huart1)


typedef enum _CUSTOM_SOUL_STATUSES {
	CUSTOM_STATUS_01 = RESERVED_STATUS_01,
	CUSTOM_STATUS_02 = RESERVED_STATUS_02
} CUSTOM_SOUL_STATUSES;


#ifdef __cplusplus
}
#endif


#endif
