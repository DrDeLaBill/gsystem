
/*
 * @file g_job.cpp
 * @brief Job scheduler and runtime statistics collector.
 *
 * Implements the adaptive scheduler used by the system to balance periodic
 * tasks, collect execution timings and present lightweight profiling info
 * in debug builds.
 *
 * Copyright © 2025 Georgy E. All rights reserved.
 */

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


#define GSYSTEM_INTERNAL_PROCCESS_PRIORITY ((uint8_t)50)


static void _device_rev_show();
static void _scheduler_load_show();
static void _scheduler_recompute_scaling();
static void _scheduler_error_check();


static constexpr uint32_t RECOMPUTE_MS         = 200;
static constexpr uint32_t TARGET_CPU_LOAD_X100 = 7000;
static constexpr uint32_t SCALE_SMOOTH_ALPHA   = 5;
static constexpr uint32_t EXEC_SMOOTH_ALPHA    = 90;
static constexpr uint32_t FIX                  = 100;
static constexpr uint32_t LOAD_SCALE           = 10000;
static constexpr uint32_t JOB_SCALE_MAX_X100   = 9000000;
static constexpr uint32_t LOAD_SHOW_DELAY_MS   = SECOND_MS;
static constexpr uint32_t JOBS_BUF_SIZE        = GSYSTEM_MIN_PROCCESS_CNT + GSYSTEM_POCESSES_COUNT;


struct Job {
	static constexpr uint32_t MAX_DELAY_MS = MINUTE_MS;

    void       (*action)(void)     = NULL;
    uint32_t   orig_delay_ms       = 0;      // Original period
    uint32_t   current_delay_ms    = 0;      // Real calculated period
    bool       work_with_error     = false;  // Work with error flag
    bool       realtime            = false;  // Scale disable flag
    bool       system_task         = false;  // System task flag

    uint64_t   last_start_us       = 0;      // Start time
    uint64_t   last_end_us         = 0;      // End time

    uint32_t   last_exec_sum_us    = 0;
    uint32_t   last_average_us     = 0;
    uint32_t   exec_sum_us         = 0;
    uint64_t   exec_sum_start_us   = 0;

    uint32_t   scale_x100          = 0;
    uint8_t    priority            = GSYSTEM_PROCCESS_PRIORITY_DEFAULT;

#if !defined(GSYSTEM_NO_PROC_INFO)
    uint32_t   last_max_exec_us     = 0;
    uint32_t   max_exec_us          = 0;     // Max execution time
#endif
    uint32_t   last_exec_counter    = 0;
    uint32_t   exec_counter         = 0;     // Execution counter
    bool       isr                  = false;


    Job() {}

    Job(
        void     (*action)(void),
        uint32_t delay_ms,
        bool     realtime        = false,
        bool     work_with_error = false,
        uint8_t  priority        = GSYSTEM_PROCCESS_PRIORITY_DEFAULT,
        bool     isr             = false
    ):
        action(action), orig_delay_ms(delay_ms), current_delay_ms(delay_ms),
        work_with_error(work_with_error), realtime(realtime), system_task(false),
        last_start_us(0), last_end_us(0), last_exec_sum_us(0), last_average_us(0),
        exec_sum_us(0), exec_sum_start_us(0), scale_x100(0), priority(priority),
#if !defined(GSYSTEM_NO_PROC_INFO)
    	last_max_exec_us(0), max_exec_us(0),
#endif
        last_exec_counter(0), exec_counter(0), isr(isr)
    {}
    
    bool denied()
    {
        if (system_task) {
            return false;
        }
        if (is_error(HARD_FAULT)) {
            return true;;
        }
        if (is_status(SYSTEM_ERROR_HANDLER_CALLED) && !work_with_error) {
            return true;
        }
        return false;
    }

    void updateCount(uint64_t now_us = system_micros())
    {
        if (last_end_us + SECOND_US < now_us) {
            last_exec_sum_us = (last_exec_sum_us * EXEC_SMOOTH_ALPHA) / 100;
        }
    }

    void exec(uint64_t now_us = system_micros())
    {
        last_start_us = now_us;
        if (action) {
            action();
            exec_counter++;
        }
        last_end_us = system_micros();

        if (exec_sum_start_us + SECOND_US < last_end_us) {
            uint32_t last_period = (uint32_t)(last_end_us > exec_sum_start_us ? last_end_us - exec_sum_start_us : SECOND_US);
            last_exec_sum_us = (uint32_t)__proportion((uint64_t)exec_sum_us, 0, (uint64_t)last_period, 0, (uint64_t)SECOND_US);
            last_average_us = exec_sum_us / (exec_counter > 0 ? exec_counter : 1);
            exec_sum_start_us = last_end_us;
            exec_sum_us = 0;
            last_exec_counter = (uint32_t)__proportion((uint64_t)exec_counter, 0, (uint64_t)last_period, 0, (uint64_t)SECOND_US);
            exec_counter = 0;
        }

        uint32_t dur_us = (uint32_t)((last_end_us > last_start_us) ? (last_end_us - last_start_us) : 0);
        exec_sum_us += dur_us;
#if !defined(GSYSTEM_NO_PROC_INFO)
        if (max_exec_us < dur_us) {
            last_max_exec_us = dur_us;
            max_exec_us = dur_us;
        }
#endif
    }

    uint32_t get_load_x100()
    {
        return (uint32_t)__proportion((uint64_t)last_exec_sum_us, 0, (uint64_t)SECOND_US, 0, (uint64_t)LOAD_SCALE);
    }
};

static circle_buf_gc_t jobs = {};
static Job jobs_buf[JOBS_BUF_SIZE] = {};

class Scheduler {
private:
    const uint32_t LOAD_WRN_X100 = 500;
    const uint32_t LOAD_ERR_X100 = 1000;
    const char* COLOR_DEFAULT    = "\x1b[0m";
    const char* COLOR_WARN       = "\x1b[33m";
    const char* COLOR_ERROR      = "\x1b[31m";

	circle_buf_gc_t* const jobs;

    uint32_t   smooth_scale_x100;
    uint32_t   last_recompute_ms;
    uint32_t   last_scale_x100;

    utl::Timer err_timer;

    utl::Timer TPC_timer;
    uint32_t   TPC_counter;
    uint32_t   last_TPC_counter;

    uint32_t   isr_job_idx;

    uint32_t   last_sum_reset_us;

    uint32_t   jobs_scale_x100;

    void print_div_line()
    {
#if defined(GSYSTEM_PROC_PROC_ENABLE)
        printPretty("+----+------------+----------+---------+---------+---------+------+----------+----------+\n");
#endif
    }

public:
    Scheduler(
        circle_buf_gc_t* const jobs,
        Job* const jobs_buf,
        const uint32_t jobs_cnt,
        std::initializer_list<Job> sys_jobs
    ):
        jobs(jobs), smooth_scale_x100(100), last_recompute_ms(0),
        last_scale_x100(0), err_timer(0),
        TPC_timer(SECOND_MS), TPC_counter(0), last_TPC_counter(0),
        isr_job_idx(0), last_sum_reset_us(0), jobs_scale_x100(0)
    {
        BEDUG_ASSERT(
		    circle_buf_gc_init(jobs, (uint8_t*)jobs_buf, sizeof(Job), jobs_cnt),
            "GSystem jobs buffer initialization error"
        )
        for (unsigned i = 0 ; i < sys_jobs.size(); i++) {
            Job* job = (Job*)((Job*)sys_jobs.begin() + i);
            job->system_task = true;
            add_task(job);
        }
    }

    void init()
    {
        _device_rev_show();
    }

    bool add_task(Job* const job)
    {
        BEDUG_ASSERT(!full(), "GSystem jobs is out of range");
        if (full()) {
            return false;
        }

		job->current_delay_ms = job->orig_delay_ms;
        if (job->priority <= GSYSTEM_INTERNAL_PROCCESS_PRIORITY && !job->system_task) {
            job->priority = GSYSTEM_INTERNAL_PROCCESS_PRIORITY + 1;
        }
        if (job->priority > GSYSTEM_PROCCESS_PRIORITY_MAX) {
            job->priority = GSYSTEM_PROCCESS_PRIORITY_MAX;
        }

		circle_buf_gc_push_back(jobs, (uint8_t*)job);

        return true;
    }

    void tick(bool isr = false)
    {
        if (!isr) {
            TPC_counter++;

            if (!TPC_timer.wait()) {
                last_TPC_counter = __proportion(TPC_timer.end(), TPC_timer.getStart(), (uint32_t)system_millis(), 0, TPC_counter);
                TPC_timer.start();
                TPC_counter = 0;
            }
        }

        uint32_t len = circle_buf_gc_count(jobs);
        if (isr_job_idx >= len) {
            isr_job_idx = 0;
        }
        uint32_t i = isr ? isr_job_idx : 0;
        for (; i < len; i++) {
            Job* job = (Job*)circle_buf_gc_index(jobs, i);
            if (isr != job->isr) {
                continue;
            }

            uint64_t time_us = system_micros();

            job->updateCount(time_us);

            if (job->last_end_us + job->current_delay_ms * MILLIS_US > time_us) {
                continue;
            }

            if (job->denied()) {
                continue;
            }

            job->exec(time_us);
#if defined(GSYSTEM_PROC_INFO_ENABLE)
            if (isr) {
                break;
            }
            if (has_new_status_data() || has_new_error_data()) {
                show_statuses();
                show_errors();
            }
#endif
        }
        if (isr) {
            isr_job_idx++;
        }
    }

    void print_status()
    {
#if defined(GSYSTEM_PROC_PROC_ENABLE)
        printf("\033[2J\033[H");
        SYSTEM_BEDUG("System scheduler info");

    #if !defined(GSYSTEM_NO_ADC_W)
        uint32_t voltage = get_system_power_v_x100();
    #endif
        printPretty(
            "Build version: v%s | kTPC: %lu.%02lu"
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

        static const char header[] = "| ID | Period(ms) | Freq(Hz) | Load(%%) | AVG(us) | Max(us) | Prio | Scale(%%) | Realtime |\n";
        print_div_line();
        printPretty(header);
        print_div_line();

        uint32_t total_load_x100 = 0;
        auto show = [&] (Job* job, uint32_t index) {
            uint32_t load_percent_x100 = job->get_load_x100();
            total_load_x100 += load_percent_x100;
            uint32_t load_max_exec_us_x100 = (uint32_t)__proportion((uint64_t)job->last_max_exec_us, 0, (uint64_t)SECOND_US, 0, (uint64_t)LOAD_SCALE);
            uint32_t scale_x100 = job->scale_x100 + LOAD_SCALE;
            if (!job->realtime) {
                scale_x100 += jobs_scale_x100;
            }

            const char* color = COLOR_DEFAULT;
            if (load_percent_x100 > LOAD_ERR_X100) {
                color = COLOR_ERROR;
            } else if (load_percent_x100 > LOAD_WRN_X100) {
                color = COLOR_WARN;
            }
            
	        gprint("%s", color);
            printPretty("|");
            gprint(" %02u |", index);
            gprint(" %10lu |", (job->current_delay_ms > job->orig_delay_ms && !job->realtime) ? job->current_delay_ms : job->orig_delay_ms);
            gprint(" %8lu |", job->last_exec_counter);
            gprint(" %4lu.%02lu |", load_percent_x100 / FIX, __abs(load_percent_x100 % FIX));
            gprint(" %7lu |", job->last_average_us);
            if (color == COLOR_DEFAULT && load_max_exec_us_x100 > LOAD_WRN_X100) {
	            gprint("%s", COLOR_WARN);
            }
            gprint(" %7lu", job->last_max_exec_us);
            if (color == COLOR_DEFAULT && load_max_exec_us_x100 > LOAD_WRN_X100) {
	            gprint("%s", COLOR_DEFAULT);
            }
            gprint(" | %4lu |", job->priority);
            gprint(" %5lu.%02lu |", scale_x100 / FIX, __abs(scale_x100 % FIX));
            gprint(" %8s |", job->realtime ? "YES" : "NO");
	        gprint("%s\n", COLOR_DEFAULT);

            job->last_max_exec_us = job->max_exec_us;
            job->max_exec_us = 0;
        };

        for (uint32_t i = 0; i < circle_buf_gc_count(jobs); i++) {
            Job* job  = (Job*)circle_buf_gc_index(jobs, i);
            if (i == GSYSTEM_MIN_PROCCESS_CNT) {
                print_div_line();
            }
            if (!job->isr) {
                show(job, i);
            }
        }
        print_div_line();

        bool isr_printed = false;
        for (uint32_t i = 0; i < circle_buf_gc_count(jobs); i++) {
            Job* job  = (Job*)circle_buf_gc_index(jobs, i);
            if (job->isr) {
                isr_printed = true;
                show(job, i);
            }
        }
        if (isr_printed) {
            print_div_line();
        }

        const char* color = COLOR_DEFAULT;
        if (total_load_x100 > TARGET_CPU_LOAD_X100 + LOAD_WRN_X100) {
            color = COLOR_ERROR;
        } else if (total_load_x100 > TARGET_CPU_LOAD_X100 - LOAD_ERR_X100) {
            color = COLOR_WARN;
        }
        uint32_t debug_scale_x100 = jobs_scale_x100 + LOAD_SCALE;
        gprint("%s", color);
        printPretty(
            "Total sum load: %ld.%02ld%% | Target load: %ld.%02ld%% | Jobs scale: %ld.%02ld%%\n",
            (uint32_t)(total_load_x100 / 100), (uint32_t)(__abs(total_load_x100 % 100)),
            (uint32_t)(TARGET_CPU_LOAD_X100 / 100), (uint32_t)(__abs(TARGET_CPU_LOAD_X100 % 100)),
            (uint32_t)(debug_scale_x100 / 100), (uint32_t)(__abs(debug_scale_x100 % 100))
        );  
        gprint("%s", COLOR_DEFAULT);
#endif
    }

    void recompute_scaling()
    {
        uint32_t total_load_x100 = 0;
        uint32_t total_realtime_load_x100 = 0;
        uint32_t jobs_cnt = circle_buf_gc_count(jobs);
        uint32_t realtime_jobs_cnt = 0;
        for (uint32_t i = 0; i < jobs_cnt; i++) {
            Job* job = (Job*)circle_buf_gc_index(jobs, i);
            uint32_t load_x100 = job->get_load_x100();
            total_load_x100 += load_x100;
            if (job->realtime) {
                total_realtime_load_x100 += load_x100;
                job->current_delay_ms = job->orig_delay_ms;
                realtime_jobs_cnt++;
                job->scale_x100 = 0;
                continue;
            }

            uint32_t period_ms = job->orig_delay_ms;
            if (period_ms == 0) {
                period_ms = 1;
            }
            
            if (load_x100 > LOAD_WRN_X100) {
                uint32_t load_delta_x100 = load_x100 - LOAD_WRN_X100;
                job->scale_x100 = (uint32_t)(
                    ((uint64_t)job->last_exec_sum_us * 
                    ((uint64_t)LOAD_SCALE + (uint64_t)load_delta_x100)) / 
                    (uint64_t)LOAD_WRN_X100
				);
            } else {
                job->scale_x100 = job->scale_x100 > 0 ? (job->scale_x100 * (100 - SCALE_SMOOTH_ALPHA) / 100) : 0;
            }
            if (job->scale_x100 > JOB_SCALE_MAX_X100) {
                job->scale_x100 = JOB_SCALE_MAX_X100;
            }

            if (job->scale_x100) {
                job->current_delay_ms = period_ms * (job->priority * FIX + job->scale_x100) / LOAD_SCALE;
            } else {
                job->current_delay_ms = job->orig_delay_ms;
            }
        }

        if (jobs_cnt <= realtime_jobs_cnt) {
            jobs_scale_x100 = 0;
            return;
        }

        if (total_load_x100 <= TARGET_CPU_LOAD_X100) {
            jobs_scale_x100 = jobs_scale_x100 > 0 ? (jobs_scale_x100 * (100 - SCALE_SMOOTH_ALPHA) / 100) : 0;
            return;
        }

        uint32_t total_load_delta_x100 = total_load_x100 - TARGET_CPU_LOAD_X100;
        jobs_scale_x100 = (uint32_t)(
            ((uint64_t)total_load_x100 * 
            ((uint64_t)LOAD_SCALE + (uint64_t)total_load_delta_x100)) / 
            (uint64_t)TARGET_CPU_LOAD_X100
		);
        if (jobs_scale_x100 > JOB_SCALE_MAX_X100) {
            jobs_scale_x100 = JOB_SCALE_MAX_X100;
        }

        for (uint32_t i = 0; i < jobs_cnt; i++) {
            Job* job = (Job*)circle_buf_gc_index(jobs, i);
            if (job->realtime) {
                continue;
            }

            uint32_t period_ms = job->orig_delay_ms;
            if (period_ms == 0) {
                period_ms = 1;
            }

            if (jobs_scale_x100) {
                uint32_t scale_x100 = jobs_scale_x100 + job->scale_x100;
                job->current_delay_ms = period_ms * (job->priority * FIX + scale_x100) / LOAD_SCALE;
            } else {
                job->current_delay_ms = job->orig_delay_ms;
            }
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
        return circle_buf_gc_full(jobs);
    }

    unsigned job_count()
    {
        return circle_buf_gc_count(jobs);
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

static Scheduler scheduler(
    &jobs,
    jobs_buf,
    __arr_len(jobs_buf),
    {
        {_scheduler_load_show,         LOAD_SHOW_DELAY_MS, true,  true, GSYSTEM_INTERNAL_PROCCESS_PRIORITY, false},
        {_scheduler_recompute_scaling, RECOMPUTE_MS,       true,  true, GSYSTEM_INTERNAL_PROCCESS_PRIORITY, false},
        {_scheduler_error_check,       200,                true,  true, GSYSTEM_INTERNAL_PROCCESS_PRIORITY, false},
#ifndef GSYSTEM_NO_MEMORY_W
        {memory_watchdog_check,        100,                false, true, GSYSTEM_INTERNAL_PROCCESS_PRIORITY, false},
#endif
#ifndef GSYSTEM_NO_SYS_TICK_W
        {sys_clock_watchdog_check,     SECOND_MS / 10,     false, true, GSYSTEM_INTERNAL_PROCCESS_PRIORITY, false},
#endif
#ifndef GSYSTEM_NO_RAM_W
        {ram_watchdog_check,           5 * SECOND_MS,      false, true, GSYSTEM_INTERNAL_PROCCESS_PRIORITY, false},
#endif
#if !defined(GSYSTEM_NO_ADC_W)
        {adc_watchdog_check,           1,                  true,  true, GSYSTEM_INTERNAL_PROCCESS_PRIORITY, false},
#endif
#if defined(STM32F1) && !defined(GSYSTEM_NO_I2C_W)
        {i2c_watchdog_check,           5 * SECOND_MS,      false, true, GSYSTEM_INTERNAL_PROCCESS_PRIORITY, false},
#endif
#ifndef GSYSTEM_NO_RTC_W
        {rtc_watchdog_check,           SECOND_MS,          false, true, GSYSTEM_INTERNAL_PROCCESS_PRIORITY, false},
#endif
#if !defined(GSYSTEM_NO_POWER_W) && !defined(GSYSTEM_NO_ADC_W)
        {power_watchdog_check,         1,                  true,  true, GSYSTEM_INTERNAL_PROCCESS_PRIORITY, false},
#endif
#ifndef GSYSTEM_NO_DEVICE_SETTINGS
        {settings_update,              500,                false, true, GSYSTEM_INTERNAL_PROCCESS_PRIORITY, false},
#endif
        {btn_watchdog_check,           5,                  false, true, GSYSTEM_INTERNAL_PROCCESS_PRIORITY, false}
    }
);


extern "C" void sys_jobs_init()
{
    scheduler.init();
}

extern "C" void system_tick()
{
    scheduler.tick(false);
}

extern "C" void system_tick_isr()
{
    scheduler.tick(true);
}

extern "C" void system_register(void (*task) (void), uint32_t delay_ms, bool realtime, bool work_with_error, uint32_t priority)
{
    if (!task) {
        BEDUG_ASSERT(false, "Empty task");
        return;
    }
    if (scheduler.full()) {
        BEDUG_ASSERT(false, "GSystem user jobs is out of range");
        return;
    }
	Job job(task, delay_ms, realtime, work_with_error, (uint8_t)priority, false);
    SYSTEM_BEDUG("add job[%02u] (addr=0x%08X delay_ms=%lu)", scheduler.job_count(), task, delay_ms);
    scheduler.add_task(&job);
}

void system_register_isr(void (*task) (void), uint32_t delay_ms, bool realtime, bool work_with_error, uint32_t priority)
{
    if (!task) {
        BEDUG_ASSERT(false, "Empty task");
        return;
    }
    if (scheduler.full()) {
        BEDUG_ASSERT(false, "GSystem user jobs is out of range");
        return;
    }
	Job job(task, delay_ms, realtime, work_with_error, (uint8_t)priority, true);
    scheduler.add_task(&job);
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

    const char SERIAL_START[] = "CPU SERIAL ";
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

    #if !defined(GSYSTEM_NO_BEDUG)
    g_uart_print(str, (uint16_t)strlen(str));
    #endif

    #if !defined(GSYSTEM_NO_PRINTF) && !defined(GSYSTEM_NO_BEDUG)
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
