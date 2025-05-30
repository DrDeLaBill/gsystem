/* Copyright © 2025 Georgy E. All rights reserved. */

#ifndef _G_HRF_H_
#define _G_HRF_H_


#ifdef __cplusplus
extern "C" {
#endif


#if defined(USE_HAL_DRIVER)
    #include "hal/g_hal.h"
#elif defined(NRF52)
    #include "nrf/g_nrf.h"
#else
    #error "No driver selected"
#endif


#include <stdint.h>
#include <stdbool.h>


#define TIMESTAMP2000_01_01_00_00_00 (946670400)

#define SECOND_MS                    ((uint32_t)(1000))

#define MINUTE_S                     ((uint32_t)(60))
#define MINUTE_MS                    ((uint32_t)(MINUTE_S * SECOND_MS))

#define HOUR_MIN                     ((uint32_t)(60))
#define HOUR_MS                      ((uint32_t)(HOUR_MIN * MINUTE_MS))

#define DAY_H                        ((uint32_t)(24))
#define DAY_MS                       ((uint32_t)(DAY_H * HOUR_MS))

#define WEEK_D                       ((uint32_t)(7))
#define WEEK_MS                      ((uint32_t)(WEEK_D * DAY_MS))

#define BITS_IN_BYTE                 (8)


typedef struct _port_pin_t {
	hard_port_t* port;
    uint16_t     pin;
} port_pin_t;


void g_reboot();

void g_restart_check();

uint32_t g_get_freq();

uint32_t* g_heap_start();
uint32_t* g_stack_end();

bool g_pin_read(port_pin_t pin);

char* g_serial_number();


#ifdef __cplusplus
}
#endif

#endif
