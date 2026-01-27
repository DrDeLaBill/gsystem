/* Copyright © 2025 Georgy E. All rights reserved. */

#include "g_nrf.h"

#ifdef NRF52

#ifdef ARDUINO
    #include <Arduino.h>
#else
    #error "Please select your framework"
#endif 

#include <unistd.h>

#include "gconfig.h"
#include "gdefines.h"

#include "Timer.h"
#include "gsystem.h"


#define VECTOR_TABLE_SIZE  0xD8
#define VECTOR_TABLE_ALIGN __attribute__((aligned(0x200)))


extern "C" {
    extern uint32_t __data_start__;
    extern uint32_t __HeapLimit;
    extern uint32_t __HeapBase;
    extern uint32_t __StackTop;
    extern uint32_t __StackLimit;
}

extern "C" void g_reboot()
{
    NVIC_SystemReset();
}

// TODO: add returnto backup settings
extern "C" void g_timer_start(system_timer_t* timer, NRF_TIMER_Type* nrf_timer, uint32_t delay_ms)
{
    memset(timer, 0, sizeof(system_timer_t));

    nrf_timer->TASKS_STOP = 1;
    nrf_timer->TASKS_CLEAR = 1;

    nrf_timer->MODE      = TIMER_MODE_MODE_Timer;
    nrf_timer->BITMODE   = TIMER_BITMODE_BITMODE_32Bit;
    nrf_timer->PRESCALER = 4;

    uint32_t ticks = delay_ms * 1000;

    nrf_timer->CC[0] = ticks;

    nrf_timer->SHORTS = TIMER_SHORTS_COMPARE0_STOP_Enabled << TIMER_SHORTS_COMPARE0_STOP_Pos;

    nrf_timer->EVENTS_COMPARE[0] = 0;

    nrf_timer->TASKS_START = 1;

    timer->tim   = nrf_timer;
    timer->end   = 1;
    timer->count = 0;
    timer->verif = TIMER_VERIF_WORD;
}

extern "C" bool g_timer_wait(system_timer_t* timer)
{
    if (timer->tim->EVENTS_COMPARE[0]) {
        timer->tim->EVENTS_COMPARE[0] = 0;
        timer->count++;
    }
    return timer->count < timer->end;
}

extern "C" void g_timer_stop(system_timer_t* timer)
{
    if (!timer || !timer->tim) {
        return;
    }

    timer->tim->TASKS_STOP = 1;

    timer->tim->TASKS_CLEAR = 1;

    timer->tim->SHORTS = 0;

    timer->tim->EVENTS_COMPARE[0] = 0;

    timer->count = 0;
    timer->end   = 0;
    timer->verif = 0;
    timer->tim   = NULL;
}


extern "C" void g_restart_check() {}

extern "C" uint32_t g_get_freq()
{
  return 64000000;
}

extern "C" uint32_t* g_ram_start()
{
    return &__data_start__;
}

extern "C" uint32_t* g_ram_end()
{
    return &__StackTop;
}

extern "C" uint32_t* g_heap_start()
{
    return &__HeapBase;
}

extern "C" uint32_t* g_stack_end()
{
    return &__StackTop; // TODO: do lower
}

extern "C" void g_ram_fill()
{
    uint32_t *heap_end = (uint32_t*)sbrk(0);
    uint32_t *stack_limit = (uint32_t*)&__StackLimit;
    for (; heap_end < stack_limit; ++heap_end) {
        *heap_end = SYSTEM_CANARY_WORD;
    }
}

extern "C" uint32_t g_ram_measure_free()
{
    uint32_t *heap_end = (uint32_t*)sbrk(0);
    uint32_t *stack_limit = (uint32_t*)&__StackLimit;
    uint32_t cur = 0, max = 0;
    for (uint32_t *p = heap_end; p < stack_limit; ++p) {
        if (*p == SYSTEM_CANARY_WORD) {
            cur += 4;
        } else {
            if (cur > max) max = cur;
            cur = 0;
        }
    }
    if (cur > max) max = cur;
    return max;
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
    static bool init = false;
    static char str_uid[17] = {0};
    if (!init) {
        memset((void*)str_uid, 0, sizeof(str_uid));
        sprintf(str_uid, "%08lX%08lX", NRF_FICR->DEVICEADDR[1], NRF_FICR->DEVICEADDR[0]);
    }
    return str_uid;
}

extern "C" void g_uart_print(const char* data, const uint16_t len)
{
    (void)data;
    (void)len;
    for (uint16_t i = 0; i < len; i++) {
        Serial.write(data[i]);
    }
}

extern "C" void g_delay_ms(const uint32_t ms)
{
    utl::Timer timer(ms);
    timer.start();
    while (timer.wait());
}

#endif // #ifdef NRF52
