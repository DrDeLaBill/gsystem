 /* Copyright © 2025 Georgy E. All rights reserved. */

#include "gdefines.h"
#include "gconfig.h"

#include <variant>

#include "gstring.h"
#include "gsystem.h"
#include "gversion.h"
#include "g_settings.h"

#include "Timer.h"
#include "CircleBuffer.hpp"
#include "TypeListService.h"


#if GSYSTEM_BEDUG && !defined(GSYSTEM_NO_STATUS_PRINT)
	#define GSYSTEM_PROC_INFO_ENABLE
#endif


static void _device_rev_show(void);


static constexpr int32_t TARGET_UTIL_X100        = 7000; // Target CPU load (70%)
static constexpr int32_t EWMA_ALPHA_X100         = 1000; // Alpha EWMA execute time (us)
static constexpr int32_t SCALE_SMOOTH_ALPHA_X100 = 20;   // Smooth scale
static constexpr int32_t MIN_SCALE_X100          = 50;
static constexpr int32_t MAX_SCALE_X100          = 2000;
static constexpr uint32_t DEFAULT_WEIGTH_X100    = 100;


template<void (*ACTION) (void) = nullptr, uint32_t DELAY_MS = 0, bool REALTIME = false, bool WORK_WITH_ERROR = false, uint32_t WEIGHT = 100>
struct Process {
    void       (*action)(void)   = ACTION;
    uint32_t   orig_delay_ms     = DELAY_MS;        // Original period
    uint32_t   current_delay_ms  = 0;               // Real period
    utl::Timer timer             = {};              // Period timer
    bool       work_with_error   = WORK_WITH_ERROR; // Work with error flag
    bool       realtime          = REALTIME;        // Scale disable flag
    uint64_t   exec_ewma_us      = 0;               // Average time EWMA
    uint64_t   last_start_us     = 0;               // Start time
    int32_t    weight_x100       = WEIGHT;          // CPU load weight TODO uint
    bool       system_task       = false;
    uint32_t   max_delay_ms      = MINUTE_MS;
    // Adaptive scaling:
    uint32_t   last_load_x100    = 0;               // Last load = exec/period (part)
    uint32_t   scale_raw_x100    = 100;             // Raw scale value (without smooth) scale_i
    uint32_t   scale_smooth_x100 = 100;             // Process smooth scale
    uint64_t   last_exec_us      = 0;               // Last execution time

    Process(): timer(0) {}

    Process(
		void (*action)(void),
		uint32_t delay_ms,
		bool realtime = false,
		bool work_with_error = false,
		uint32_t weight_x100 = 100,
		uint32_t maxd = MINUTE_MS
		):
			action(action), orig_delay_ms(delay_ms), current_delay_ms(delay_ms), timer(delay_ms),
			work_with_error(work_with_error), realtime(realtime), exec_ewma_us(0), last_start_us(0), weight_x100(weight_x100),
			max_delay_ms(maxd), last_load_x100(0), scale_raw_x100(100), scale_smooth_x100(100), last_exec_us(0)
    {}

    template<void (*_ACTION) (void) = nullptr, uint32_t _DELAY_MS = 0, bool _REALTIME = false, bool _WORK_WITH_ERROR = false, uint32_t _WEIGHT = 100>
    Process(const Process<_ACTION, _DELAY_MS, _REALTIME, _WORK_WITH_ERROR, _WEIGHT>& right):
		Process(right.action, right.orig_delay_ms, right.realtime, right.work_with_error, right.weight_x100)
    {}
};

struct InfoService {
    static utl::Timer TPC_timer;
    static uint32_t TPC_counter;
    static uint32_t last_TPC_counter;
    static uint64_t free_time_start_us;
    static uint64_t free_time_sum_us;

    static void tick()
    {
    	TPC_counter++;
    	if (!TPC_timer.wait()) {
    		last_TPC_counter = TPC_counter;
    		TPC_counter = 0;
    	} else {
    		return;
    	}
#if defined(GSYSTEM_PROC_INFO_ENABLE)
		printTagLog(
			SYSTEM_TAG,
			"firmware: v%s",
			system_device_version()
		);
		if (TPC_timer.passed()) {
			printTagLog(
				SYSTEM_TAG,
				"kTPC    : %lu.%02lu",
				last_TPC_counter / (TPC_timer.passed()),
				(last_TPC_counter / SECOND_MS) % (last_TPC_counter / SECOND_MS)
			);
		}
    #ifndef GSYSTEM_NO_ADC_W
		uint32_t voltage = get_system_power_v_x100();
		printTagLog(
			SYSTEM_TAG,
			"CPU PWR : %lu.%02lu V",
			voltage / 100,
			voltage % 100
		);
    #endif
		if (has_new_status_data() || has_new_error_data()) {
			show_statuses();
			show_errors();
		}
#endif
		TPC_timer.start();
    }

    uint32_t getTPC()
    {
    	return last_TPC_counter;
    }
};


utl::Timer InfoService::TPC_timer(SECOND_MS);
uint32_t InfoService::TPC_counter(0);
uint32_t InfoService::last_TPC_counter(0);
uint64_t InfoService::free_time_start_us(0);
uint64_t InfoService::free_time_sum_us(0);


template<class... TASKS>
struct TaskList {
    static_assert(
        !utl::empty(typename utl::typelist_t<TASKS...>::RESULT{}),
        "empty tasks list"
    );
    static_assert(
        std::is_same_v<
            typename utl::variant_factory<utl::typelist_t<TASKS...>>::VARIANT,
            typename utl::variant_factory<utl::removed_duplicates_t<TASKS...>>::VARIANT
        >,
        "repeated tasks"
    );

    using tasks_p = utl::simple_list_t<TASKS...>;
    using tasks_v = std::variant<TASKS...>;

	static constexpr unsigned COUNT =  std::variant_size_v<tasks_v>;
};


template<size_t USER_COUNT, class SYSTEM_TASK_LIST>
class Scheduler {
private:
    static constexpr uint32_t RECOMPUTE_MS = 2 * SECOND_MS;
    static constexpr unsigned TASKS_COUNT  = utl::SizeMultiplierSelector<USER_COUNT + SYSTEM_TASK_LIST::COUNT, 2>::SIZE;

	using system_task_v = typename SYSTEM_TASK_LIST::tasks_v;
	using system_task_p = typename SYSTEM_TASK_LIST::tasks_p;
	using proc_size_t   = typename utl::TypeSelector<TASKS_COUNT>::TYPE;

    utl::Timer printTimer;
    utl::Timer recomputeTimer;
    utl::CircleBuffer<TASKS_COUNT, Process<>> processes;
    uint32_t smooth_scale_x100;
    uint32_t last_recompute_ms;
    int32_t last_scale_x100;
    utl::Timer err_timer;

    bool denied(Process<>& proc)
    {
    	if (proc.system_task) {
    		return false;
    	}
		if (is_error(HARD_FAULT)) {
			return true;;
		}
		if (is_status(SYSTEM_ERROR_HANDLER_CALLED) && !proc.work_with_error) {
			return true;
		}
		return false;
    }

    void print_status()
    {
#if defined(GSYSTEM_PROC_INFO_ENABLE)
    	SYSTEM_BEDUG("System scheduler info");
        printPretty("+----+--------------+--------------+--------------+-------------+------------+---------+-----------+-----------+\n");
        printPretty("| ID | PeriodO (ms) | PeriodC (ms) | ExecAvg (us) |   Load (%%)  | Weight (%%) | scaleR  | scaleSmo  | Realtime  |\n");
        printPretty("+----+--------------+--------------+--------------+-------------+------------+---------+-----------+-----------+\n");

        uint32_t total_weighted_load_x100 = 0;
        for (proc_size_t i = 0; i < processes.count(); ++i) {
            Process<>& p = processes[i];
            uint32_t periodO   = p.orig_delay_ms;
            uint32_t periodC   = p.current_delay_ms;
            uint64_t exec_us   = p.exec_ewma_us > 0 ? p.exec_ewma_us : p.last_exec_us;
            uint32_t load_x100 = 0;
            if (periodC > 0) {
            	load_x100 = (uint32_t)(exec_us / (periodC * MILLIS_US)) * 100;
            }
            total_weighted_load_x100 += (uint32_t)(p.weight_x100 * (exec_us / (periodC * MILLIS_US)));
			printPretty(
				"| %02u | %12lu | %12lu | %12lu",
				i,
				periodO,
				periodC,
				exec_us
			);
			gprint(
				" | %8lu.%02lu | %7lu.%02lu | %4lu.%02lu | %6lu.%02lu | %9s |\n",
				load_x100 / 100, __abs(load_x100 % 100),
				p.weight_x100 / 100, __abs(p.weight_x100 % 100),
				p.scale_raw_x100 / 100, __abs(p.scale_raw_x100 % 100),
				p.scale_smooth_x100 / 100, __abs(p.scale_smooth_x100 % 100),
				p.realtime ? "YES" : "NO"
			);
		}
        printPretty("+----+--------------+--------------+--------------+-------------+------------+---------+-----------+-----------+\n");
        printPretty(
			"Total weighted load: %lu.%02lu%%   TARGET_UTIL: %lu.%02lu%%\n",
			(uint32_t)total_weighted_load_x100 / 100, (uint32_t)__abs(total_weighted_load_x100 % 100),
			(uint32_t)TARGET_UTIL_X100 / 100, (uint32_t)__abs(TARGET_UTIL_X100 % 100)
		);
#endif
    }

    void error_check()
    {
    	static utl::Timer check_delay(SECOND_MS);
    	static bool initialized = false;

    	if (check_delay.wait()) {
    		return;
    	}
    	check_delay.start();

    	if (is_error(HARD_FAULT)) {
    		return;
    	}

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

		if (!err_timer.getDelay() || is_status(SYSTEM_ERROR_HANDLER_CALLED)) {
			return;
		}

		if (!initialized) {
			err_timer.start();
			initialized = true;
		}

		if (is_system_ready()) {
			err_timer.start();
		}

		if (initialized && !err_timer.wait()) {
			system_error_handler(has_errors() ? get_first_error() : LOAD_ERROR);
		}
    }

	void newd_update(Process<>& proc)
	{
        uint32_t newd_x100 = proc.orig_delay_ms * proc.scale_smooth_x100;
        if (newd_x100 < 100) {
        	newd_x100 = 100;
        }
        if (newd_x100 / 100 > proc.max_delay_ms) {
        	newd_x100 = proc.max_delay_ms;
        }
        proc.current_delay_ms = newd_x100 / 100;
	}

    void recompute_scaling()
    {
        int32_t weigth_load_sum_x100 = 0;
        int32_t weights_sum_x100 = 0;

		for (proc_size_t i = 0; i < processes.count(); i++) {
            Process<>& proc = processes[i];
            if (proc.realtime) {
            	proc.last_load_x100 = 0;
            	continue;
            }

            uint32_t period_ms = (proc.current_delay_ms ? proc.current_delay_ms : proc.orig_delay_ms);
            if (period_ms == 0) {
            	proc.last_load_x100 = 0;
            	continue;
            }

            uint64_t exec_us    = proc.exec_ewma_us > 0 ? proc.exec_ewma_us : proc.last_exec_us;
            int32_t  load_x100  = (int32_t)((exec_us * 100) / (period_ms * MILLIS_US));
            if (load_x100 < 0) {
            	load_x100 = 0;
            }
            proc.last_load_x100 = load_x100;

            int32_t weight_x100 = proc.weight_x100 > 0 ? proc.weight_x100 : 100;
            weigth_load_sum_x100 += weight_x100 * load_x100 / 100;
            weights_sum_x100 += weight_x100;
        }
        if (weights_sum_x100 <= 0) {
        	weights_sum_x100 = 100;
        }

        int32_t Util_weighted_x100 = weigth_load_sum_x100;
        if (Util_weighted_x100 <= TARGET_UTIL_X100) {

    		for (proc_size_t i = 0; i < processes.count(); i++) {
                Process<>& proc = processes[i];
                if (proc.realtime) {
                	proc.current_delay_ms  = proc.orig_delay_ms;
                	proc.scale_raw_x100    = 100;
                	proc.scale_smooth_x100 = ((100 - SCALE_SMOOTH_ALPHA_X100) * proc.scale_smooth_x100 + SCALE_SMOOTH_ALPHA_X100 * 100) / 100;
                	continue;
                }
                int32_t scale_i_raw_x100 = 100;
                proc.scale_raw_x100 = scale_i_raw_x100;

                proc.scale_smooth_x100 = ((100 - SCALE_SMOOTH_ALPHA_X100) * proc.scale_smooth_x100 + SCALE_SMOOTH_ALPHA_X100 * scale_i_raw_x100) / 100;

                newd_update(proc);
    		}

        	return;
        }


        int32_t excess_x100 = Util_weighted_x100 * 100 / TARGET_UTIL_X100;
        if (excess_x100 > MAX_SCALE_X100) {
        	excess_x100 = MAX_SCALE_X100;
        }

        int32_t denom_x100 = 0;
		for (proc_size_t i = 0; i < processes.count(); i++) {
            Process<>& proc = processes[i];
            denom_x100 += proc.weight_x100 * proc.last_load_x100 / 100;
		}
		if (denom_x100 <= 0) {
			denom_x100 = 100;
		}


		for (proc_size_t i = 0; i < processes.count(); i++) {
            Process<>& proc = processes[i];
            if (proc.realtime) {
            	proc.current_delay_ms = proc.orig_delay_ms;
            	proc.scale_raw_x100   = 100;
            	proc.scale_raw_x100   = ((100 - SCALE_SMOOTH_ALPHA_X100) * proc.scale_smooth_x100 + SCALE_SMOOTH_ALPHA_X100 * 100) / 100;
            	continue;
            }

            int32_t contrib_x100 = proc.weight_x100 * proc.last_load_x100 / 100;
            int32_t scale_i_raw_x100 = 100 + (excess_x100 - 100) * (contrib_x100 / denom_x100);
            if (scale_i_raw_x100 < MIN_SCALE_X100) {
            	scale_i_raw_x100 = MIN_SCALE_X100;
            }
            if (scale_i_raw_x100 > MAX_SCALE_X100) {
            	scale_i_raw_x100 = MAX_SCALE_X100;
            }

            newd_update(proc);
		}
    }

    template<class SYS_PACK>
    void add_sys_task(utl::getType<SYS_PACK>)
    {
		add_task(SYS_PACK{});
    }

    template<class... SYS_PACKS>
    void system_tasks_register(utl::simple_list_t<SYS_PACKS...>)
    {
        (add_sys_task(utl::getType<SYS_PACKS>{}), ...);
    }

public:
    Scheduler():
    	printTimer(1 * SECOND_MS), recomputeTimer(RECOMPUTE_MS), processes({}),
		smooth_scale_x100(100), last_recompute_ms(0),
		last_scale_x100(0), err_timer(0)
    {
    	system_tasks_register(system_task_p{});
    }

    void init()
    {
    	_device_rev_show();
    }

    template<void (*ACTION) (void) = nullptr, uint32_t DELAY_MS = 0, bool REALTIME = false, bool WORK_WITH_ERROR = false, uint32_t WEIGHT = 100>
    bool add_task(const Process<ACTION, DELAY_MS, REALTIME, WORK_WITH_ERROR, WEIGHT>& p)
    {
		BEDUG_ASSERT(!full(), "GSystem user processes is out of range");
    	if (full()) {
    		return false;
    	}

    	Process<> proc = p;
    	proc.timer.changeDelay(processes.back().current_delay_ms);
    	proc.timer.start();
    	proc.scale_smooth_x100 = 100;

    	processes.push_back(proc);

    	return true;
    }

    void tick()
    {
    	InfoService::tick();

		for (proc_size_t i = 0; i < processes.count(); i++) {
			Process<>& pr = processes[i];
			if (pr.timer.wait()) {
				continue;
			}

			if (denied(pr)) {
				continue;
			}

			pr.last_start_us = getMicrosecondes();
			if (pr.action) {
				pr.action();
			}

			uint64_t after_us = getMicrosecondes();
			uint64_t dur_us = (after_us >= pr.last_start_us) ? (after_us - pr.last_start_us) : 0;
			if (pr.exec_ewma_us <= 0) {
				pr.exec_ewma_us = dur_us;
			} else {
				pr.exec_ewma_us =
					((100 - EWMA_ALPHA_X100) * pr.exec_ewma_us) / 100 +
					(EWMA_ALPHA_X100 * dur_us) / 100;
			}

			uint32_t delay_ms = pr.current_delay_ms ? pr.current_delay_ms : pr.orig_delay_ms;
			pr.timer.changeDelay(delay_ms);
			pr.timer.start();
		}

		if (!recomputeTimer.wait()) {
			recompute_scaling();
			recomputeTimer.start();
		}

		if (!printTimer.wait()) {
			print_status();
			printTimer.start();
		}

		error_check();
    }

    void set_timeout(uint32_t timeout_ms)
    {
    	err_timer.changeDelay(timeout_ms);
    }

    bool full()
    {
    	return processes.full();
    }
};


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
#ifndef GSYSTEM_NO_DEVICE_SETTINGS
extern "C" void settings_update();
#endif
extern "C" void btn_watchdog_check();
extern "C" void system_scheduler_init();

static Scheduler<
	GSYSTEM_POCESSES_COUNT,
	TaskList<
#ifndef GSYSTEM_NO_SYS_TICK_W
		Process<sys_clock_watchdog_check, SECOND_MS / 10, false, true, DEFAULT_WEIGTH_X100>,
#endif
#ifndef GSYSTEM_NO_RAM_W
		Process<ram_watchdog_check,       5 * SECOND_MS,  false, true, DEFAULT_WEIGTH_X100>,
#endif
#ifndef GSYSTEM_NO_ADC_W
		Process<adc_watchdog_check,       1,              true,  true, DEFAULT_WEIGTH_X100>,
#endif
#if defined(STM32F1) && !defined(GSYSTEM_NO_I2C_W)
		Process<i2c_watchdog_check,       5 * SECOND_MS,  false, true, DEFAULT_WEIGTH_X100>,
#endif
#ifndef GSYSTEM_NO_RTC_W
		Process<rtc_watchdog_check,       SECOND_MS,      false, true, DEFAULT_WEIGTH_X100>,
#endif
#if !defined(GSYSTEM_NO_POWER_W) && !defined(GSYSTEM_NO_ADC_W)
		Process<power_watchdog_check,     1,              true,  true, DEFAULT_WEIGTH_X100>,
#endif
#ifndef GSYSTEM_NO_MEMORY_W
		Process<memory_watchdog_check,    1,              false, true, DEFAULT_WEIGTH_X100>,
#endif
#ifndef GSYSTEM_NO_DEVICE_SETTINGS
		Process<settings_update,          500,            false, true, DEFAULT_WEIGTH_X100>,
#endif
		Process<btn_watchdog_check,       5,              false, true, DEFAULT_WEIGTH_X100>
	>
> scheduler;


extern "C" void sys_proc_init()
{
	scheduler.init();

#if defined(GSYSTEM_PROC_INFO_ENABLE)
//	sys_free_time_sum_us   = 0;
//	sys_free_time_start_us = getMicrosecondes();
#endif
}

extern "C" void system_tick()
{
	scheduler.tick();
}

extern "C" void system_register(void (*task) (void), uint32_t delay_ms, bool realtime, bool work_with_error, uint32_t weight_x100)
{
	if (!task) {
		BEDUG_ASSERT(false, "Empty task");
		return;
	}
	if (scheduler.full()) {
		BEDUG_ASSERT(false, "GSystem user processes is out of range");
		return;
	}
	scheduler.add_task({task, delay_ms, realtime, work_with_error, weight_x100});
}

extern "C" void set_system_timeout(uint32_t timeout_ms)
{
	scheduler.set_timeout(timeout_ms);
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
		system_device_version(),
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
