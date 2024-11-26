/* Copyright © 2024 Georgy E. All rights reserved. */

#ifndef _SYSTEM_H_
#define _SYSTEM_H_


#ifdef __cplusplus
extern "C" {
#endif


#include <stdint.h>

#include "soul.h"
#include "gconfig.h"


#ifdef DEBUG
#   define SYSTEM_BEDUG (1)
#endif

#define SYSTEM_CANARY_WORD      ((uint32_t)0xBEDAC0DE)
#define SYSTEM_BKUP_STATUS_TYPE uint32_t

#ifndef GSYSTEM_ADC_VOLTAGE_COUNT
#   define GSYSTEM_ADC_VOLTAGE_COUNT (1)
#endif

void system_pre_load(void);
void system_registrate(void (*process) (void), uint32_t delay_ms, bool work_with_error);
void set_system_timeout(uint32_t timeout_ms);
void system_start();

void system_post_load(void);
void system_tick(void);
void system_ready_check(void);

bool is_system_ready(void);

void system_error_handler(SOUL_STATUS error);

#ifndef GSYSTEM_NO_ADC_W
uint32_t get_system_power(void);
#endif

void system_reset_i2c_errata(void);

char* get_system_serial_str(void);

void system_error_loop(void);

#ifndef GSYSTEM_NO_SYS_TICK_W
void system_hsi_config(void);
void system_hse_config(void);
void system_sys_tick_reanimation(void);
#endif

#ifndef GSYSTEM_NO_ADC_W
uint16_t get_system_adc(unsigned index);
#endif

#ifndef GSYSTEM_NO_RTC_W
bool get_system_rtc_ram(const uint8_t idx, uint8_t* data);
bool set_system_rtc_ram(const uint8_t idx, const uint8_t data);
#endif

#ifndef GSYSTEM_NO_PRINTF
int _write(int line, uint8_t *ptr, int len);
#endif

#ifdef __cplusplus
}
#endif


#endif
