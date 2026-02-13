/*
 * @file gsystem.h
 * @brief Core system API for gsystem - small RTOS library: initialization, scheduling,
 *        timers, button handling and utility helpers used by platform code.
 *
 * This header declares the public C API consumed by application and example
 * projects. It provides functions to initialize the runtime, register
 * periodic or ISR-scheduled tasks, manage simple timers, query device
 * identification, and access button/ADC/RTC helpers.
 *
 * Notes:
 * - Keep this header C-compatible; it is wrapped in extern "C" for C++.
 * - For compile this library, the C++17 standard is used.
 * - Functions return status codes or boolean values; no exceptions are used.
 *
 * Copyright © 2025 Georgy E. All rights reserved.
 */

#ifndef _SYSTEM_H_
#define _SYSTEM_H_


#ifdef __cplusplus
extern "C" {
#endif


#include <stdio.h>
#include <stdint.h>

#include "gdefines.h"

#include "g_settings.h"


#define GSYS_CALL_HARD_FAULT() { \
    volatile int _gsys_val1 = 1, _gsys_val2 = 1; \
    while(--_gsys_val1) _gsys_val2++; \
    volatile int _gsys_res = _gsys_val2 / _gsys_val1; \
}


#define GSYSTEM_PROCCESS_PRIORITY_DEFAULT ((uint8_t)100)
#define GSYSTEM_PROCCESS_PRIORITY_MAX     ((uint8_t)200)


typedef struct _system_timer_t {
    uint32_t    verif;
    hard_tim_t* tim;
    hard_tim_t  bkup_tim;
    bool        enabled;
    uint32_t    end;
    uint32_t    count;
} system_timer_t;

extern const uint32_t TIMER_VERIF_WORD;


/*
 * @brief Initialize core system subsystems and hardware abstractions. Use it at start of main().
 * @param None
 * @return None
 * @example system_init();
 */
void system_init(void);

/*
 * @brief Register a periodic non-ISR task with the system scheduler.
 * @param task (void (*)(void)) - Function pointer to the task to be called.
 * @param delay_ms (uint32_t) - Delay in milliseconds between launches.
 * @param realtime (bool) - If true, the task is treated as realtime priority and won't be optimized by the scheduler.
 * @param work_with_error (bool) - If true, the task will run even if system has errors.
 * @param priority (uint32_t) - Relative task priority. Used for scheduling decisions.
 * @return None
 * @example system_register(my_task, 1000, false, true, 100);
 */
void system_register(
    void (*task) (void),
    uint32_t delay_ms,
    bool realtime,
    bool work_with_error,
    uint32_t priority
);

/*
 * @brief Register a task to be executed in ISR context or with ISR-like timing.
 * @param task (void (*)(void)) - Function pointer to the ISR-context task.
 * @param delay_ms (uint32_t) - Delay in milliseconds between launches.
 * @param realtime (bool) - If true, the task is treated as realtime priority and won't be optimized by the scheduler.
 * @param work_with_error (bool) - If true, the task will run even if system has errors.
 * @param priority (uint32_t) - Relative task priority.
 * @return None
 * @example system_register_isr(my_task, 1000, false, true, 100);
 * TODO: the sheduler is not implemented yet
 */
void system_register_isr(
    void (*task) (void),
    uint32_t delay_ms,
    bool realtime,
    bool work_with_error,
    uint32_t priority
);

/*
 * @brief Set a global system error timeout used by watchdog-like operations.
 *        If runtime has error statuses for longer than `timeout_ms`, the system error handler 
 *        is invoked. If not set, the timeout is not used.
 * @param timeout_ms (uint32_t) - Timeout in milliseconds.
 * @return None
 */
void set_system_timeout(uint32_t timeout_ms);

/*
 * @brief Start the system scheduler, enable registered tasks execution in endless loop.
 * @param None
 * @return None
 */
void system_start(void);

/*
 * @brief Trigger a system software reset. Calls `system_before_reset()` before resetting.
 * @param None
 * @return None
 */
void system_reset(void);

/*
 * @brief Return device firmware version string.
 * @param None
 * @return const char* - Pointer to a null-terminated version string.
 */
const char* system_device_version();

/*
 * @brief Hook executed after configuration or settings are loaded.
 *        Uses only project needs custom system_tick() call.
 * @param None
 * @return None
 * @example { system_post_load(); your_method(); while (1) system_tick(); }
 */
void system_post_load(void);

/*
 * @brief Main system tick handler called from periodic timer.
 *        Uses only project needs custom system_tick() call.
 * @param None
 * @return None
 * @example { while (1) system_tick(); }
 */
void system_tick(void);

/*
 * @brief System tick handler intended for ISR context. 
 *        Calls in the interrupt.
 * @param None
 * @return None
 */
void system_tick_isr(void);

/*
 * @brief Query whether the hardware system is ready.
 * @param None
 * @return bool - true if hardware subsystems are initialized.
 */
bool is_system_ready(void);

/*
 * @brief Query whether software subsystems (services/tasks) and gsystem are ready.
 * @note A weak default implementation for `is_software_ready()` is provided
 *       in the library. To customize behavior, provide a non-weak implementation
 *       with the same signature in your application code; the linker will prefer
 *       the user-defined symbol and override the weak default.
 * @param None
 * @return bool - true if software initialization is complete.
 * @example bool is_software_ready() { return my_service_is_ready() && another_service_is_ready(); }
 */
bool is_software_ready(void);

/*
 * @brief Handle a system-level or C/C++ hard faults error statuses.
 *        After calling this function, the system will be in error state 
 *        for GSYSTEM_RESET_TIMEOUT_MS ms before resetting.
 * @param error (SOUL_STATUS) - Error code to handle.
 * @return None
 * @example system_error_handler(MCU_ERROR);
 */
void system_error_handler(SOUL_STATUS error);

/*
 * @brief Called before performing a system reset to allow cleanup.
 * @note `system_before_reset()` has a weak default (no-op) implementation in
 *        the library. To run custom shutdown/cleanup code before resets, implement
 *        a non-weak `system_before_reset()` in your application; it will override
 *        the weak one at link time.
 * @param None
 * @return None
 * @example void system_before_reset() { save_critical_data(); }
 */
void system_before_reset(void);

/*
 * @brief Apply platform-specific I2C errata fixes and reset I2C bus.
 *        For example, on STM32F1 devices this function implements the
 *        known I2C bus hang recovery sequence.
 * @param None
 * @return None
 */
void system_reset_i2c_errata(void);

/*
 * @brief Retrieve the device unique manufacture serial number as integer.
 * @param None
 * @return uint64_t - Numeric system serial.
 */
uint64_t get_system_serial(void);

/*
 * @brief Retrieve the device unique manufacture serial number as a string.
 * @param None
 * @return char* - Pointer to a string buffer containing the serial.
 * @example char* serial = get_system_serial_str();
 */
char* get_system_serial_str(void);

/*
 * @brief User error loop called periodically when system is in error state (system_error_handler).
 * @note `system_error_loop()` has a weak default implementation. To change
 *        error-loop behaviour (for example to hook in a watchdog reset or logger),
 *       implement a non-weak `system_error_loop()` in your application code.
 * @param None
 * @return None
 */
void system_error_loop(void);

/*
 * @brief Start a simple software timer backed by provided hardware timer.
 *        Typically used when system is in error state to track timeouts.
 * @param timer (system_timer_t*) - Pointer to timer struct to start you need to create.
 * @param fw_tim (hard_tim_t*) - Hardware timer reference (may be null).
 * @param delay_ms (uint32_t) - Delay in milliseconds before timer expiry.
 * @return None
 * @example static system_timer_t timer = {}; system_timer_start(&timer, fw_tim, 500);
 */
void system_timer_start(system_timer_t* timer, hard_tim_t* fw_tim, uint32_t delay_ms);

/*
 * @brief Check if the software timer has expired.
 * @param timer (system_timer_t*) - Pointer to the timer struct to check.
 * @return bool - true if timer expired.
 */
bool system_timer_wait(system_timer_t* timer);

/*
 * @brief Stop and invalidate the software timer.
 *        You need to call this function to free resources after use.
 * @param timer (system_timer_t*) - Pointer to the timer to stop.
 * @return None
 */
void system_timer_stop(system_timer_t* timer);

/*
 * @brief Register a button pin to be handled by the system button subsystem.
 * @note If you need to realize more complex button logic, 
 *       you need to use button.h API directly.
 * @param pin (port_pin_t) - Platform-specific pin descriptor.
 * @param inverse (bool) - If true, invert the logical state.
 * @return None
 * @example system_add_button({my_button_port, my_button_pin}, true);
 */
void system_add_button(const port_pin_t pin, bool inverse);

/*
 * @brief Retrieve number of clicks detected for given button pin.
 *        Clicks are counted since last reset and will be erased after some time.
 * @param pin (port_pin_t) - Button pin descriptor.
 * @return uint32_t - Number of clicks recorded since last reset.
 * @example uint32_t clicks = system_button_clicks({my_button_port, my_button_pin});
 */
uint32_t system_button_clicks(const port_pin_t pin);

/*
 * @brief Query if the button is currently pressed.
 * @param pin (port_pin_t) - Button pin descriptor.
 * @return bool - true if pressed.
 * @example bool pressed = system_button_pressed({my_button_port, my_button_pin});
 */
bool system_button_pressed(const port_pin_t pin);

/*
 * @brief Get how long the button has been held in milliseconds.
 * @param pin (port_pin_t) - Button pin descriptor.
 * @return uint32_t - Milliseconds the button has been held.
 * @example uint32_t held_ms = system_button_held_ms({my_button_port, my_button_pin});
 */
uint32_t system_button_held_ms(const port_pin_t pin);

/*
 * @brief Check if the button has been held at least `time_ms`.
 * @param pin (port_pin_t) - Button pin descriptor.
 * @param time_ms (uint32_t) - Threshold in milliseconds.
 * @return bool - true if held at least `time_ms`.
 * @example bool is_held = system_button_held({my_button_port, my_button_pin}, 2000);
 */
bool system_button_held(const port_pin_t pin, uint32_t time_ms);

/*
 * @brief Reset internal button state (click counters, durations).
 * @param None
 * @return None
 */
void system_buttons_reset();

/*
 * @brief Check whether gsystem message logging is enabled at runtime.
 * @param None
 * @return bool - true if messaging/log output is enabled.
 */
bool gsystem_messages_enabled();

#ifndef GSYSTEM_NO_ADC_W

/*
 * @brief Get the measured MCU reference power voltage multiplied by 100.
 * @param None
 * @return uint32_t - Voltage in centivolts (V * 100).
 */
uint32_t get_system_power_v_x100(void);

#endif // #ifndef GSYSTEM_NO_ADC_W

#ifndef GSYSTEM_NO_SYS_TICK_W

/*
 * @brief Configure system to use internal high-speed oscillator (HSI).
 *        For cases when HSE has broken or is not present.
 * @note The library provides a weak default for `system_hsi_config()`.
 *       Provide a non-weak implementation in application code to override the
 *       default hardware setup when necessary.
 * @param None
 * @return None
 * @example void system_hsi_config() { custom HSI setup }
 */
void system_hsi_config(void);

/*
 * @brief Configure system to use external high-speed oscillator (HSE).
 *        For cases when HSE needs to be restarted.
 * @note The library provides a weak default for `system_hse_config()`.
 *       Provide a non-weak implementation in application code to override the
 *       default hardware setup when necessary.
 * @param None
 * @return None
 * @example void system_hse_config() {  custom HSE setup  }
 */
void system_hse_config(void);

/*
 * @brief Attempt to reanimate the SysTick timer after fault recovery.
 *        Function use system_hse_config() and system_hsi_config() to restart the clock if needed.
 * @note `system_error_loop()` has a weak default implementation. To change
 *        error-loop behaviour (for example to hook in a watchdog reset or logger),
 *        implement a non-weak `system_error_loop()` in your application code.
 * @param None
 * @return None
 */
void system_sys_tick_reanimation(void);

#endif

#ifndef GSYSTEM_NO_ADC_W

/*
 * @brief HAL only function. Read ADC value for the given channel index.
 *        You need to initialize ADC channels rank and DMA in STM32CubeMX or manually before use.
 *        GSYSTEM_ADC_VOLTAGE_COUNT set in gconfig.h defines number of ADC channels to read.
 *        The system use ADC_HandleTypeDef hadc1.
 * @param index (unsigned) - ADC channel index (0..GSYSTEM_ADC_VOLTAGE_COUNT-1).
 * @return uint16_t - ADC raw value.
 * TODO: rename to get_system_adc_value() & fix hadc1 usage
 */
uint16_t get_system_adc(unsigned index);

#endif

#ifndef GSYSTEM_NO_RTC_W

/*
 * @brief HAL only function. Read a byte from the backup register area.
 *        You need to initialize and start RTC in STM32CubeMX before use.
 * @param idx (const uint8_t) - Backup register index.
 * @param data (uint8_t*) - Output pointer for the read byte.
 * @return bool - true on success.
 * @example uint8_t data = 0; bool success = get_system_bckp(0, &data);
 */
bool get_system_bckp(const uint8_t idx, uint8_t* data);

/*
 * @brief HAL only function. Write a byte into the backup register area.
 *        You need to initialize and start RTC in STM32CubeMX before use.
 * @param idx (const uint8_t) - Backup register index.
 * @param data (const uint8_t) - Byte value to write.
 * @return bool - true on success.
 * @example bool success = set_system_bckp(0, 42);
 */
bool set_system_bckp(const uint8_t idx, const uint8_t data);

#endif

/*
 * @brief Get system core frequency in Hz used by timing helpers.
 * @param None
 * @return uint32_t - System frequency in Hz.
 */
uint32_t get_system_freq(void);

/*
 * @brief Busy-wait delay for the specified number of microseconds.
 * @param us (uint64_t) - Microseconds to delay.
 * @return None
 */
void system_delay_us(uint64_t us);

#if defined(ARDUINO) && !defined(GSYSTEM_NO_VTOR_REWRITE)

void NMI_Handler(void);
void HardFault_Handler(void);
void MemoryManagement_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);

#endif

#ifdef __cplusplus
}
#endif


#endif
