/* Copyright © 2025 Georgy E. All rights reserved. */

#include "g_nrf.h"


#ifdef NRF52

    #ifdef ARDUINO
        #include <Arduino.h>
    #else
        #error "Please select your framework"
    #endif 


    #include "gsystem.h"


    #define VECTOR_TABLE_SIZE  0xD8
    #define VECTOR_TABLE_ALIGN __attribute__((aligned(0x200)))


extern "C" {
	extern uint32_t __HeapBase;
	extern uint32_t __StackTop;
}

extern "C" void g_reboot()
{
    NVIC_SystemReset();
}

// TODO: sys timers
extern "C" void g_timer_start(system_timer_t* timer, hard_tim_t* fw_tim, uint32_t delay_ms) {}

extern "C" bool g_timer_wait(system_timer_t* timer) 
{
    return false;
}

extern "C" void g_timer_stop(system_timer_t* timer) {}

extern "C" void g_restart_check() {}

extern "C" uint32_t g_get_freq()
{
  return 64000000;
}

// TODO: check memory map rules
extern "C" uint32_t* g_heap_start()
{
    return &__HeapBase;
}

extern "C" uint32_t* g_stack_end()
{
    return &__StackTop;
}

extern "C" bool g_pin_read(port_pin_t pin)
{
    return digitalRead(pin.pin);
}

extern "C" float g_temperature() { // TODO
  NRF_TEMP->TASKS_START = 1;
  while (NRF_TEMP->EVENTS_DATARDY == 0);
  NRF_TEMP->EVENTS_DATARDY = 0;
  int32_t raw_temp = NRF_TEMP->TEMP;
  return (raw_temp * 0.25f);
}

extern "C" uint64_t g_serial()
{
    uint64_t serial = 0;
    serial = (uint64_t)NRF_FICR->DEVICEADDR[1] << 32 | (uint64_t)NRF_FICR->DEVICEADDR[0];
    return serial;
}

extern "C" char* g_serial_number()
{
    static char str_uid[17] = {0};
    memset((void*)str_uid, 0, sizeof(str_uid));
    sprintf(str_uid, "%08lX%08lX", NRF_FICR->DEVICEADDR[1], NRF_FICR->DEVICEADDR[0]);
    return str_uid;
}


#endif