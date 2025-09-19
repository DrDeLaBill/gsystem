/* Copyright © 2024 Georgy E. All rights reserved. */

#include "gdefines.h"
#include "gconfig.h"

#include "gsystem.h"
#include "gversion.h"

#include "Timer.h"
#include "GQueue.hpp"


typedef struct _process_t {
	void     (*action) (void);
	uint32_t delay_ms;
	gtimer_t timer;
	bool     work_with_error;
	uint32_t time_max_ms;
	uint32_t time_sum_ms;
	uint32_t time_count;
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
extern "C" void btn_watchdog_check();


static void _device_rev_show(void);
static void _queue_proc(process_t* proc);


static process_t sys_proc[] = {
	{_sys_watchdog_check,      20,             {0,0}, true, 0, 0, 0},
#ifndef GSYSTEM_NO_SYS_TICK_W
	{sys_clock_watchdog_check, SECOND_MS / 10, {0,0}, true, 0, 0, 0},
#endif
#ifndef GSYSTEM_NO_RAM_W
	{ram_watchdog_check,       5 * SECOND_MS,  {0,0}, true, 0, 0, 0},
#endif
#ifndef GSYSTEM_NO_ADC_W
	{adc_watchdog_check,       1,              {0,0}, true, 0, 0, 0},
#endif
#if defined(STM32F1) && !defined(GSYSTEM_NO_I2C_W)
	{i2c_watchdog_check,       5 * SECOND_MS,  {0,0}, true, 0, 0, 0},
#endif
#ifndef GSYSTEM_NO_RTC_W
	{rtc_watchdog_check,       SECOND_MS,      {0,0}, true, 0, 0, 0},
#endif
#if !defined(GSYSTEM_NO_POWER_W) && !defined(GSYSTEM_NO_ADC_W)
	{power_watchdog_check,     1,              {0,0}, true, 0, 0, 0},
#endif
#ifndef GSYSTEM_NO_MEMORY_W
	{memory_watchdog_check,    1,              {0,0}, true, 0, 0, 0},
#endif
	{btn_watchdog_check,       5,              {0,0}, true, 0, 0, 0},
};
static unsigned user_proc_cnt = 0;
static process_t user_proc[GSYSTEM_POCESSES_COUNT] = {};

static utl::GQueue<16, process_t*> queue;

#if GSYSTEM_BEDUG && !defined(GSYSTEM_NO_STATUS_PRINT)
static utl::Timer kTPCTimer(10 * SECOND_MS);
static unsigned kTPCcounter = 0;
#endif

static bool err_timer_init = false;
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
	_device_rev_show();
}

extern "C" void sys_proc_tick()
{
#if GSYSTEM_BEDUG && !defined(GSYSTEM_NO_STATUS_PRINT)
	kTPCcounter++;
#endif

	if (queue.count()) {
		process_t* proc = queue.pop();
#if !defined(GSYSTEM_NO_PROC_INFO)
		uint32_t start_ms = getMillis();
		if (!is_status(SYSTEM_ERROR_HANDLER_CALLED) || proc->work_with_error) {
			proc->action();
		}
		uint32_t time_ms = getMillis() - start_ms;
		proc->time_sum_ms += time_ms;
		proc->time_count++;
		if (time_ms > proc->time_max_ms) {
			proc->time_max_ms = time_ms;
		}
#else
		if (!is_status(SYSTEM_ERROR_HANDLER_CALLED) || proc->work_with_error) {
			proc->action();
		}
#endif
	}

	for (unsigned i = 0; i < user_proc_cnt; i++) {
		if (is_error(HARD_FAULT)) {
			break;
		}
		if (is_status(SYSTEM_ERROR_HANDLER_CALLED) && !user_proc[i].work_with_error) {
			continue;
		}
		if (!gtimer_wait(&user_proc[i].timer)) {
			_queue_proc(&user_proc[i]);
		}
	}

	for (unsigned i = 0; i < __arr_len(sys_proc); i++) {
		if (!gtimer_wait(&sys_proc[i].timer)) {
			_queue_proc(&sys_proc[i]);
		}
	}
}

static uint32_t sys_cpu_freq = 0;
extern "C" uint32_t get_system_freq(void)
{
	return sys_cpu_freq;
}

#if GSYSTEM_BEDUG && !defined(GSYSTEM_NO_STATUS_PRINT)
static void _show_process(const unsigned idx, process_t* const proc, const uint32_t time_passed)
{
	uint32_t tpc_per_sec = 0;
	if (time_passed > 0) {
		tpc_per_sec = proc->time_count * SECOND_MS / time_passed;
	}
	uint32_t avrg = 0;
	if (proc->time_count > 0) {
		avrg = proc->time_sum_ms / proc->time_count;
	}
	printPretty("process[%02u]: TPC=%06lu | avrg=%05lu ms | max=%04lu ms\n", idx, tpc_per_sec, avrg, proc->time_max_ms);
	proc->time_sum_ms = 0;
	proc->time_count  = 0;
	proc->time_max_ms = 0;
}
#endif

void _sys_watchdog_check(void)
{
    sys_cpu_freq = g_get_freq();

	if (is_error(HARD_FAULT)) {
		return;
	}

#if GSYSTEM_BEDUG && !defined(GSYSTEM_NO_STATUS_PRINT)
	if (!kTPCTimer.getStart()) {
		kTPCTimer.start();
	}
	if (!kTPCTimer.wait()) {
		printTagLog(
			SYSTEM_TAG,
			"firmware: v%s",
			gversion_to_string(&build_ver)
		);
		printTagLog(
			SYSTEM_TAG,
			"kTPC    : %lu.%lu",
			kTPCcounter / (10 * SECOND_MS),
			(kTPCcounter / SECOND_MS) % 10
		);
    #ifndef GSYSTEM_NO_PROC_INFO
		uint32_t time_passed = kTPCTimer.getDelay();
		printTagLog(SYSTEM_TAG, "System processes", time_passed);
		for (unsigned i = 0; i < __arr_len(sys_proc); i++) {
			_show_process(i, &sys_proc[i], time_passed);
		}
		printTagLog(SYSTEM_TAG, "User processes");
		for (unsigned i = 0; i < user_proc_cnt; i++) {
			_show_process(i, &user_proc[i], time_passed);
		}
	#endif
    #ifndef GSYSTEM_NO_ADC_W
		uint32_t voltage = get_system_power_v_x100();
		printTagLog(
			SYSTEM_TAG,
			"power   : %lu.%02lu V",
			voltage / 100,
			voltage % 100
		);
    #endif
		kTPCcounter = 0;
		kTPCTimer.start();
	}
	if (has_new_status_data() || has_new_error_data()) {
		show_statuses();
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

	if (is_software_ready() && is_status(SYSTEM_HARDWARE_READY)) {
		set_status(SYSTEM_SOFTWARE_READY);
	} else {
		reset_status(SYSTEM_SOFTWARE_READY);
	}

	if (!sys_timeout_enabled || is_status(SYSTEM_ERROR_HANDLER_CALLED)) {
		return;
	}
	if (!err_timer_init && sys_timeout_ms) {
		gtimer_start(&err_timer, sys_timeout_ms);
		err_timer_init = true;
	}
	if (is_system_ready()) {
		gtimer_start(&err_timer, sys_timeout_ms);
	} 
	if (!gtimer_wait(&err_timer)) {
		system_error_handler(has_errors() ? get_first_error() : LOAD_ERROR);
	}
}

void _device_rev_show(void)
{
#if !defined(GSYSTEM_NO_REVISION)
	
	char rev[100] = "";
	char ser[100] = "";
	char str[322] = "";

	snprintf(
		rev,
		sizeof(rev) - 1,
		"REVISION %s (%s %s)",
		gversion_to_string(&build_ver),
		__DATE__,
		__TIME__
	);

	const char SERIAL_START[] = "SERIAL ";
	char const* serial_num = get_system_serial_str();
	uint16_t offset = (uint16_t)(strlen(rev) - strlen(SERIAL_START));
	offset = (uint16_t)((strlen(serial_num) < offset) ? offset - strlen(serial_num) : 0);
	snprintf(
		ser,
		sizeof(ser) - 1,
		"%*s%s%s%*s",
		offset / 2,
		"",
		SERIAL_START,
		serial_num,
		__div_up(offset, 2),
		""
	);

	snprintf(
		str,
		sizeof(str) - 1,
		"----------------------------> %s <----------------------------\n"
		"----------------------------> %s <----------------------------\n",
		ser,
		rev
	);
	
	g_uart_print(str, (uint16_t)strlen(str));

    #if !defined(GSYSTEM_NO_PRINTF)
	char* ptr = str;
    for (size_t DataIdx = 0; DataIdx < strlen(str); DataIdx++) {
        ITM_SendChar(*ptr++);
    }
    #endif

#endif
}

void _queue_proc(process_t* proc)
{
	if (queue.full()) {
		gtimer_reset(&queue.pop()->timer);
	}
	queue.push(proc);
	gtimer_start(&proc->timer, proc->delay_ms);
}
