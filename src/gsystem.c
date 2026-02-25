/*
 * @file gsystem.c
 * @brief Core system bootstrap and runtime helpers.
 *
 * Initializes platform subsystems, starts the main loop and provides
 * small runtime utilities (timers, reset handling, system info). Private
 * helper functions and file-local state are documented below.
 */

#include "gsystem.h"

#include <string.h>
#include <unistd.h>
#include <stdbool.h>

#include "glog.h"
#include "clock.h"
#include "bmacro.h"
#include "fsm_gc.h"
#include "gconfig.h"

#include "gdefines.h"
#include "drivers.h"

#include "button.h"
#include "gversion.h"

#if defined(GSYSTEM_DS130X_CLOCK)
#    include "ds130x.h"
#endif


#if GSYSTEM_BEDUG
const char SYSTEM_TAG[] = "GSYS";
#endif


/* 
 * @brief File-local helper: check and log whether the device was restarted recently
 *                           (boot reason inspection). 
 * @param None
 * @return None
 */
static void _system_restart_check(void);


/* @brief Verification word used by system timers. */
const uint32_t TIMER_VERIF_WORD = 0xBEDAC1DE;

/* @brief Run-time messages state. */
static bool messages_enabled = true;

static gversion_t build_ver = { 0 };

#ifndef GSYSTEM_NO_SYS_TICK_W
static bool system_hsi_initialized = false;
#endif

#ifndef GSYSTEM_NO_ADC_W
uint16_t SYSTEM_ADC_VOLTAGE[GSYSTEM_ADC_VOLTAGE_COUNT] = {0};
#endif

uint32_t sys_time_ms = 0;
bool sys_timer_rdy = false;


#if !defined(GSYSTEM_NO_RTC_W)
extern bool __internal_set_clock_ready();
extern bool __internal_is_clock_ready();
extern bool __internal_get_clock_ram(const uint8_t idx, uint8_t* data);
extern bool __internal_set_clock_ram(const uint8_t idx, uint8_t data);
#endif

extern void sys_isr_register();
extern void sys_fill_ram();
void system_init(void)
{
    sys_isr_register();

    sys_fill_ram();

#if defined(GSYSTEM_TIMER)
    sys_timer_rdy = g_sys_tick_start(GSYSTEM_TIMER);
#endif

	if (!gversion_from_string(BUILD_VERSION, strlen(BUILD_VERSION), &build_ver)) {
		memset((void*)&build_ver, 0, sizeof(build_ver));
	}

    system_timer_t timer = {0};
#ifndef GSYSTEM_NO_SYS_TICK_W
    RCC->CR |= RCC_CR_HSEON;

    system_timer_start(&timer, SECOND_MS);
    while (system_timer_wait(&timer)) {
        if (RCC->CR & RCC_CR_HSERDY) {
            break;
        }
    }
    if (!(RCC->CR & RCC_CR_HSERDY)) {
    	set_error(SYS_TICK_ERROR);
        set_status(SYS_TICK_FAULT);
    }
    system_timer_stop(&timer);

    #ifndef GSYSTEM_NO_PLL_CHECK_W

    RCC_PLLInitTypeDef PLL = {0};
    PLL.PLLState = RCC_PLL_ON;
    PLL.PLLSource = RCC_PLLSOURCE_HSE;
    #    if defined(STM32F1)
    PLL.PLLMUL = RCC_PLL_MUL9;
    #    elif defined(STM32F4)
    PLL.PLLM = 4;
    PLL.PLLN = 84;
    PLL.PLLP = RCC_PLLP_DIV2;
    PLL.PLLQ = 4;
    #    endif

    __HAL_RCC_PLL_DISABLE();
    system_timer_start(&timer, SECOND_MS);
    while (system_timer_wait(&timer)) {
        if (__HAL_RCC_GET_FLAG(RCC_FLAG_PLLRDY) == RESET) {
            break;
        }
    }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_PLLRDY) != RESET) {
    	set_error(SYS_TICK_ERROR);
        set_status(SYS_TICK_FAULT);
    }
    system_timer_stop(&timer);

    #    if defined(STM32F1)
    __HAL_RCC_PLL_CONFIG(PLL.PLLSource,
                         PLL.PLLMUL);
    #    elif defined(STM32F4)
    WRITE_REG(RCC->PLLCFGR, (PLL.PLLSource                     | \
                PLL.PLLM                                       | \
             (  PLL.PLLN << RCC_PLLCFGR_PLLN_Pos)              | \
             (((PLL.PLLP >> 1U) - 1U) << RCC_PLLCFGR_PLLP_Pos) | \
             (  PLL.PLLQ << RCC_PLLCFGR_PLLQ_Pos)));
    #    endif
    __HAL_RCC_PLL_ENABLE();

    system_timer_start(&timer, SECOND_MS);
    while (system_timer_wait(&timer)) {
        if (__HAL_RCC_GET_FLAG(RCC_FLAG_PLLRDY) != RESET) {
            break;
        }
    }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_PLLRDY) == RESET) {
    	set_error(SYS_TICK_ERROR);
        set_status(SYS_TICK_FAULT);
    }
    system_timer_stop(&timer);

    #    if defined(STM32F1)
    uint32_t pll_config = RCC->CFGR;
    #    elif defined(STM32F4)
    uint32_t pll_config = RCC->PLLCFGR;
    #    endif
    if (((PLL.PLLState) == RCC_PLL_OFF)                                                                ||
    #    if defined(STM32F1)
       (READ_BIT(pll_config, RCC_CFGR_PLLSRC)  != PLL.PLLSource)                                       ||
       (READ_BIT(pll_config, RCC_CFGR_PLLMULL) != PLL.PLLMUL)
    #    elif defined(STM32F4)
       (READ_BIT(pll_config, RCC_PLLCFGR_PLLSRC) != PLL.PLLSource)                                     ||
       (READ_BIT(pll_config, RCC_PLLCFGR_PLLM)   != (PLL.PLLM) << RCC_PLLCFGR_PLLM_Pos)                ||
       (READ_BIT(pll_config, RCC_PLLCFGR_PLLN)   != (PLL.PLLN) << RCC_PLLCFGR_PLLN_Pos)                ||
       (READ_BIT(pll_config, RCC_PLLCFGR_PLLP)   != (((PLL.PLLP >> 1U) - 1U)) << RCC_PLLCFGR_PLLP_Pos) ||
       (READ_BIT(pll_config, RCC_PLLCFGR_PLLQ)   != (PLL.PLLQ << RCC_PLLCFGR_PLLQ_Pos))
    #    endif
    ) {
    	set_error(SYS_TICK_ERROR);
        set_status(SYS_TICK_FAULT);
    }

    #endif

#endif

    system_timer_start(&timer, 20);
    while (system_timer_wait(&timer));
    system_timer_stop(&timer);

    set_status(SYSTEM_HARDWARE_STARTED);
}

const char* system_device_version()
{
	return gversion_to_string(&build_ver);
}

#ifndef GSYSTEM_NO_ADC_W
extern void adc_watchdog_check();
#endif
extern void sys_jobs_init();
void system_post_load(void)
{
    SYSTEM_BEDUG("GSystem is loading");

    set_status(SYSTEM_SOFTWARE_STARTED);

#ifndef GSYSTEM_NO_CPU_INFO
    SystemInfo();
#endif

    _system_restart_check();

#ifndef GSYSTEM_NO_ADC_W
    const uint32_t delay_ms = 10000;
    gtimer_t timer = {0};
    system_timer_t s_timer = {0};
    bool need_error_timer = is_status(SYS_TICK_FAULT);
    if (need_error_timer) {
        system_timer_start(&s_timer, delay_ms);
    } else {
        gtimer_start(&timer, delay_ms);
    }
    while (1) {
        adc_watchdog_check();

        uint32_t voltage = get_system_power_v_x100();
        if (STM_MIN_VOLTAGEx100 <= voltage && voltage <= STM_MAX_VOLTAGEx100) {
            break;
        }

        if (is_status(SYS_TICK_FAULT)) {
            if (!system_timer_wait(&s_timer)) {
#ifndef GSYSTEM_NO_SYS_TICK_W
                set_error(SYS_TICK_ERROR);
#endif
                break;
            }
        } else if (!gtimer_wait(&timer)) {
#ifndef GSYSTEM_NO_SYS_TICK_W
            set_error(SYS_TICK_ERROR);
#endif
            break;
        }
    }
    if (need_error_timer) {
        system_timer_stop(&s_timer);
    }
#endif

    if (is_error(SYS_TICK_ERROR) || is_error(POWER_ERROR)) {
        system_error_handler(
            (get_first_error() == INTERNAL_ERROR) ?
                LOAD_ERROR :
                (SOUL_STATUS)get_first_error()
        );
    }

#if defined(STM32F1) && \
    !defined(GSYSTEM_NO_TAMPER_RESET) && \
    !defined(GSYSTEM_NO_RTC_W)
    HAL_PWR_EnableBkUpAccess();
    MODIFY_REG(BKP->RTCCR, (BKP_RTCCR_CCO | BKP_RTCCR_ASOE | BKP_RTCCR_ASOS), RTC_OUTPUTSOURCE_NONE);
    CLEAR_BIT(BKP->CSR, BKP_CSR_TPIE);
    SET_BIT(BKP->CSR, BKP_CSR_CTI);
    SET_BIT(BKP->CSR, BKP_CSR_CTE);
    CLEAR_BIT(BKP->CR, BKP_CR_TPE);
    HAL_PWR_DisableBkUpAccess();
#endif

    sys_jobs_init();

    SYSTEM_BEDUG("GSystem loaded");
}

void system_start(void)
{
    system_post_load();

    while (1) {
        system_tick();
    }
}

void system_reset(void)
{
    system_before_reset();
    g_reboot();
}

bool is_system_ready()
{
    return !(has_errors() || is_status(SYSTEM_SAFETY_MODE) || !is_status(SYSTEM_HARDWARE_READY) || !is_status(SYSTEM_SOFTWARE_READY));
}

__attribute__ ((weak)) bool is_software_ready(void)
{
    return true;
}

void system_error_handler(SOUL_STATUS error)
{
    if (is_status(SYSTEM_ERROR_HANDLER_CALLED)) {
        return;
    }
    set_status(SYSTEM_ERROR_HANDLER_CALLED);

    set_error(error);

    bool has_mcu_internal_error = 
        is_error(NON_MASKABLE_INTERRUPT) || is_error(HARD_FAULT) || 
        is_error(MEM_MANAGE) || is_error(BUS_FAULT) ||
        is_error(USAGE_FAULT) || is_error(ERROR_HANDLER_CALLED);

    bool need_error_timer = is_status(SYS_TICK_FAULT) || has_mcu_internal_error;
    if (need_error_timer) {
        fsm_gc_disable_all_messages();
        messages_enabled = false;
        __disable_irq();
    }

    if (!has_errors()) {
        error = INTERNAL_ERROR;
    }

    if (!has_mcu_internal_error) {
        if (is_soul_bedug_enable() && !is_error(POWER_ERROR)) {
            SYSTEM_BEDUG("GSystem_error_handler called error=%s", get_status_name(error));
        } else if (!is_error(POWER_ERROR)) {
            SYSTEM_BEDUG("GSystem_error_handler called error=%u", error);
        }
    }

#ifndef GSYSTEM_NO_SYS_TICK_W
    if (is_error(SYS_TICK_ERROR) && !system_hsi_initialized) {
        system_hsi_config();
    }
#endif

#if !defined(GSYSTEM_NO_RTC_W)
    #if defined(GSYSTEM_DS1307_CLOCK)
    if (!is_clock_started()) {
        GSYSTEM_CLOCK_I2C.Instance = GSYSTEM_CLOCK_I2C_BASE;
        GSYSTEM_CLOCK_I2C.Init.ClockSpeed = 100000;
        GSYSTEM_CLOCK_I2C.Init.DutyCycle = I2C_DUTYCYCLE_2;
        GSYSTEM_CLOCK_I2C.Init.OwnAddress1 = 0;
        GSYSTEM_CLOCK_I2C.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
        GSYSTEM_CLOCK_I2C.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
        GSYSTEM_CLOCK_I2C.Init.OwnAddress2 = 0;
        GSYSTEM_CLOCK_I2C.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
        GSYSTEM_CLOCK_I2C.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
        if (HAL_I2C_Init(&GSYSTEM_CLOCK_I2C) != HAL_OK) {
            set_error(I2C_ERROR);
        }
        clock_begin();
    }
    #endif

	#if defined(GSYSTEM_DOUBLE_BKCP_ENABLE)
    if (!__internal_is_clock_ready()) {
    	__internal_set_clock_ready();
    }
    if (__internal_is_clock_ready()) {
        for (uint8_t i = 0; i < sizeof(error); i++) {
        	__internal_set_clock_ram(i, ((uint8_t*)&error)[i]);
        }
    }
	#endif
    if (!is_clock_ready()) {
        set_clock_ready();
    }
    if (is_clock_ready()) {
        for (uint8_t i = 0; i < sizeof(error); i++) {
            set_clock_ram(i, ((uint8_t*)&error)[i]);
        }
    }
#endif

    if (is_error(POWER_ERROR)) {
#ifndef DEBUG
        __disable_irq();
        __reset_bit(SCB->SCR, SCB_SCR_SEVONPEND_Msk);
        __set_bit(SCB->SCR, SCB_SCR_SLEEPDEEP_Msk | SCB_SCR_SLEEPONEXIT_Msk);
        __WFI();
#else
        g_reboot();
#endif
    }

    if (has_mcu_internal_error) {
        g_reboot();
    }

    uint32_t delay_ms = GSYSTEM_RESET_TIMEOUT_MS;
    gtimer_t timer = {0};
    system_timer_t s_timer = {0};
    if (need_error_timer) {
        system_timer_start(&s_timer, delay_ms);
    } else {
        gtimer_start(&timer, delay_ms);
    }
    while(1) {
        system_error_loop();

        system_tick();

        if (need_error_timer) {
            if (!system_timer_wait(&s_timer)) break;
        } else if (!gtimer_wait(&timer)) {
            break;
        }
    }
    if (need_error_timer) {
        system_timer_stop(&s_timer);
    }

    system_before_reset();

#if GSYSTEM_BEDUG
    system_timer_start(&s_timer, SECOND_MS);
    SYSTEM_BEDUG("GSystem reset\n\n\n");
    while(system_timer_wait(&s_timer));
    system_timer_stop(&s_timer);
#endif

    g_reboot();
}

__attribute__((weak)) void system_before_reset(void) {}

void system_timer_start(system_timer_t* timer, uint32_t delay_ms)
{
    if (!timer) {
        BEDUG_ASSERT(false, "Timer must not be NULL");
        return;
    }
    timer->started  = 1;
    timer->delay_ms = delay_ms;
    timer->start_ms = g_get_millis();
}

bool system_timer_wait(const system_timer_t* timer)
{
    if (!timer) {
        BEDUG_ASSERT(false, "Timer must not be NULL");
        return false;
    }
    return timer->started && timer->start_ms + timer->delay_ms < g_get_millis();
}

void system_timer_stop(system_timer_t* timer)
{
    if (!timer) {
        BEDUG_ASSERT(false, "Timer must not be NULL");
        return;
    }
    timer->started = false;
}

bool system_hw_timer_start(hard_tim_t* timer, void (*callback) (void), uint32_t presc, uint32_t cnt)
{
    if (!timer || !callback) {
        BEDUG_ASSERT(false, "Timer and callback must not be NULL");
        return false;
    }
    return g_hw_timer_start(timer, callback, presc, cnt);
}

void system_hw_timer_stop(hard_tim_t* timer)
{
    if (!timer) {
        BEDUG_ASSERT(false, "Timer must not be NULL");
        return;
    }
    g_hw_timer_stop(timer);
}

#ifndef GSYSTEM_NO_ADC_W
uint32_t get_system_power_v_x100(void)
{
    if (!SYSTEM_ADC_VOLTAGE[0]) {
        return 0;
    }
    return (STM_ADC_MAX * STM_REF_VOLTAGEx100) / SYSTEM_ADC_VOLTAGE[0];
}
#endif

#ifndef GSYSTEM_NO_SYS_TICK_W
__attribute__((weak)) void system_hse_config(void)
{
#ifdef STM32F1
    RCC_OscInitTypeDef RCC_OscInitStruct   = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct   = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.LSIState = RCC_LSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        set_error(SYS_TICK_FAULT);
        return;
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                          |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) {
        set_error(SYS_TICK_FAULT);
        return;
    }
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RTC|RCC_PERIPHCLK_ADC;
    PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
    PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) {
        set_error(SYS_TICK_FAULT);
        return;
    }

    HAL_RCC_EnableCSS();
#elif defined(STM32F4)
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.LSIState = RCC_LSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 4;
    RCC_OscInitStruct.PLL.PLLN = 84;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = 4;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        set_error(SYS_TICK_FAULT);
        return;
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) {
        set_error(SYS_TICK_FAULT);
        return;
    }
#endif
}

__attribute__((weak)) void system_hsi_config(void)
{
#ifdef STM32F1
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.LSIState = RCC_LSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        set_error(SYS_TICK_ERROR);
        return;
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK) {
        set_error(SYS_TICK_ERROR);
        return;
    }
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RTC|RCC_PERIPHCLK_ADC;
    PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
    PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) {
        set_error(SYS_TICK_ERROR);
        return;
    }
#elif defined(STM32F4)
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /** Configure the main internal regulator output voltage
    */
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

    /** Initializes the RCC Oscillators according to the specified parameters
    * in the RCC_OscInitTypeDef structure.
    */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSI
                                        |RCC_OSCILLATORTYPE_LSE;
    RCC_OscInitStruct.LSEState = RCC_LSE_ON;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.LSIState = RCC_LSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = 8;
    RCC_OscInitStruct.PLL.PLLN = 84;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = 4;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        set_error(SYS_TICK_ERROR);
        return;
    }

    /** Initializes the CPU, AHB and APB buses clocks
    */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                                    |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) {
        set_error(SYS_TICK_ERROR);
        return;
    }
#else
    #error "Please select your controller"
#endif

    system_hsi_initialized = true;
}
#endif

__attribute__((weak)) void system_error_loop(void) {}

bool gsystem_messages_enabled()
{
    return messages_enabled;
}

void system_reset_i2c_errata(void)
{
#if defined(GSYSTEM_EEPROM_MODE) || defined(GSYSTEM_I2C) || (defined(GSYSTEM_DS1307_CLOCK) && defined(GSYSTEM_NO_RTC_W))
    SYSTEM_BEDUG("RESET I2C (ERRATA)");

    extern I2C_HandleTypeDef GSYSTEM_I2C;

    if (!GSYSTEM_I2C.Instance) {
        return;
    }

    HAL_I2C_DeInit(&GSYSTEM_I2C);

    GPIO_TypeDef* I2C_PORT = NULL;
    uint16_t I2C_SDA_Pin   = 0;
    uint16_t I2C_SCL_Pin   = 0;
    if (GSYSTEM_I2C.Instance == I2C1) {
        I2C_PORT    = GPIOB;
        I2C_SDA_Pin = GPIO_PIN_7;
        I2C_SCL_Pin = GPIO_PIN_6;
    } else if (GSYSTEM_I2C.Instance == I2C2) {
        I2C_PORT    = GPIOB;
        I2C_SDA_Pin = GPIO_PIN_11;
        I2C_SCL_Pin = GPIO_PIN_10;
    }

    if (!I2C_PORT) {
        SYSTEM_BEDUG("GSystem i2c has not selected");
        system_error_handler(I2C_ERROR);
    }

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin   = I2C_SCL_Pin | I2C_SCL_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(I2C_PORT, &GPIO_InitStruct);

    GSYSTEM_I2C.Instance->CR1 &= (unsigned)~(0x0001);

    GPIO_InitTypeDef GPIO_InitStructure = {0};
    GPIO_InitStructure.Mode = GPIO_MODE_OUTPUT_OD;
#ifdef STM32F4
    GPIO_InitStructure.Alternate = 0;
#endif
    GPIO_InitStructure.Pull = GPIO_PULLUP;
    GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_HIGH;


    GPIO_InitStructure.Pin = I2C_SCL_Pin;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStructure);
    GPIO_InitStructure.Pin = I2C_SDA_Pin;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStructure);

    typedef struct _reseter_t {
        uint16_t      pin;
        GPIO_PinState stat;
    } reseter_t;
    const uint32_t TIMEOUT_MS = 2000;
    gtimer_t timer = {0};
    reseter_t reseter[] = {
        {I2C_SCL_Pin, GPIO_PIN_SET},
        {I2C_SDA_Pin, GPIO_PIN_SET},
        {I2C_SCL_Pin, GPIO_PIN_RESET},
        {I2C_SDA_Pin, GPIO_PIN_RESET},
        {I2C_SCL_Pin, GPIO_PIN_SET},
        {I2C_SDA_Pin, GPIO_PIN_SET},
    };

    reset_error(I2C_ERROR);
    for (unsigned i = 0; i < __arr_len(reseter); i++) {
        HAL_GPIO_WritePin(I2C_PORT, reseter[i].pin, reseter[i].stat);
        gtimer_start(&timer, TIMEOUT_MS);
        while (reseter[i].stat != HAL_GPIO_ReadPin(I2C_PORT, reseter[i].pin)) {
            if (!gtimer_wait(&timer)) {
                set_error(I2C_ERROR);
                break;
            }
            asm("nop");
        }
    }

    GPIO_InitStructure.Mode = GPIO_MODE_AF_OD;
#ifdef STM32F4
    GPIO_InitStructure.Alternate = GPIO_AF4_I2C1;
#endif

    GPIO_InitStructure.Pin = I2C_SCL_Pin;
    HAL_GPIO_Init(I2C_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.Pin = I2C_SDA_Pin;
    HAL_GPIO_Init(I2C_PORT, &GPIO_InitStructure);

    GSYSTEM_I2C.Instance->CR1 |= 0x8000;
    asm("nop");
    GSYSTEM_I2C.Instance->CR1 &= (unsigned)~0x8000;
    asm("nop");

    GSYSTEM_I2C.Instance->CR1 |= 0x0001;

    HAL_I2C_Init(&GSYSTEM_I2C);
#elif defined(STM32F1) && !defined(GSYSTEM_NO_I2C_W)
    #warning "GSystem i2c has not selected"
#endif
}

uint64_t get_system_serial(void)
{
    return g_serial();
}

char* get_system_serial_str(void)
{
    return g_serial_number();
}

#ifndef GSYSTEM_NO_SYS_TICK_W
void system_sys_tick_reanimation(void)
{
    extern void SystemClock_Config(void);

    __disable_irq();

    set_error(SYS_TICK_FAULT);
    set_error(SYS_TICK_ERROR);

    RCC->CIR |= RCC_CIR_CSSC;

    system_timer_t timer = {0};
    system_timer_start(&timer, 5 * SECOND_MS);
    while (system_timer_wait(&timer));
    system_timer_stop(&timer);

    RCC->CR |= RCC_CR_HSEON;
    system_timer_start(&timer, 5 * SECOND_MS);
    while (system_timer_wait(&timer)) {
        if (RCC->CR & RCC_CR_HSERDY) {
            reset_error(SYS_TICK_FAULT);
            reset_error(SYS_TICK_ERROR);
            break;
        }
    }
    system_timer_stop(&timer);


    if (is_error(SYS_TICK_ERROR)) {
        system_hsi_config();
        reset_error(SYS_TICK_ERROR);
    } else {
        system_hse_config();
    }
    reset_error(NON_MASKABLE_INTERRUPT);


    if (is_status(SYS_TICK_FAULT)) {
        SYSTEM_BEDUG("Critical external RCC failure");
        SYSTEM_BEDUG("The internal RCC has been started");
    } else {
        SYSTEM_BEDUG("Critical external RCC failure");
        SYSTEM_BEDUG("The external RCC has been restarted");
    }

    __enable_irq();
}

void HAL_RCC_CSSCallback(void)
{
    system_sys_tick_reanimation();
}

#endif

#ifndef GSYSTEM_NO_ADC_W
uint16_t get_system_adc(unsigned index)
{
#if GSYSTEM_ADC_VOLTAGE_COUNT <= 1
    (void)index;
    return 0;
#else
    if (index + 1 >= GSYSTEM_ADC_VOLTAGE_COUNT) {
        return 0;
    }
    return SYSTEM_ADC_VOLTAGE[index+1];
#endif
}
#endif

#ifndef GSYSTEM_NO_RTC_W
bool get_system_bckp(const uint8_t idx, uint8_t* data)
{
    if (idx + sizeof(SOUL_STATUS) >= SYSTEM_BKUP_SIZE) {
        return false;
    }
    return get_clock_ram(idx + sizeof(SOUL_STATUS), data);
}

bool set_system_bckp(const uint8_t idx, const uint8_t data)
{
    if (idx + sizeof(SOUL_STATUS) >= SYSTEM_BKUP_SIZE) {
        return false;
    }
    return set_clock_ram(idx + sizeof(SOUL_STATUS), data);
}
#endif

void system_delay_us(uint64_t us)
{
    uint64_t start = getMicroseconds();
    while (start + us > getMicroseconds());
}

void SYSTEM_BEDUG(const char* format, ...)
{
#if GSYSTEM_BEDUG
    if (gsystem_messages_enabled()) {
        __g_print_tag(SYSTEM_TAG);
        va_list args;
        va_start(args, format);
        printMessage(format, args);
        va_end(args);
        gprint("\n");
    }
#endif
}

uint64_t system_micros()
{
    if (sys_timer_rdy) {
        return g_get_micros();
    }
    return getMicroseconds();
}

uint32_t system_millis()
{
    if (sys_timer_rdy) {
        return g_get_millis();
    }
    return getMillis();
}

void _system_restart_check(void)
{
    g_restart_check();
}
