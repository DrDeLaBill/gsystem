/* Copyright © 2025 Georgy E. All rights reserved. */

#ifndef _G_NRF_H_
#define _G_NRF_H_

#ifdef __cplusplus
extern "C" {
#endif


#ifdef NRF52


#   ifdef ARDUINO
#       include "nrf52.h"
#   else
#       error "Please select your framework"
#   endif


#   define  hard_tim_t      NRF_TIMER_Type

#   define GSYS_DEFAULT_TIM (NRF_TIMER0)


#endif


#ifdef __cplusplus
}
#endif

#endif