/* Copyright © 2024 Georgy E. All rights reserved. */

#ifndef _SYSTEM_H_
#define _SYSTEM_H_


#ifdef __cplusplus
extern "C" {
#endif


#include <stdio.h>
#include <stdint.h>

#include "soul.h"
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
#define SYSTEM_BKUP_STATUS_TYPE            uint32_t

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

void system_init(void);
void system_register(void (*process) (void), uint32_t delay_ms, bool work_with_error);
void set_system_timeout(uint32_t timeout_ms);
void system_start(void);

void system_post_load(void);
void system_tick(void);

bool is_system_ready(void);
bool is_software_ready(void);

void system_error_handler(SOUL_STATUS error);

void system_reset_i2c_errata(void);

char* get_system_serial_str(void);

void system_error_loop(void);


typedef struct _system_timer_t {
    uint32_t     verif;
    TIM_TypeDef* tim;
    TIM_TypeDef  bkup_tim;
    bool         enabled;
    uint32_t     end;
    uint32_t     count;
} system_timer_t;

void system_timer_start(system_timer_t* timer, TIM_TypeDef* fw_tim, uint32_t delay_ms);
bool system_timer_wait(system_timer_t* timer);
void system_timer_stop(system_timer_t* timer);

#if GSYSTEM_BUTTONS_COUNT
void system_add_button(GPIO_TypeDef* port, uint16_t pin, bool inverse);
bool system_button_clicked(GPIO_TypeDef* port, uint16_t pin);
bool system_button_pressed(GPIO_TypeDef* port, uint16_t pin);
bool system_button_holded(GPIO_TypeDef* port, uint16_t pin);
#endif

#ifndef GSYSTEM_NO_ADC_W
uint32_t get_system_power(void);
#endif

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
