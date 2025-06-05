/* Copyright © 2025 Georgy E. All rights reserved. */

#ifndef _SYSTEM_H_
#define _SYSTEM_H_


#ifdef __cplusplus
extern "C" {
#endif


#include <stdio.h>
#include <stdint.h>

#include "gdefines.h"


#define GSYS_CALL_HARD_FAULT() { \
	volatile int _gsys_val1 = 1, _gsys_val2 = 1; \
	while(--_gsys_val1) _gsys_val2++; \
	volatile int _gsys_res = _gsys_val2 / _gsys_val1; \
}


typedef struct _system_timer_t {
    uint32_t    verif;
    hard_tim_t* tim;
    hard_tim_t  bkup_tim;
    bool        enabled;
    uint32_t    end;
    uint32_t    count;
} system_timer_t;

extern const uint32_t TIMER_VERIF_WORD;


void system_init(void);
void system_register(void (*process) (void), uint32_t delay_ms, bool work_with_error);
void set_system_timeout(uint32_t timeout_ms);
void system_start(void);
void system_reset(void);

void system_post_load(void);
void system_tick(void);

bool is_system_ready(void);
bool is_software_ready(void);

void system_error_handler(SOUL_STATUS error);
void system_before_reset(void);

void system_reset_i2c_errata(void);

uint64_t get_system_serial(void);
char* get_system_serial_str(void);

void system_error_loop(void);

void system_timer_start(system_timer_t* timer, hard_tim_t* fw_tim, uint32_t delay_ms);
bool system_timer_wait(system_timer_t* timer);
void system_timer_stop(system_timer_t* timer);

void system_add_button(port_pin_t pin, bool inverse);
uint32_t system_button_clicks(port_pin_t pin);
bool system_button_pressed(port_pin_t pin);
uint32_t system_button_held_ms(port_pin_t pin);
void system_buttons_reset();

#ifndef GSYSTEM_NO_ADC_W
uint32_t get_system_power_v_x100(void);
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
bool get_system_bckp(const uint8_t idx, uint8_t* data);
bool set_system_bckp(const uint8_t idx, const uint8_t data);
#endif

uint32_t get_system_freq(void);
void system_delay_us(uint32_t us);

#ifdef __cplusplus
}
#endif


#endif
