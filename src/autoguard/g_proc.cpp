 /* Copyright © 2025 Georgy E. All rights reserved. */

#include "gdefines.h"
#include "gconfig.h"

#include <cstring>

#include "gstring.h"
#include "gsystem.h"
#include "gversion.h"
#include "g_settings.h"
#include "circle_buf_gc.h"

#include "Timer.h"
#include "TypeListService.h"


#if GSYSTEM_BEDUG && !defined(GSYSTEM_NO_PROC_INFO)
    #define GSYSTEM_PROC_PROC_ENABLE
#endif

#if GSYSTEM_BEDUG && !defined(GSYSTEM_NO_STATUS_PRINT)
    #define GSYSTEM_PROC_INFO_ENABLE
#endif


static void _device_rev_show();
static void _scheduler_load_show();
static void _scheduler_recompute_scaling();
static void _scheduler_error_check();


static constexpr uint32_t RECOMPUTE_MS             = 2 * SECOND_MS;
static constexpr int32_t TARGET_UTIL_PCT100        = 70;   // Target CPU load (%)
static constexpr int32_t EWMA_ALPHA_PCT100         = 10;   // Alpha EWMA part (%)
static constexpr int32_t SCALE_SMOOTH_ALPHA_PCT100 = 20;   // Smooth scale (%)
static constexpr int32_t MIN_SCALE_PCT100          = 100;  // Work time scaling min
static constexpr int32_t MAX_SCALE_PCT100          = 2000; // Work time scaling max
static constexpr uint32_t DEFAULT_WEIGHT_PCT100    = 100;
static constexpr int32_t FIX                       = 100;
static constexpr int32_t LOAD_SCALE                = 10000;
static constexpr uint32_t LOAD_SHOW_DELAY_MS       = 10 * SECOND_MS;


template<
    void     (*ACTION) (void) = nullptr,
    uint32_t DELAY_MS         = 0,
    bool     REALTIME         = false,
    bool     WORK_WITH_ERROR  = false,
    uint32_t WEIGHT           = 100
>
struct Process {
	static constexpr uint32_t MAX_DELAY_MS = MINUTE_MS;

    void       (*action)(void)     = ACTION;
    uint32_t   orig_delay_ms       = DELAY_MS;        // Original period
    uint32_t   current_delay_ms    = 0;               // Real period
    utl::Timer timer               = {};              // Period timer
    bool       work_with_error     = WORK_WITH_ERROR; // Work with error flag
    bool       realtime            = REALTIME;        // Scale disable flag
    bool       system_task         = false;           // System task flag

    int64_t    exec_ewma_us_pct100 = 0;               // Average time EWMA
    uint64_t   last_start_us       = 0;               // Start time
    int32_t    weight_pct100       = WEIGHT;          // CPU load weight

    // Adaptive scaling:
    int64_t   last_load_scaled     = 0;               // Last load (part)
    int32_t   scale_raw_pct100     = 100;             // Raw scale value (without smooth) scale_i
    int32_t   scale_smooth_pct100  = 100;             // Process smooth scale
#if !defined(GSYSTEM_NO_PROC_INFO)
    uint32_t  last_exec_us         = 0;               // Last execution time

    // Execution counter
    uint32_t exec_counter          = 0;
    uint32_t execs_per_sec_x100    = 0;
#endif


    Process(): timer(0) {}

    Process(
        void (*action)(void),
        uint32_t delay_ms,
        bool realtime          = false,
        bool work_with_error   = false,
        int32_t weight_pct100  = 100
    ):
        action(action), orig_delay_ms(delay_ms), current_delay_ms(delay_ms), timer(delay_ms),
        work_with_error(work_with_error), realtime(realtime), system_task(false),
        exec_ewma_us_pct100(0), last_start_us(0), weight_pct100(weight_pct100),
        last_load_scaled(0), scale_raw_pct100(100), scale_smooth_pct100(100)
#if !defined(GSYSTEM_NO_PROC_INFO)
    	, last_exec_us(0), exec_counter(0), execs_per_sec_x100(0)
#endif
    {}

    template<void (*_ACTION) (void) = nullptr, uint32_t _DELAY_MS = 0, bool _REALTIME = false, bool _WORK_WITH_ERROR = false, uint32_t _WEIGHT = 100>
    Process(const Process<_ACTION, _DELAY_MS, _REALTIME, _WORK_WITH_ERROR, _WEIGHT>& right):
        Process(right.action, right.orig_delay_ms, right.realtime, right.work_with_error, right.weight_pct100)
    {}
};


template<class... TASKS>
struct TaskList {
    static_assert(
        !utl::empty(typename utl::typelist_t<TASKS...>::RESULT{}),
        "empty tasks list"
    );
#if __cplusplus > 201402L
    static_assert(
        std::is_same_v<
            typename utl::variant_factory<utl::typelist_t<TASKS...>>::VARIANT,
            typename utl::variant_factory<utl::removed_duplicates_t<TASKS...>>::VARIANT
        >,
        "repeated tasks"
    );
#endif

    using tasks_p = utl::simple_list_t<TASKS...>;

    static constexpr unsigned COUNT = sizeof...(TASKS);
};


template<size_t USER_COUNT, class SYSTEM_TASK_LIST>
class Scheduler {
private:
    static constexpr unsigned TASKS_COUNT = USER_COUNT + SYSTEM_TASK_LIST::COUNT;

    using system_task_p = typename SYSTEM_TASK_LIST::tasks_p;
    using proc_size_t   = typename utl::TypeSelector<TASKS_COUNT>::TYPE;

	Process<> processes_buf[TASKS_COUNT];
	circle_buf_gc_t processes;

    uint32_t smooth_scale_x100;
    uint32_t last_recompute_ms;
    int32_t last_scale_x100;
    utl::Timer err_timer;
    utl::Timer TPC_timer;
    uint32_t TPC_counter;
    uint32_t last_TPC_counter;

    bool denied(Process<>* const proc)
    {
        if (proc->system_task) {
            return false;
        }
        if (is_error(HARD_FAULT)) {
            return true;;
        }
        if (is_status(SYSTEM_ERROR_HANDLER_CALLED) && !proc->work_with_error) {
            return true;
        }
        return false;
    }

    void newd_update(Process<>* const proc)
    {
        int64_t newd = ((int64_t)proc->orig_delay_ms * proc->scale_smooth_pct100 + (FIX - 1)) / FIX;
        if (newd < 1) {
            newd = 1;
        }
        if ((uint32_t)newd > Process<>::MAX_DELAY_MS) {
            newd = Process<>::MAX_DELAY_MS;
        }
        proc->current_delay_ms = (uint32_t)newd;
    }

    void print_div_line()
    {
#if defined(GSYSTEM_PROC_PROC_ENABLE)
        printPretty("+----+-------------+-------------+-------------+----------+---------+-------------+-----------+--------+----------+----------+\n");
#endif
    }

    int32_t calc_load_scaled(uint32_t exec_us, uint32_t period_ms) {
        if (period_ms == 0) {
            return 0;
        }
        int64_t num = (int64_t)exec_us * (int64_t)LOAD_SCALE;
        int64_t den = (int64_t)period_ms * MILLIS_US;
        return (int32_t)(num / den);
    }

    template<class SYS_PACK>
    void add_sys_task(utl::getType<SYS_PACK>)
    {
		SYS_PACK proc;
        add_task((Process<>*)&proc);
    }

#if __cplusplus > 201402L
    template<class... SYS_PACKS>
    void system_tasks_register(utl::simple_list_t<SYS_PACKS...>)
    {
        (add_sys_task(utl::getType<SYS_PACKS>{}), ...);
    }
#else
    void system_tasks_register(utl::simple_list_t<>) {}

    template<class FIRST, class... REST>
    void system_tasks_register(utl::simple_list_t<FIRST, REST...>)
    {
        add_sys_task(utl::getType<FIRST>{});
        system_tasks_register(utl::simple_list_t<REST...>{});
    }
#endif

public:
    Scheduler():
        processes_buf({}), processes({}), smooth_scale_x100(100), last_recompute_ms(0),
        last_scale_x100(0), err_timer(0),
        TPC_timer(SECOND_MS), TPC_counter(0), last_TPC_counter(0)
    {
		circle_buf_gc_init(&processes, (uint8_t*)processes_buf, sizeof(Process<>), __arr_len(processes_buf));
        system_tasks_register(system_task_p{});
    }

    void init()
    {
        _device_rev_show();
    }

    template<void (*ACTION) (void) = nullptr, uint32_t DELAY_MS = 0, bool REALTIME = false, bool WORK_WITH_ERROR = false, uint32_t WEIGHT = 100>
    bool add_task(Process<ACTION, DELAY_MS, REALTIME, WORK_WITH_ERROR, WEIGHT>* const proc)
    {
        BEDUG_ASSERT(!full(), "GSystem user processes is out of range");
        if (full()) {
            return false;
        }

		proc->current_delay_ms = proc->orig_delay_ms;
        proc->timer.changeDelay(proc->current_delay_ms);
        proc->timer.start();
        proc->scale_smooth_pct100 = 100;

		circle_buf_gc_push_back(&processes, (uint8_t*)proc);

        return true;
    }

    void tick()
    {
        TPC_counter++;

        if (!TPC_timer.wait()) {
            last_TPC_counter = __proportion(TPC_timer.end(), TPC_timer.getStart(), (uint32_t)getMillis(), 0, TPC_counter);
            TPC_timer.start();
            TPC_counter = 0;
        }

        for (proc_size_t i = 0; i < circle_buf_gc_count(&processes); i++) {
            Process<>* proc = (Process<>*)circle_buf_gc_index(&processes, i);
            if (proc->timer.wait()) {
                continue;
            }

            if (denied(proc)) {
                continue;
            }

            proc->last_start_us = getMicroseconds();
            if (proc->action) {
                proc->action();
#if !defined(GSYSTEM_NO_PROC_INFO)
                proc->exec_counter++;
#endif
            }
            uint64_t after_us = getMicroseconds();

            uint64_t dur_us = (after_us >= proc->last_start_us) ? (after_us - proc->last_start_us) : 0;
#if !defined(GSYSTEM_NO_PROC_INFO)
            proc->last_exec_us = (uint32_t)dur_us;
#endif

            int64_t new_us100 = (int64_t)dur_us * FIX;
            if (proc->exec_ewma_us_pct100 <= 0) {
                proc->exec_ewma_us_pct100 = new_us100;
            } else {
                proc->exec_ewma_us_pct100 = (
                    ((int64_t)(100 - EWMA_ALPHA_PCT100) * proc->exec_ewma_us_pct100 ) +
                    ((int64_t)EWMA_ALPHA_PCT100 * new_us100)
                ) / 100;
            }

            uint32_t delay_ms = proc->current_delay_ms ? proc->current_delay_ms : proc->orig_delay_ms;
            proc->timer.changeDelay(delay_ms);
            proc->timer.start();

#if defined(GSYSTEM_PROC_INFO_ENABLE)
            if (has_new_status_data() || has_new_error_data()) {
                show_statuses();
                show_errors();
            }
#endif
        }
    }

    void print_status()
    {
#if defined(GSYSTEM_PROC_PROC_ENABLE)
        SYSTEM_BEDUG("System scheduler info");

    #if !defined(GSYSTEM_NO_ADC_W)
        uint32_t voltage = get_system_power_v_x100();
    #endif
        printPretty(
            "Build version: v%s  |  kTPC: %lu.%02lu"
    #if !defined(GSYSTEM_NO_ADC_W)
            "  |  CPU PWR: %lu.%02lu V"
    #endif
            "\n",
            system_device_version(),
            last_TPC_counter / 1000,
            (last_TPC_counter / 10) % 100
    #if !defined(GSYSTEM_NO_ADC_W)
            ,
            voltage / 100,
            voltage % 100
    #endif
        );

        const int32_t warn_threshold_x100 = 500;
        static const char header[] = "| ID | PeriodO(ms) | PeriodC(ms) | ExecAvg(us) | Freq(Hz) | Load(%%) | LastLoad(%%) | Weight(%%) | scaleR | scaleSmo | Realtime |\n";
        print_div_line();
        printPretty(header);
        print_div_line();

        int64_t sum_weighted_load = 0;
        int64_t sum_weights = 0;

        int64_t total_unweighted_load_scaled = 0;

        for (proc_size_t i = 0; i < circle_buf_gc_count(&processes); i++) {
            Process<>* proc  = (Process<>*)circle_buf_gc_index(&processes, i);
            uint32_t periodO = proc->orig_delay_ms;
            uint32_t periodC = proc->current_delay_ms;

            int64_t exec_us100  = proc->exec_ewma_us_pct100;
            uint32_t exec_us_ewma = (uint32_t)(exec_us100 / FIX);

            uint32_t exec_us_last = proc->last_exec_us;

            int64_t load_scaled_ewma = 0;
            if (periodC > 0) {
                load_scaled_ewma = (exec_us100 * (int64_t)LOAD_SCALE) / ((int64_t)periodC * (int64_t)MILLIS_US * (int64_t)FIX);
            }
            proc->last_load_scaled = load_scaled_ewma;

            int32_t load_percent_x100 = (int32_t)((load_scaled_ewma * 100) / LOAD_SCALE);

            int32_t load_last_scaled = 0;
            if (periodC > 0) {
                load_last_scaled = (int32_t)(((int64_t)exec_us_last * (int64_t)LOAD_SCALE) / ((int64_t)periodC * (int64_t)MILLIS_US));
            }
            int32_t load_last_percent_x100 = (load_last_scaled * 100) / LOAD_SCALE;

            sum_weighted_load += (int64_t)proc->weight_pct100 * load_scaled_ewma;
            sum_weights += proc->weight_pct100;
            total_unweighted_load_scaled += load_scaled_ewma;
            
            proc->execs_per_sec_x100 = (uint32_t)(((uint64_t)proc->exec_counter * SECOND_MS * 100) / (uint64_t)LOAD_SHOW_DELAY_MS);
            proc->exec_counter = 0;

            if (i == SYSTEM_TASK_LIST::COUNT) {
                print_div_line();
            }

            printPretty("|");
            gprint(" %02u |", i);
            gprint(" %11lu |", periodO);
            gprint(" %11lu |", periodC);
            gprint(" %11lu |", exec_us_ewma);
            gprint(" %5lu.%02lu |", proc->execs_per_sec_x100 / 100, __abs(proc->execs_per_sec_x100 % 100));
            gprint(" %4lu.%02lu |", load_percent_x100 / FIX, __abs(load_percent_x100 % FIX));
            gprint(" %8lu.%02lu |", load_last_percent_x100 / FIX, __abs(load_last_percent_x100 % FIX));
            gprint(" %6lu.%02lu |", proc->weight_pct100 / FIX, __abs(proc->weight_pct100 % FIX));
            gprint(" %3lu.%02lu |", proc->scale_raw_pct100 / FIX, __abs(proc->scale_raw_pct100 % FIX));
            gprint(" %5lu.%02lu |",  proc->scale_smooth_pct100 / FIX, __abs(proc->scale_smooth_pct100 % FIX));
            gprint(" %8s |", proc->realtime ? "YES" : "NO");
            if ( load_last_percent_x100 > warn_threshold_x100 ) {
                gprint(" WARNING!");
            }
            gprint("\n");
        }

        print_div_line();

        int64_t U_weighted_scaled = 0;
        if (sum_weights > 0) {
            U_weighted_scaled = sum_weighted_load / sum_weights;
        }

        int64_t total_unweighted_percent_x100 = (total_unweighted_load_scaled * 100) / LOAD_SCALE;
        int64_t total_weighted_percent = U_weighted_scaled / 100;

        printPretty(
            "Total sum load: %ld.%02ld%%  |  Total weighted (avg): %ld%%  |  Target util: %d%%\n",
            (int32_t)(total_unweighted_percent_x100/100), (int32_t)(__abs(total_unweighted_percent_x100%100)),
            (int32_t)total_weighted_percent,
            (int32_t)TARGET_UTIL_PCT100
        );

        for (proc_size_t i = 0; i < circle_buf_gc_count(&processes); i++) {
            Process<>* proc = (Process<>*)circle_buf_gc_index(&processes, i);
            int32_t load_s = calc_load_scaled(proc->last_exec_us, proc->current_delay_ms);
            int load_percent_times100 = (load_s * 100) / LOAD_SCALE;
            if (((int32_t)((proc->last_load_scaled * 100) / LOAD_SCALE)) > warn_threshold_x100) {
                printPretty(
                    "WARNING: task %u heavy: %u us (~%d.%02d%%)\n",
                    i,
                    proc->last_exec_us,
                    load_percent_times100 / 100, load_percent_times100 % 100
               );
            }
        }
#endif
    }

    void recompute_scaling()
    {
        int64_t sum_weighted_load = 0;
        int64_t sum_weights = 0;

        for (proc_size_t i = 0; i < circle_buf_gc_count(&processes); i++) {
            Process<>* proc = (Process<>*)circle_buf_gc_index(&processes, i);
            if (proc->realtime) {
                proc->last_load_scaled = 0;
                continue;
            }

            uint32_t period_ms = (proc->current_delay_ms ? proc->current_delay_ms : proc->orig_delay_ms);
            if (period_ms == 0) {
                proc->last_load_scaled = 0;
                continue;
            }

            int64_t exec_us100 = proc->exec_ewma_us_pct100;
            int64_t period_us  = period_ms * MILLIS_US;

            int64_t load_scaled = 0;
            if (period_ms > 0) {
                load_scaled = (exec_us100 * (int64_t)LOAD_SCALE) / (period_us * (int64_t)FIX);
            }else {
                load_scaled = 0;
            }
            proc->last_load_scaled = (int32_t)load_scaled;

            sum_weighted_load += (int64_t)proc->weight_pct100 * load_scaled;
            sum_weights += proc->weight_pct100;
        }
        if (sum_weights <= 0) {
            sum_weights = 1;
        }

        int64_t U_weighted_scaled = sum_weighted_load / sum_weights;
        int64_t target_scaled = (int64_t)TARGET_UTIL_PCT100 * (LOAD_SCALE / 100);
        if (U_weighted_scaled <= TARGET_UTIL_PCT100) {
			for (proc_size_t i = 0; i < circle_buf_gc_count(&processes); i++) {
				Process<>* proc = (Process<>*)circle_buf_gc_index(&processes, i);
                if (proc->realtime) {
                    proc->current_delay_ms    = proc->orig_delay_ms;
                    proc->scale_raw_pct100    = 100;
                    proc->scale_smooth_pct100 = (
                            (int32_t)((100 - SCALE_SMOOTH_ALPHA_PCT100) * (int64_t)proc->scale_smooth_pct100 +
                            (int64_t)SCALE_SMOOTH_ALPHA_PCT100 * 100)
                    ) / 100;
                    continue;
                }

                proc->scale_raw_pct100    = 100;
                proc->scale_smooth_pct100 = (int32_t)(
                    ((100 - SCALE_SMOOTH_ALPHA_PCT100) * (int64_t)proc->scale_smooth_pct100 +
                    (int64_t)SCALE_SMOOTH_ALPHA_PCT100 * (int64_t)proc->scale_raw_pct100)
                ) / 100;

                newd_update(proc);
            }
            return;
        }

        int64_t excess_scaled100 = (U_weighted_scaled * (int64_t)FIX) / target_scaled;
        if (excess_scaled100 < 100) {
            excess_scaled100 = 100;
        }
        int64_t max_excess_scaled100 = MAX_SCALE_PCT100;
        if (excess_scaled100 > max_excess_scaled100) {
            excess_scaled100 = max_excess_scaled100;
        }

        int64_t denom = 0;
        for (proc_size_t i = 0; i < circle_buf_gc_count(&processes); i++) {
            Process<>* proc = (Process<>*)circle_buf_gc_index(&processes, i);
            denom += proc->weight_pct100 * proc->last_load_scaled;
        }
        if (denom <= 0) {
            denom = 1;
        }

        for (proc_size_t i = 0; i < circle_buf_gc_count(&processes); i++) {
            Process<>* proc = (Process<>*)circle_buf_gc_index(&processes, i);
            if (proc->realtime) {
                proc->current_delay_ms    = proc->orig_delay_ms;
                proc->scale_raw_pct100    = 100;
                proc->scale_smooth_pct100 = (int32_t)(
                    ((100 - SCALE_SMOOTH_ALPHA_PCT100) * (int64_t)proc->scale_smooth_pct100 +
                    (int64_t)SCALE_SMOOTH_ALPHA_PCT100 * 100)
                ) / 100;
                continue;
            }

            int64_t contrib = (int64_t)proc->weight_pct100 * proc->last_load_scaled;
            int64_t frac_pct100 = (contrib * (int64_t)FIX) / denom;
            int64_t delta = excess_scaled100 - 100;
            int64_t scale_raw_pct100 = 100 + (delta * frac_pct100) / 100;
            if (scale_raw_pct100 < MIN_SCALE_PCT100) {
                scale_raw_pct100 = MIN_SCALE_PCT100;
            }
            if (scale_raw_pct100 > MAX_SCALE_PCT100) {
                scale_raw_pct100 = MAX_SCALE_PCT100;
            }
            proc->scale_raw_pct100 = (int32_t)scale_raw_pct100;

            proc->scale_smooth_pct100 = (int32_t)(
                ((100 - SCALE_SMOOTH_ALPHA_PCT100) * (int64_t)proc->scale_smooth_pct100 +
                (int64_t)SCALE_SMOOTH_ALPHA_PCT100 * (int64_t)proc->scale_raw_pct100)
            ) / 100;

            newd_update(proc);
        }
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

    void set_timeout(uint32_t timeout_ms)
    {
        err_timer.changeDelay(timeout_ms);
    }

    bool full()
    {
        return circle_buf_gc_full(&processes);
    }
};


#ifndef GSYSTEM_NO_MEMORY_W
extern "C" void memory_watchdog_check();
#endif
#ifndef GSYSTEM_NO_SYS_TICK_W
extern "C" void sys_clock_watchdog_check();
#endif
#ifndef GSYSTEM_NO_RAM_W
extern "C" void ram_watchdog_check();
#endif
#if !defined(GSYSTEM_NO_ADC_W)
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
#ifndef GSYSTEM_NO_DEVICE_SETTINGS
extern "C" void settings_update();
#endif
extern "C" void btn_watchdog_check();
extern "C" void system_scheduler_init();

static Scheduler<
    GSYSTEM_POCESSES_COUNT,
    TaskList<
        Process<_scheduler_load_show,         LOAD_SHOW_DELAY_MS, true,  true, DEFAULT_WEIGHT_PCT100>,
        Process<_scheduler_recompute_scaling, RECOMPUTE_MS,       true,  true, DEFAULT_WEIGHT_PCT100>,
        Process<_scheduler_error_check,       SECOND_MS,          true,  true, DEFAULT_WEIGHT_PCT100>,
#ifndef GSYSTEM_NO_MEMORY_W
        Process<memory_watchdog_check,        100,                false, true, DEFAULT_WEIGHT_PCT100>,
#endif
#ifndef GSYSTEM_NO_SYS_TICK_W
        Process<sys_clock_watchdog_check,     SECOND_MS / 10,     false, true, DEFAULT_WEIGHT_PCT100>,
#endif
#ifndef GSYSTEM_NO_RAM_W
        Process<ram_watchdog_check,           5 * SECOND_MS,      false, true, DEFAULT_WEIGHT_PCT100>,
#endif
#if !defined(GSYSTEM_NO_ADC_W)
        Process<adc_watchdog_check,           1,                  true,  true, DEFAULT_WEIGHT_PCT100>,
#endif
#if defined(STM32F1) && !defined(GSYSTEM_NO_I2C_W)
        Process<i2c_watchdog_check,           5 * SECOND_MS,      false, true, DEFAULT_WEIGHT_PCT100>,
#endif
#ifndef GSYSTEM_NO_RTC_W
        Process<rtc_watchdog_check,           SECOND_MS,          false, true, DEFAULT_WEIGHT_PCT100>,
#endif
#if !defined(GSYSTEM_NO_POWER_W) && !defined(GSYSTEM_NO_ADC_W)
        Process<power_watchdog_check,         1,                  true,  true, DEFAULT_WEIGHT_PCT100>,
#endif
#ifndef GSYSTEM_NO_DEVICE_SETTINGS
        Process<settings_update,              500,                false, true, DEFAULT_WEIGHT_PCT100>,
#endif
        Process<btn_watchdog_check,           5,                  false, true, DEFAULT_WEIGHT_PCT100>
    >
> scheduler;


extern "C" void sys_proc_init()
{
    scheduler.init();
}

extern "C" void system_tick()
{
    scheduler.tick();
}

extern "C" void system_register(void (*task) (void), uint32_t delay_ms, bool realtime, bool work_with_error, int32_t weight_x100)
{
    if (!task) {
        BEDUG_ASSERT(false, "Empty task");
        return;
    }
    if (scheduler.full()) {
        BEDUG_ASSERT(false, "GSystem user processes is out of range");
        return;
    }
	Process<> proc(task, delay_ms, realtime, work_with_error, weight_x100);
    scheduler.add_task(&proc);
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

void _scheduler_load_show(void)
{
    scheduler.print_status();
}

void _scheduler_recompute_scaling()
{
    scheduler.recompute_scaling();
}

void _scheduler_error_check()
{
    scheduler.error_check();
}

