/* Copyright © 2025 Georgy E. All rights reserved. */

#include "g_nrf.h"


#ifdef NRF52

#   ifdef ARDUINO
#       include <Arduino.h>
#   else
#       error "Please select your framework"
#   endif


#   include "gsystem.h"


extern "C" {
	extern uint32_t __HeapBase;
	extern uint32_t __StackTop;
}


static void wdt_reset(void)
{
  const unsigned BASE_ADDR = 0x40010000;
  const unsigned START_REG = 0x000;
  const unsigned CRV_REG = 0x504;
  *((unsigned*)(BASE_ADDR + CRV_REG)) = 0;
  *((unsigned*)(BASE_ADDR + START_REG)) = 1;
  while(1);
}

extern "C" void g_reboot()
{
    wdt_reset();
}

// TODO: sys timers
extern "C" void g_timer_start(system_timer_t* timer, hard_tim_t* fw_tim, uint32_t delay_ms) {}

extern "C" bool g_timer_wait(system_timer_t* timer) 
{
    return false;
}

extern "C" void g_timer_stop(system_timer_t* timer) {}

extern "C" void g_restart_check() {}

uint32_t g_get_freq()
{
  return 64000000;
}

uint32_t* g_heap_start()
{
    return &__HeapBase;
}

uint32_t* g_stack_end()
{
    return &__StackTop;
}


#endif