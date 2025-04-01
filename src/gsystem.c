/* Copyright © 2025 Georgy E. All rights reserved. */

#include "gsystem.h"

#include <unistd.h>
#include <stdbool.h>

#include "main.h"
#include "glog.h"
#include "clock.h"
#include "bmacro.h"
#include "gconfig.h"
#include "gdefines.h"
#include "hal_defs.h"

#if defined(GSYSTEM_DS130X_CLOCK)
#   include "ds130x.h"
#endif

#if GSYSTEM_BUTTONS_COUNT
#   include "button.h"
#endif

#if GSYSTEM_BEDUG
const char SYSTEM_TAG[] = "GSYS";
#endif


static void _system_restart_check(void);
#ifndef GSYSTEM_NO_RAM_W
static void _fill_ram();
#endif


static const uint32_t TIMER_VERIF_WORD = 0xBEDAC1DE;
static const uint32_t TIMER_FREQ_MUL   = 2;

#if GSYSTEM_BUTTONS_COUNT
unsigned buttons_count = 0;
button_t buttons[GSYSTEM_BUTTONS_COUNT] = {0};
#endif

#ifndef GSYSTEM_NO_SYS_TICK_W
static bool system_hsi_initialized = false;
#endif

#ifndef GSYSTEM_NO_ADC_W
uint16_t SYSTEM_ADC_VOLTAGE[GSYSTEM_ADC_VOLTAGE_COUNT] = {0};
#endif

#ifndef GSYSTEM_TIMER
#   define GSYSTEM_TIMER (TIM1)
#endif


extern void sys_isr_register();
void system_init(void)
{
    sys_isr_register();

#ifndef GSYSTEM_NO_RAM_W
    _fill_ram();
#endif

    system_timer_t timer = {0};
#ifndef GSYSTEM_NO_SYS_TICK_W
    RCC->CR |= RCC_CR_HSEON;

    system_timer_start(&timer, GSYSTEM_TIMER, SECOND_MS);
    while (system_timer_wait(&timer)) {
        if (RCC->CR & RCC_CR_HSERDY) {
            break;
        }
    }
    if (!(RCC->CR & RCC_CR_HSERDY)) {
        set_status(SYS_TICK_ERROR);
        set_error(SYS_TICK_FAULT);
    }
    system_timer_stop(&timer);

#   ifndef GSYSTEM_NO_PLL_CHECK_W

    RCC_PLLInitTypeDef PLL = {0};
    PLL.PLLState = RCC_PLL_ON;
    PLL.PLLSource = RCC_PLLSOURCE_HSE;
#       if defined(STM32F1)
    PLL.PLLMUL = RCC_PLL_MUL9;
#       elif defined(STM32F4)
    PLL.PLLM = 4;
    PLL.PLLN = 84;
    PLL.PLLP = RCC_PLLP_DIV2;
    PLL.PLLQ = 4;
#       endif

    __HAL_RCC_PLL_DISABLE();
    system_timer_start(&timer, GSYSTEM_TIMER, SECOND_MS);
    while (system_timer_wait(&timer)) {
        if (__HAL_RCC_GET_FLAG(RCC_FLAG_PLLRDY) == RESET) {
            break;
        }
    }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_PLLRDY) != RESET) {
        set_status(SYS_TICK_ERROR);
        set_error(SYS_TICK_FAULT);
    }
    system_timer_stop(&timer);

#       if defined(STM32F1)
    __HAL_RCC_PLL_CONFIG(PLL.PLLSource,
                         PLL.PLLMUL);
#       elif defined(STM32F4)
    WRITE_REG(RCC->PLLCFGR, (PLL.PLLSource                     | \
                PLL.PLLM                                       | \
             (  PLL.PLLN << RCC_PLLCFGR_PLLN_Pos)              | \
             (((PLL.PLLP >> 1U) - 1U) << RCC_PLLCFGR_PLLP_Pos) | \
             (  PLL.PLLQ << RCC_PLLCFGR_PLLQ_Pos)));
#       endif
    __HAL_RCC_PLL_ENABLE();

    system_timer_start(&timer, GSYSTEM_TIMER, SECOND_MS);
    while (system_timer_wait(&timer)) {
        if (__HAL_RCC_GET_FLAG(RCC_FLAG_PLLRDY) != RESET) {
            break;
        }
    }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_PLLRDY) == RESET) {
        set_status(SYS_TICK_ERROR);
        set_error(SYS_TICK_FAULT);
    }
    system_timer_stop(&timer);

#       if defined(STM32F1)
    uint32_t pll_config = RCC->CFGR;
#       elif defined(STM32F4)
    uint32_t pll_config = RCC->PLLCFGR;
#       endif
    if (((PLL.PLLState) == RCC_PLL_OFF)                                                                ||
#       if defined(STM32F1)
       (READ_BIT(pll_config, RCC_CFGR_PLLSRC)  != PLL.PLLSource)                                       ||
       (READ_BIT(pll_config, RCC_CFGR_PLLMULL) != PLL.PLLMUL)
#       elif defined(STM32F4)
       (READ_BIT(pll_config, RCC_PLLCFGR_PLLSRC) != PLL.PLLSource)                                     ||
       (READ_BIT(pll_config, RCC_PLLCFGR_PLLM)   != (PLL.PLLM) << RCC_PLLCFGR_PLLM_Pos)                ||
       (READ_BIT(pll_config, RCC_PLLCFGR_PLLN)   != (PLL.PLLN) << RCC_PLLCFGR_PLLN_Pos)                ||
       (READ_BIT(pll_config, RCC_PLLCFGR_PLLP)   != (((PLL.PLLP >> 1U) - 1U)) << RCC_PLLCFGR_PLLP_Pos) ||
       (READ_BIT(pll_config, RCC_PLLCFGR_PLLQ)   != (PLL.PLLQ << RCC_PLLCFGR_PLLQ_Pos))
#       endif
    ) {
        set_status(SYS_TICK_ERROR);
        set_error(SYS_TICK_FAULT);
    }

#   endif

#endif

    // us delay initialize
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    system_timer_start(&timer, GSYSTEM_TIMER, 20);
    while (system_timer_wait(&timer));
    system_timer_stop(&timer);

    set_status(SYSTEM_HARDWARE_STARTED);
}

#ifndef GSYSTEM_NO_ADC_W
extern void adc_watchdog_check();
#endif
extern void sys_proc_init();
void system_post_load(void)
{
    set_status(SYSTEM_SOFTWARE_STARTED);

    SystemInfo();

    _system_restart_check();

#ifndef GSYSTEM_NO_ADC_W
    const uint32_t delay_ms = 10000;
    gtimer_t timer = {0};
    system_timer_t s_timer = {0};
    bool need_error_timer = is_status(SYS_TICK_FAULT);
    if (need_error_timer) {
        system_timer_start(&s_timer, GSYSTEM_TIMER, delay_ms);
    } else {
        gtimer_start(&timer, delay_ms);
    }
    while (1) {
        adc_watchdog_check();

        uint32_t voltage = get_system_power();
        if (STM_MIN_VOLTAGEx10 <= voltage && voltage <= STM_MAX_VOLTAGEx10) {
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

    sys_proc_init();
}

void system_start(void)
{
#if GSYSTEM_BEDUG
    gprint("\n\n\n");
#endif
    SYSTEM_BEDUG("GSystem is loading");

    system_post_load();

    SYSTEM_BEDUG("GSystem loaded");

    while (1) {
        system_tick();
    }
}

extern void sys_proc_tick();
void system_tick(void)
{
    sys_proc_tick();
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

    bool need_error_timer = is_status(SYS_TICK_FAULT) || is_error(HARD_FAULT);
    if (need_error_timer) {
        __disable_irq();
    }

    if (!has_errors()) {
        error = INTERNAL_ERROR;
    }

    SYSTEM_BEDUG("GSystem_error_handler called error=%s", get_status_name(error));

#ifndef GSYSTEM_NO_SYS_TICK_W
    if (is_error(SYS_TICK_ERROR) && !system_hsi_initialized) {
        system_hsi_config();
    }
#endif

#if !defined(GSYSTEM_NO_RTC_W)
#   if defined(GSYSTEM_DS1307_CLOCK)
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
#   endif
    if (!is_clock_ready()) {
        set_clock_ready();
    }
    if (is_clock_ready()) {
        for (uint8_t i = 0; i < sizeof(error); i++) {
            set_clock_ram(i, ((uint8_t*)&error)[i]);
        }
    }
#endif

    uint32_t delay_ms = GSYSTEM_RESET_TIMEOUT_MS;
    gtimer_t timer = {0};
    system_timer_t s_timer = {0};
    if (need_error_timer) {
        system_timer_start(&s_timer, GSYSTEM_TIMER, delay_ms);
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

#if GSYSTEM_BEDUG
    system_timer_start(&s_timer, GSYSTEM_TIMER, SECOND_MS);
    SYSTEM_BEDUG("GSystem reset"); // TODO: change printf to work with registers when need_error_timer is true
    while(system_timer_wait(&s_timer));
    system_timer_stop(&s_timer);
#endif

#ifndef DEBUG
    if (is_error(POWER_ERROR)) {
        __disable_irq();
        __reset_bit(SCB->SCR, SCB_SCR_SEVONPEND_Msk);
        __set_bit(SCB->SCR, SCB_SCR_SLEEPDEEP_Msk | SCB_SCR_SLEEPONEXIT_Msk);
        __WFI();
    }
#endif

    NVIC_SystemReset();
}

void system_timer_start(system_timer_t* timer, TIM_TypeDef* fw_tim, uint32_t delay_ms)
{
    // TODO: timers after system_timer_start don't work
    memset(timer, 0, sizeof(system_timer_t));
    switch ((uint32_t)fw_tim) {
    case TIM1_BASE:
        timer->enabled = READ_BIT(RCC->APB2ENR, RCC_APB2ENR_TIM1EN);
        break;
    case TIM2_BASE:
        timer->enabled = READ_BIT(RCC->APB1ENR, RCC_APB1ENR_TIM2EN);
        break;
    case TIM3_BASE:
        timer->enabled = READ_BIT(RCC->APB1ENR, RCC_APB1ENR_TIM3EN);
        break;
    case TIM4_BASE:
        timer->enabled = READ_BIT(RCC->APB1ENR, RCC_APB1ENR_TIM4EN);
        break;
    default:
        BEDUG_ASSERT(false, "GSYSTEM TIM WAS NOT SELECTED");
        return;
    }
    if (timer->enabled) {
        memcpy((void*)&timer->bkup_tim, (void*)fw_tim, sizeof(TIM_TypeDef));
    }
    timer->tim = fw_tim;
    timer->end = delay_ms / SECOND_MS;
    memset((void*)fw_tim, 0, sizeof(TIM_TypeDef));

    uint32_t count = 0;
    switch ((uint32_t)fw_tim) {
    case TIM1_BASE:
        __TIM1_CLK_ENABLE();
        count = HAL_RCC_GetPCLK2Freq();
        break;
    case TIM2_BASE:
        __TIM2_CLK_ENABLE();
        count = HAL_RCC_GetPCLK1Freq();
        break;
    case TIM3_BASE:
        __TIM3_CLK_ENABLE();
        count = HAL_RCC_GetPCLK1Freq();
        break;
    case TIM4_BASE:
        __TIM4_CLK_ENABLE();
        count = HAL_RCC_GetPCLK1Freq();
        break;
    default:
        BEDUG_ASSERT(false, "GSYSTEM TIM WAS NOT SELECTED");
        return;
    }
    if (count) {
        count *= TIMER_FREQ_MUL;
        count /= SECOND_MS;
    }
    uint32_t presc = SECOND_MS;
    if (delay_ms < SECOND_MS) {
        timer->end = 1;
        presc      = delay_ms;
    }
    while (count > 0xFFFF) {
        presc *= 2;
        count /= 2;
    }
    // TODO: add interrupt with new VTOR
    if (presc > 0xFFFF) {
        presc = 0xFFFF;
    }
    if (presc == 1) {
        count /= TIMER_FREQ_MUL;
    }

    timer->tim->PSC  = presc - 1;
    timer->tim->ARR  = count - 1;
    timer->tim->CR1  = 0;

    timer->tim->EGR  = TIM_EGR_UG;
    timer->tim->CR1 |= TIM_CR1_UDIS;

    timer->tim->CNT  = 0;
    timer->tim->SR   = 0;
    timer->tim->CR1 &= ~(TIM_CR1_DIR);
    timer->tim->CR1 |= TIM_CR1_OPM;
    timer->tim->CR1 |= TIM_CR1_CEN;

    timer->verif = TIMER_VERIF_WORD;
}

bool system_timer_wait(system_timer_t* timer)
{
    if (!timer->tim || timer->verif != TIMER_VERIF_WORD) {
        SYSTEM_BEDUG("System timer has not initialized");
        return false;
    }
    if (timer->tim->SR & TIM_SR_CC1IF) {
        timer->count++;
        timer->tim->SR   = 0;
        timer->tim->CNT  = 0;
        timer->tim->CR1 |= TIM_CR1_CEN;
    }
    return timer->count < timer->end;
}

void system_timer_stop(system_timer_t* timer)
{
    if (!timer->tim || timer->verif != TIMER_VERIF_WORD) {
        BEDUG_ASSERT(false, "GSYSTEM TIM WAS NOT SELECTED");
        return;
    }

    timer->tim->SR &= ~(TIM_SR_UIF | TIM_SR_CC1IF);
    timer->tim->CNT = 0;

    if (timer->enabled) {
        memcpy(timer->tim, &timer->bkup_tim, sizeof(TIM_TypeDef));
    } else {
        timer->tim->CR1 &= ~(TIM_CR1_CEN);
        switch ((uint32_t)timer->tim) {
        case TIM1_BASE:
            __TIM1_CLK_DISABLE();
            break;
        case TIM2_BASE:
            __TIM2_CLK_DISABLE();
            break;
        case TIM3_BASE:
            __TIM3_CLK_DISABLE();
            break;
        case TIM4_BASE:
            __TIM4_CLK_DISABLE();
            break;
        default:
            BEDUG_ASSERT(false, "GSYSTEM TIM WAS NOT SELECTED");
            return;
        }
        memset((void*)timer->tim, 0, sizeof(TIM_TypeDef));
    }

    timer->verif = 0;
    timer->tim = NULL;
}

#ifndef GSYSTEM_NO_ADC_W
uint32_t get_system_power(void)
{
    if (!SYSTEM_ADC_VOLTAGE[0]) {
        return 0;
    }
    return (STM_ADC_MAX * STM_REF_VOLTAGEx10) / SYSTEM_ADC_VOLTAGE[0];
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
#   error "Please select your controller"
#endif

    system_hsi_initialized = true;
}
#endif

__attribute__((weak)) void system_error_loop(void) {}

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
#   warning "GSystem i2c has not selected"
#endif
}

char* get_system_serial_str(void)
{
    uint32_t uid_base = 0x1FFFF7E8;

    uint16_t *idBase0 = (uint16_t*)(uid_base);
    uint16_t *idBase1 = (uint16_t*)(uid_base + 0x02);
    uint32_t *idBase2 = (uint32_t*)(uid_base + 0x04);
    uint32_t *idBase3 = (uint32_t*)(uid_base + 0x08);

    static char str_uid[25] = {0};
    memset((void*)str_uid, 0, sizeof(str_uid));
    sprintf(str_uid, "%04X%04X%08lX%08lX", *idBase0, *idBase1, *idBase2, *idBase3);

    return str_uid;
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
    system_timer_start(&timer, GSYSTEM_TIMER, 5 * SECOND_MS);
    while (system_timer_wait(&timer));
    system_timer_stop(&timer);

    RCC->CR |= RCC_CR_HSEON;
    system_timer_start(&timer, GSYSTEM_TIMER, 5 * SECOND_MS);
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

void system_delay_us(uint32_t us)
{
    uint32_t ticks = us * (get_system_freq() / 1000000);
    uint32_t start = DWT->CYCCNT;
    while (DWT->CYCCNT - start < ticks);
}

void _system_restart_check(void)
{
    bool flag = false;
    // IWDG check reboot
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST)) {
#if GSYSTEM_BEDUG
        printTagLog(SYSTEM_TAG, "IWDG just went off");
#endif
        flag = true;
    }

    // WWDG check reboot
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST)) {
#if GSYSTEM_BEDUG
        printTagLog(SYSTEM_TAG, "WWDG just went off");
#endif
        flag = true;
    }

    if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST)) {
#if GSYSTEM_BEDUG
        printTagLog(SYSTEM_TAG, "SOFT RESET");
#endif
        flag = true;
    }

    if (flag) {
        __HAL_RCC_CLEAR_RESET_FLAGS();
#if GSYSTEM_BEDUG
        printTagLog(SYSTEM_TAG, "DEVICE HAS BEEN REBOOTED");
#endif
        HAL_Delay(2500);
    }
}

#ifndef GSYSTEM_NO_RAM_W
void _fill_ram()
{
    volatile unsigned *top, *start;
    __asm__ volatile ("mov %[top], sp" : [top] "=r" (top) : : );
    unsigned *end_heap = (unsigned*)sbrk(0);
    start = end_heap;
    start++;
    while (start < top) {
        *(start++) = SYSTEM_CANARY_WORD;
    }
}
#endif

#if !defined(GSYSTEM_NO_PRINTF) || defined(GSYSTEM_BEDUG_UART)
int _write(int line, uint8_t *ptr, int len) {
    (void)line;
    (void)ptr;
    (void)len;

#   if defined(GSYSTEM_BEDUG_UART)
    extern UART_HandleTypeDef GSYSTEM_BEDUG_UART;
    HAL_UART_Transmit(&GSYSTEM_BEDUG_UART, (uint8_t*)ptr, (uint16_t)(len), 100);
#   endif

#   if !defined(GSYSTEM_NO_PRINTF)
    for (int DataIdx = 0; DataIdx < len; DataIdx++) {
        ITM_SendChar(*ptr++);
    }
#   endif

    return len;
}
#endif
