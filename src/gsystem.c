/* Copyright © 2024 Georgy E. All rights reserved. */

#include "gsystem.h"

#include <malloc.h>
#include <stdbool.h>

#include "main.h"
#include "glog.h"
#include "clock.h"
#include "bmacro.h"
#include "button.h"
#include "hal_defs.h"

#if defined(GSYSTEM_DS1307_CLOCK)
#   include "ds1307.h"
#endif


#define GSYSTEM_BEDUG (defined(DEBUG) || defined(GBEDUG_FORCE))
#if GSYSTEM_BEDUG
static const char SYSTEM_TAG[] = "GSYS";
#   define SYSTEM_BEDUG(FORMAT, ...) printTagLog(SYSTEM_TAG, FORMAT __VA_OPT__(,) __VA_ARGS__);
#else
#   define SYSTEM_BEDUG(FORMAT, ...)
#endif
#if defined(GSYSTEM_BEDUG_UART) && !GSYSTEM_BEDUG
#   undef GSYSTEM_BEDUG_UART
#endif

#define SYSTEM_WATCHDOG_MIN_DELAY_MS (20)

#if defined(GSYSTEM_DS1307_CLOCK)
#   define SYSTEM_BKUP_SIZE (DS1307_REG_RAM_END - DS1307_REG_RAM - sizeof(SYSTEM_BKUP_STATUS_TYPE))
#elif !defined(GSYSTEM_NO_RTC_W)
#   define SYSTEM_BKUP_SIZE (RTC_BKP_NUMBER - RTC_BKP_DR2 - sizeof(SYSTEM_BKUP_STATUS_TYPE))
#endif


#ifndef GSYSTEM_POCESSES_COUNT
#   define GSYSTEM_POCESSES_COUNT (32)
#endif

static void _system_watchdog_check(void);
static void _fill_ram();


static const uint32_t TIMER_VERIF_WORD = 0xBEDAC1DE;

#if GSYSTEM_BUTTONS_COUNT
unsigned buttons_count = 0;
button_t buttons[GSYSTEM_BUTTONS_COUNT] = {0};
#endif

static const uint32_t err_delay_ms = 30 * MINUTE_MS;

static bool sys_timeout_enabled = false;
static uint32_t sys_timeout_ms = 0;

#ifndef GSYSTEM_NO_SYS_TICK_W
static bool system_hsi_initialized = false;
#endif
static gtimer_t err_timer = {0};

#if defined(DEBUG)
static unsigned kCPScounter = 0;
#endif

#ifndef GSYSTEM_NO_ADC_W
uint16_t SYSTEM_ADC_VOLTAGE[GSYSTEM_ADC_VOLTAGE_COUNT] = {0};
#endif

#ifndef GSYSTEM_TIMER
#   define GSYSTEM_TIMER (TIM1)
#endif

typedef struct _watchdogs_t {
	void     (*action)(void);
	uint32_t delay_ms;
	gtimer_t timer;
} watchdogs_t;


#if !defined(GSYSTEM_NO_RESTART_W) || !defined(GSYSTEM_NO_RTC_W)
extern void restart_watchdog_check();
#endif
#ifndef GSYSTEM_NO_SYS_TICK_W
extern void sys_clock_watchdog_check();
#endif
#ifndef GSYSTEM_NO_RAM_W
extern void ram_watchdog_check();
#endif
#ifndef GSYSTEM_NO_ADC_W
extern void adc_watchdog_check();
#endif
#ifndef GSYSTEM_NO_I2C_W
extern void i2c_watchdog_check();
#endif
#if !defined(GSYSTEM_NO_POWER_W) && !defined(GSYSTEM_NO_ADC_W)
extern void power_watchdog_check();
#endif
#ifndef GSYSTEM_NO_RTC_W
extern void rtc_watchdog_check();
#endif
#ifndef GSYSTEM_NO_MEMORY_W
extern void memory_watchdog_check();
#endif
#if GSYSTEM_BUTTONS_COUNT
extern void btn_watchdog_check();
#endif
watchdogs_t watchdogs[] = {
	{_system_watchdog_check,   SYSTEM_WATCHDOG_MIN_DELAY_MS, {0,0}},
#if !defined(GSYSTEM_NO_RESTART_W) || !defined(GSYSTEM_NO_RTC_W)
	{restart_watchdog_check,   SECOND_MS / 10,               {0,0}},
#endif
#ifndef GSYSTEM_NO_SYS_TICK_W
	{sys_clock_watchdog_check, SECOND_MS / 10,               {0,0}},
#endif
#ifndef GSYSTEM_NO_RAM_W
	{ram_watchdog_check,       5 * SECOND_MS,                {0,0}},
#endif
#ifndef GSYSTEM_NO_ADC_W
	{adc_watchdog_check,       SECOND_MS / 10,               {0,0}},
#endif
#ifndef GSYSTEM_NO_I2C_W
	{i2c_watchdog_check,       5 * SECOND_MS,                {0,0}},
#endif
#ifndef GSYSTEM_NO_RTC_W
	{rtc_watchdog_check,       SECOND_MS,                    {0,0}},
#endif
#if !defined(GSYSTEM_NO_POWER_W) && !defined(GSYSTEM_NO_ADC_W)
	{power_watchdog_check,     SECOND_MS,                    {0,0}},
#endif
#ifndef GSYSTEM_NO_MEMORY_W
	{memory_watchdog_check,    SECOND_MS,                    {0,0}},
#endif
#if GSYSTEM_BUTTONS_COUNT
	{btn_watchdog_check,       5,                            {0,0}},
#endif
};


typedef struct _process_t {
	void (*action) (void);
	gtimer_t timer;
	uint32_t delay_ms;
	bool work_with_error;
} process_t;

static unsigned  processes_cnt = 0;
static process_t processes[GSYSTEM_POCESSES_COUNT] = { 0 };


void system_init(void)
{
#ifndef GSYSTEM_NO_RAM_W
	_fill_ram();
#endif

	if (!MCUcheck()) {
		set_error(MCU_ERROR);
		system_error_handler(get_first_error());
		while(1) {}
	}

#ifndef GSYSTEM_NO_SYS_TICK_W
	RCC->CR |= RCC_CR_HSEON;

	system_timer_t timer = {0};
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

	system_timer_start(&timer, GSYSTEM_TIMER, SECOND_MS);
	while (system_timer_wait(&timer));
	system_timer_stop(&timer);

	memset((uint8_t*)&processes, 0, sizeof(GSYSTEM_POCESSES_COUNT));

	gtimer_start(&err_timer, err_delay_ms);

	set_status(SYSTEM_HARDWARE_STARTED);
}

void system_post_load(void)
{
	set_status(SYSTEM_SOFTWARE_STARTED);

	SystemInfo();

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

#if defined(STM32F1) && !defined(GSYSTEM_NO_TAMPER_RESET) && !defined(GSYSTEM_NO_RTC_W)
	HAL_PWR_EnableBkUpAccess();
    MODIFY_REG(BKP->RTCCR, (BKP_RTCCR_CCO | BKP_RTCCR_ASOE | BKP_RTCCR_ASOS), RTC_OUTPUTSOURCE_NONE);
	CLEAR_BIT(BKP->CSR, BKP_CSR_TPIE);
	SET_BIT(BKP->CSR, BKP_CSR_CTI);
	SET_BIT(BKP->CSR, BKP_CSR_CTE);
	CLEAR_BIT(BKP->CR, BKP_CR_TPE);
	HAL_PWR_DisableBkUpAccess();
#endif
}

void system_registrate(void (*process) (void), uint32_t delay_ms, bool work_with_error)
{
	if (processes_cnt >= __arr_len(processes)) {
		BEDUG_ASSERT(false, "GSystem processes count is out of range");
		return;
	}
	processes[processes_cnt].action          = process;
	processes[processes_cnt].delay_ms        = delay_ms;
	processes[processes_cnt].work_with_error = work_with_error;
	processes_cnt++;
}

void set_system_timeout(uint32_t timeout_ms)
{
	sys_timeout_enabled = true;
	sys_timeout_ms      = timeout_ms;
}

void system_start(void)
{
	HAL_Delay(100);

#if GSYSTEM_BEDUG
	gprint("\n\n\n");
#endif
	SYSTEM_BEDUG("GSystem is loading");

	system_post_load();

	SYSTEM_BEDUG("GSystem loaded");

	gtimer_t err_timer = {0};
	gtimer_start(&err_timer, sys_timeout_ms);
	while (1) {
		system_tick();

		if (!gtimer_wait(&err_timer) && sys_timeout_enabled) {
			system_error_handler(has_errors() ? get_first_error() : LOAD_ERROR);
		}

		if (!is_system_ready() && sys_timeout_enabled) {
			continue;
		}

		gtimer_start(&err_timer, sys_timeout_ms);
	}
}

void system_tick()
{
	static unsigned index_w = 0;
	static unsigned index_p = 0;

#if defined(DEBUG)
	kCPScounter++;
#endif

	static bool work_w = true;
	bool tmp_work_w = work_w;
	work_w = !work_w;
	if (tmp_work_w) {
		if (!__arr_len(watchdogs)) {
			return;
		}

		if (index_w >= __arr_len(watchdogs)) {
			index_w = 0;
		}

		if (is_status(SYS_TICK_FAULT) || !gtimer_wait(&watchdogs[index_w].timer)) {
			gtimer_start(&watchdogs[index_w].timer, watchdogs[index_w].delay_ms);
			watchdogs[index_w].action();
		}

		index_w++;
	} else {
		if (!processes_cnt) {
			return;
		}

		if (index_p >= processes_cnt) {
			index_p = 0;
		}

		if (!gtimer_wait(&processes[index_p].timer) &&
			(
				!is_status(SYSTEM_ERROR_HANDLER_CALLED) ||
				(
					is_status(SYSTEM_ERROR_HANDLER_CALLED) &&
					processes[index_p].work_with_error
				)
			) &&
			processes[index_p].action
		) {
			gtimer_start(&processes[index_p].timer, processes[index_p].delay_ms);
			processes[index_p].action();
		}

		index_p++;
	}
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

	if (!has_errors()) {
		error = INTERNAL_ERROR;
	}

	SYSTEM_BEDUG("GSystem_error_handler called error=%s", get_status_name(error));

#ifndef GSYSTEM_NO_SYS_TICK_W
	if (is_error(SYS_TICK_ERROR) && !system_hsi_initialized) {
		system_hsi_config();
	}
#endif

#if !defined(GSYSTEM_NO_RTC_W) && defined(GSYSTEM_DS1307_CLOCK)
	if (!is_clock_started()) {
		SYSTEM_CLOCK_I2C.Instance = I2C1;
		SYSTEM_CLOCK_I2C.Init.ClockSpeed = 100000;
		SYSTEM_CLOCK_I2C.Init.DutyCycle = I2C_DUTYCYCLE_2;
		SYSTEM_CLOCK_I2C.Init.OwnAddress1 = 0;
		SYSTEM_CLOCK_I2C.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
		SYSTEM_CLOCK_I2C.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
		SYSTEM_CLOCK_I2C.Init.OwnAddress2 = 0;
		SYSTEM_CLOCK_I2C.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
		SYSTEM_CLOCK_I2C.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
		if (HAL_I2C_Init(&SYSTEM_CLOCK_I2C) != HAL_OK) {
			set_error(I2C_ERROR);
		}
		clock_begin();
	}
	if (!is_clock_ready()) {
		set_clock_ready();
	}

	if (is_clock_ready()) {
		set_clock_ram(0, (uint32_t)error);
	}
#endif

	const uint32_t delay_ms = 30 * SECOND_MS;
	gtimer_t timer = {0};
	system_timer_t s_timer = {0};
	bool need_error_timer = is_status(SYS_TICK_FAULT) || is_error(HARD_FAULT);
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
	system_timer_start(&s_timer, GSYSTEM_TIMER, 300);
	SYSTEM_BEDUG("GSystem reset");
	while(system_timer_wait(&s_timer));
	system_timer_stop(&s_timer);
#endif

	NVIC_SystemReset();
}

void system_timer_start(system_timer_t* timer, TIM_TypeDef* fw_tim, uint32_t delay_ms)
{
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

	switch ((uint32_t)fw_tim) {
	case TIM1_BASE:
		__TIM1_CLK_ENABLE();
		break;
	case TIM2_BASE:
		__TIM2_CLK_ENABLE();
		break;
	case TIM3_BASE:
		__TIM3_CLK_ENABLE();
		break;
	case TIM4_BASE:
		__TIM4_CLK_ENABLE();
		break;
	default:
		BEDUG_ASSERT(false, "GSYSTEM TIM WAS NOT SELECTED");
		return;
	}
	uint32_t count_cnt = SECOND_MS;
	if (delay_ms < SECOND_MS) {
		timer->end = 1;
		count_cnt  = delay_ms;
	}
	uint32_t presc = HAL_RCC_GetSysClockFreq() / SECOND_MS;
	while (presc > 0xFFFF) {
		count_cnt *= 2;
		presc     /= 2;
	}
	// TODO: add interrupt with new VTOR
	if (count_cnt > 0xFFFF) {
		count_cnt = 0xFFFF;
	}
	timer->tim->SR   = 0;
	timer->tim->PSC  = presc - 1;
	timer->tim->ARR  = count_cnt - 1;
	timer->tim->CNT  = 0;
	timer->tim->CR1 &= ~(TIM_CR1_DIR); // count up
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
	}

	timer->verif = 0;
	timer->tim = NULL;
}

#if GSYSTEM_BUTTONS_COUNT
void system_add_button(GPIO_TypeDef* port, uint16_t pin, bool inverse)
{
	if (buttons_count >= __arr_len(buttons)) {
		return;
	}
	util_port_pin_t tmp_pin = {port, pin};
	button_create(&buttons[buttons_count++], &tmp_pin, inverse, DEFAULT_HOLD_TIME_MS);
}

button_t* _find_button(GPIO_TypeDef* port, uint16_t pin)
{
	button_t* btn = NULL;
	for (unsigned i = 0; i < buttons_count; i++) {
		if (buttons[i]._pin.port == port && buttons[i]._pin.pin == pin) {
			btn = &buttons[i];
			break;
		}
	}
	return btn;
}

bool system_button_clicked(GPIO_TypeDef* port, uint16_t pin)
{
	button_t* btn = _find_button(port, pin);
	if (!btn) {
		return false;
	}
	return button_one_click(btn);
}

bool system_button_pressed(GPIO_TypeDef* port, uint16_t pin)
{
	button_t* btn = _find_button(port, pin);
	if (!btn) {
		return false;
	}
	return button_pressed(btn);
}

bool system_button_holded(GPIO_TypeDef* port, uint16_t pin)
{
	button_t* btn = _find_button(port, pin);
	if (!btn) {
		return false;
	}
	return button_holded(btn);
}
#endif

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
#elif !defined(GSYSTEM_NO_I2C_W)
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
bool get_system_rtc_ram(const uint8_t idx, uint8_t* data)
{
	if (idx + sizeof(SYSTEM_BKUP_STATUS_TYPE) >= SYSTEM_BKUP_SIZE) {
		return false;
	}
	return get_clock_ram(idx + sizeof(SYSTEM_BKUP_STATUS_TYPE), data);
}

bool set_system_rtc_ram(const uint8_t idx, const uint8_t data)
{
	if (idx + sizeof(SYSTEM_BKUP_STATUS_TYPE) >= SYSTEM_BKUP_SIZE) {
		return false;
	}
	return set_clock_ram(idx + sizeof(SYSTEM_BKUP_STATUS_TYPE), data);
}
#endif

void _system_watchdog_check(void)
{
#if GSYSTEM_BEDUG
	static gtimer_t kCPSTimer = {0,(10 * SECOND_MS)};
#endif

	if (!gtimer_wait(&err_timer)) {
		system_error_handler(
			get_first_error() != NO_ERROR ? get_first_error() : INTERNAL_ERROR
		);
	}
	if (!has_errors()) {
		gtimer_start(&err_timer, err_delay_ms);
	}

#if GSYSTEM_BEDUG
	if (!gtimer_wait(&kCPSTimer)) {
		printTagLog(
			SYSTEM_TAG,
			"kCPS: %lu.%lu",
			kCPScounter / (10 * SECOND_MS),
			(kCPScounter / SECOND_MS) % 10
		);
#   ifndef GSYSTEM_NO_ADC_W
		uint32_t power = get_system_power();
		printTagLog(
			SYSTEM_TAG,
			"Power: %lu.%lu V",
			power / 10,
			power % 10
		);
#   endif
		kCPScounter = 0;
		gtimer_start(&kCPSTimer, (10 * SECOND_MS));
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

void _fill_ram()
{
	volatile unsigned *top, *start;
	__asm__ volatile ("mov %[top], sp" : [top] "=r" (top) : : );
	unsigned *end_heap = (unsigned*)malloc(sizeof(size_t));
	start = end_heap;
	free(end_heap);
	start++;
	while (start < top) {
		*(start++) = SYSTEM_CANARY_WORD;
	}
}

#ifndef GSYSTEM_NO_PRINTF
int _write(int line, uint8_t *ptr, int len) {
	(void)line;
	(void)ptr;
	(void)len;

#   if GSYSTEM_BEDUG_UART
    extern UART_HandleTypeDef GSYSTEM_BEDUG_UART;
    HAL_UART_Transmit(&GSYSTEM_BEDUG_UART, (uint8_t*)ptr, (uint16_t)(len), 100);
#   endif

    for (int DataIdx = 0; DataIdx < len; DataIdx++) {
        ITM_SendChar(*ptr++);
    }
    return len;

    return 0;
}
#endif
