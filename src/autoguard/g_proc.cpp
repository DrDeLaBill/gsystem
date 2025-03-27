/* Copyright © 2024 Georgy E. All rights reserved. */

#include "gdefines.h"
#include "gconfig.h"

#include "gsystem.h"
#include "gversion.h"

#include "GQueue.hpp"


typedef struct _process_t {
	void     (*action) (void);
	uint32_t delay_ms;
	gtimer_t timer;
	bool     work_with_error;
} process_t;


static void _sys_watchdog_check(void);
#ifndef GSYSTEM_NO_SYS_TICK_W
extern "C" void sys_clock_watchdog_check();
#endif
#ifndef GSYSTEM_NO_RAM_W
extern "C" void ram_watchdog_check();
#endif
#ifndef GSYSTEM_NO_ADC_W
extern "C" void adc_watchdog_check();
#endif
#if defined(STM32F1) && !defined(GSYSTEM_NO_I2C_W)
extern "C" void i2c_watchdog_check();
#endif
#if !defined(GSYSTEM_NO_POWER_W) && !defined(GSYSTEM_NO_ADC_W)
extern "C" void power_watchdog_check();
#endif
#ifndef GSYSTEM_NO_RTC_W
extern "C" void rtc_watchdog_check();
#endif
#ifndef GSYSTEM_NO_MEMORY_W
extern "C" void memory_watchdog_check();
#endif
#if GSYSTEM_BUTTONS_COUNT
extern "C" void btn_watchdog_check();
#endif


static process_t sys_proc[] = {
	{_sys_watchdog_check,      20,             {0,0}, true},
#ifndef GSYSTEM_NO_SYS_TICK_W
	{sys_clock_watchdog_check, SECOND_MS / 10, {0,0}, true},
#endif
#ifndef GSYSTEM_NO_RAM_W
	{ram_watchdog_check,       5 * SECOND_MS,  {0,0}, true},
#endif
#ifndef GSYSTEM_NO_ADC_W
	{adc_watchdog_check,       50,             {0,0}, true},
#endif
#if defined(STM32F1) && !defined(GSYSTEM_NO_I2C_W)
	{i2c_watchdog_check,       5 * SECOND_MS,  {0,0}, true},
#endif
#ifndef GSYSTEM_NO_RTC_W
	{rtc_watchdog_check,       SECOND_MS,      {0,0}, true},
#endif
#if !defined(GSYSTEM_NO_POWER_W) && !defined(GSYSTEM_NO_ADC_W)
	{power_watchdog_check,     SECOND_MS,      {0,0}, true},
#endif
#ifndef GSYSTEM_NO_MEMORY_W
	{memory_watchdog_check,    5 * SECOND_MS,  {0,0}, true},
#endif
#if GSYSTEM_BUTTONS_COUNT
	{btn_watchdog_check,       1,              {0,0}, true},
#endif
};
static unsigned user_proc_cnt = 0;
static process_t user_proc[GSYSTEM_POCESSES_COUNT] = {{NULL,0,{0,0},false}};

static utl::GQueue<16, process_t> queue;

#if defined(DEBUG)
static unsigned kTPScounter = 0;
#endif

static gtimer_t err_timer = {};

static const uint32_t err_delay_ms = 30 * MINUTE_MS;

static gversion_t build_ver = {};

static bool sys_timeout_enabled = false;

static uint32_t sys_timeout_ms = 0;


extern "C" void system_register(void (*process) (void), uint32_t delay_ms, bool work_with_error)
{
	if (user_proc_cnt >= __arr_len(user_proc)) {
		BEDUG_ASSERT(false, "GSystem user processes is out of range");
		return;
	}
	user_proc[user_proc_cnt].action          = process;
	user_proc[user_proc_cnt].delay_ms        = delay_ms ? delay_ms : 1;
	user_proc[user_proc_cnt].work_with_error = work_with_error;
	user_proc_cnt++;
}

extern "C" void set_system_timeout(uint32_t timeout_ms)
{
	sys_timeout_enabled = true;
	sys_timeout_ms      = timeout_ms;
}

extern "C" void sys_proc_init()
{
	gtimer_start(&err_timer, err_delay_ms);

	if (!gversion_from_string(BUILD_VERSION, strlen(BUILD_VERSION), &build_ver)) {
		memset((void*)&build_ver, 0, sizeof(build_ver));
	}
#ifdef GSYSTEM_BEDUG
	printTagLog(SYSTEM_TAG, "BUILD VERSION=%s", gversion_to_string(&build_ver));
#endif
}

extern "C" void sys_proc_tick()
{
#if defined(DEBUG)
	kTPScounter++;
#endif
	static gtimer_t err_timer = {};

	if (queue.count()) {
		queue.pop().action();
	}

	for (unsigned i = 0; i < user_proc_cnt; i++) {
		if (is_error(HARD_FAULT)) {
			break;
		}
		if (is_status(SYSTEM_ERROR_HANDLER_CALLED) && !user_proc[i].work_with_error) {
			continue;
		}
		if (!gtimer_wait(&user_proc[i].timer)) {
			queue.push(user_proc[i]);
			gtimer_start(&user_proc[i].timer, user_proc[i].delay_ms);
		}
	}

	for (unsigned i = 0; i < __arr_len(sys_proc); i++) {
		if (!gtimer_wait(&sys_proc[i].timer)) {
			queue.push(sys_proc[i]);
			gtimer_start(&sys_proc[i].timer, sys_proc[i].delay_ms);
		}
	}

	if (!sys_timeout_enabled) {
		return;
	}

	if (is_system_ready()) {
		gtimer_start(&err_timer, sys_timeout_ms);
	} else if (!gtimer_wait(&err_timer)) {
		system_error_handler(has_errors() ? get_first_error() : LOAD_ERROR);
	}
}

void _sys_watchdog_check(void)
{
#if GSYSTEM_BEDUG
	static gtimer_t kTPSTimer = {0,(10 * SECOND_MS)};
#endif

	if (!gtimer_wait(&err_timer)) {
		system_error_handler(
			get_first_error() != NO_ERROR ? get_first_error() : INTERNAL_ERROR
		);
	}
	if (!has_errors()) {
		gtimer_start(&err_timer, err_delay_ms);
	}

#if GSYSTEM_BEDUG && !defined(GSYSTEM_NO_STATUS_PRINT)
	if (!gtimer_wait(&kTPSTimer)) {
		printTagLog(
			SYSTEM_TAG,
			"firmware: v%s",
			gversion_to_string(&build_ver)
		);
		printTagLog(
			SYSTEM_TAG,
			"kTPS    : %lu.%lu",
			kTPScounter / (10 * SECOND_MS),
			(kTPScounter / SECOND_MS) % 10
		);
#   ifndef GSYSTEM_NO_ADC_W
		uint32_t power = get_system_power();
		printTagLog(
			SYSTEM_TAG,
			"power   : %lu.%lu V",
			power / 10,
			power % 10
		);
#   endif
		kTPScounter = 0;
		gtimer_start(&kTPSTimer, (10 * SECOND_MS));
	}
	if (has_new_status_data()) {
		show_statuses();
	}
	if (has_new_error_data()) {
		show_errors();
	}
#endif

	if (!is_error(STACK_ERROR) &&
		!is_error(SYS_TICK_ERROR)
	) {
		set_status(SYSTEM_HARDWARE_READY);
	} else {
		reset_status(SYSTEM_HARDWARE_READY);
	}

	if (is_software_ready()) {
		set_status(SYSTEM_SOFTWARE_READY);
	} else {
		reset_status(SYSTEM_SOFTWARE_READY);
	}

	if (!is_status(SYSTEM_HARDWARE_READY)) {
		reset_status(SYSTEM_SOFTWARE_READY);
	}
}
