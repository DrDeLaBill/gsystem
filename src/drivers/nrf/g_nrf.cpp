/* Copyright © 2025 Georgy E. All rights reserved. */

#include "g_nrf.h"

#ifdef NRF52

#ifdef ARDUINO
    #include <Arduino.h>
#else
    #error "Please select your framework"
#endif 

#include <unistd.h>

#include <nrf52_bitfields.h>

#include "gconfig.h"
#include "gdefines.h"

#include "Timer.h"
#include "gsystem.h"


#define VECTOR_TABLE_SIZE  0xD8
#define VECTOR_TABLE_ALIGN __attribute__((aligned(0x200)))


#define TIMER_IRQ_HANDLER(idx, reg) \
extern "C" void TIMER##idx##_IRQHandler() { \
    if (reg->EVENTS_COMPARE[0]) { \
        reg->EVENTS_COMPARE[0] = 0; \
        (void)reg->EVENTS_COMPARE[0]; \
        if (TIM_CALLBACKS[idx]) { \
            TIM_CALLBACKS[idx](); \
        } \
    } \
}


static void (*TIM_CALLBACKS[5])(void) = {NULL, NULL, NULL, NULL, NULL};


TIMER_IRQ_HANDLER(0, NRF_TIMER0)
TIMER_IRQ_HANDLER(1, NRF_TIMER1)
TIMER_IRQ_HANDLER(2, NRF_TIMER2)
TIMER_IRQ_HANDLER(3, NRF_TIMER3)
TIMER_IRQ_HANDLER(4, NRF_TIMER4)


extern "C" {
    extern uint32_t __data_start__;
    extern uint32_t __HeapLimit;
    extern uint32_t __HeapBase;
    extern uint32_t __StackTop;
    extern uint32_t __StackLimit;
}


static IRQn_Type tim_irqs[] = {
    TIMER0_IRQn,
    TIMER1_IRQn,
    TIMER2_IRQn,
    TIMER3_IRQn,
    TIMER4_IRQn
};

static hard_tim_t* sys_timer = NULL;


static int _get_timer_index(hard_tim_t* timer) 
{
    if (timer == NRF_TIMER0) return 0;
    if (timer == NRF_TIMER1) return 1;
    if (timer == NRF_TIMER2) return 2;
    if (timer == NRF_TIMER3) return 3;
    if (timer == NRF_TIMER4) return 4;
    return -1;
}

extern "C" uint32_t sys_time_ms;
static void _sys_timer_callback()
{
    sys_time_ms++;
}

extern "C" void g_reboot()
{
    NVIC_SystemReset();
}

uint32_t g_get_millis_hw_tim_presc()
{
    return 4;
}

uint32_t g_get_millis_hw_tim_count()
{
    return 1000;
}

bool g_sys_tick_start(hard_tim_t* timer)
{
    if (sys_timer) {
        BEDUG_ASSERT(false, "System timer already started");
        return false;
    }
    sys_timer = timer;
    return g_hw_timer_start(timer, _sys_timer_callback, g_get_millis_hw_tim_presc(), g_get_millis_hw_tim_count(), 5);
}

bool g_hw_timer_start(hard_tim_t* timer, void (*callback) (void), uint32_t presc, uint32_t cnt, uint8_t prio)
{
    int idx = _get_timer_index(timer);
    if (idx < 0) {
        BEDUG_ASSERT(false, "Unknown NRF TIMER");
        return false;
    }

    if (presc > 9) {
        BEDUG_ASSERT(false, "Invalid NRF prescaler");
        return false;
    }

    TIM_CALLBACKS[idx] = callback;

    // Остановить и очистить
    timer->TASKS_STOP = 1;
    timer->TASKS_CLEAR = 1;

    // Настройка таймера
    timer->MODE = TIMER_MODE_MODE_Timer;
    timer->BITMODE = TIMER_BITMODE_BITMODE_16Bit;
    timer->PRESCALER = presc;
    timer->CC[0] = cnt;

    // Auto compare reset
    timer->SHORTS = TIMER_SHORTS_COMPARE0_CLEAR_Msk;
    // IRQ
    timer->INTENSET = TIMER_INTENSET_COMPARE0_Msk;

    // Включаем прерывание
    IRQn_Type irq = tim_irqs[idx];
    NVIC_SetPriority(irq, prio);
    NVIC_ClearPendingIRQ(irq);
    NVIC_EnableIRQ(irq);

    timer->TASKS_START = 1;

    return true;
}

void g_hw_timer_stop(hard_tim_t* timer)
{
    int idx = _get_timer_index(timer);
    if (idx < 0) {
        BEDUG_ASSERT(false, "Unknown NRF TIMER");
        return;
    }

    timer->TASKS_STOP = 1;
    timer->TASKS_CLEAR = 1;

    timer->INTENCLR = TIMER_INTENCLR_COMPARE0_Msk;
    timer->EVENTS_COMPARE[0] = 0;

    IRQn_Type irq = tim_irqs[idx];
    NVIC_ClearPendingIRQ(irq);
    NVIC_DisableIRQ(irq);

    __DSB();
    __ISB();
}

extern "C" uint32_t g_get_millis(void)
{
#if defined(GSYSTEM_TIMER)
    return sys_time_ms;
#else
    return getMillis();
#endif
}

extern "C" uint64_t g_get_micros(void)
{
#if defined(GSYSTEM_TIMER)
    const int CAP_CH = 1;
    uint32_t ms;
    uint32_t cc;
    bool overflow_pending;

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    ms = sys_time_ms;
    sys_timer->TASKS_CAPTURE[CAP_CH] = 1;
    cc = sys_timer->CC[CAP_CH];    
    overflow_pending = (sys_timer->EVENTS_COMPARE[0] != 0);

    __set_PRIMASK(primask);

    if (overflow_pending && (cc < (sys_timer->CC[0] / 2))) {
        ms++;
    }
    return ((uint64_t)ms * (uint64_t)MILLIS_US) + (uint64_t)cc;
#else
    return getMicroseconds();
#endif
}

extern "C" void g_restart_check() 
{
    uint32_t reset_reason = NRF_POWER->RESETREAS;

    SYSTEM_BEDUG("Reset Reason Register: 0x%08lX", reset_reason);

    if (reset_reason & 0x00000001) SYSTEM_BEDUG("Reason: Hardware Pin Reset");
    if (reset_reason & 0x00000002) SYSTEM_BEDUG("Reason: Watchdog");
    if (reset_reason & 0x00000004) SYSTEM_BEDUG("Reason: Soft Reset (NVIC_SystemReset)");
    if (reset_reason & 0x00000008) SYSTEM_BEDUG("Reason: CPU Lockup");
    
    if (reset_reason & 0x00000010) SYSTEM_BEDUG("Reason: System OFF Wakeup");
    if (reset_reason & 0x00000020) SYSTEM_BEDUG("Reason: LPCOMP (Low Power Comparator Wakeup)");
    if (reset_reason & 0x00010000) SYSTEM_BEDUG("Reason: NFC Wakeup");
}

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
    return &__StackTop; // TODO: do lower (move it to strat)
}

extern "C" void g_ram_fill()
{
    uint32_t *heap_end = (uint32_t*)sbrk(0);
    uint32_t *stack_limit = (uint32_t*)&__StackLimit;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    for (; heap_end < stack_limit - 128; ++heap_end) {
        *heap_end = SYSTEM_CANARY_WORD;
    }
    __set_PRIMASK(primask);
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

static bool _temp_evnets_datardy()
{
    return NRF_TEMP->EVENTS_DATARDY == 0;
}

extern "C" float g_temperature() { // TODO
    NRF_TEMP->TASKS_START = 1;
    if (!util_wait_event(_temp_evnets_datardy, 10)) {
        return -1.0f;
    }
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
