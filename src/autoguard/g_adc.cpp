/* Copyright © 2024 Georgy E. All rights reserved. */

#include "gdefines.h"
#include "gconfig.h"

#include "g_hal.h"


#ifndef GSYSTEM_NO_ADC_W

#define GSYSTEM_ADC_DELAY_MS   ((uint32_t)100)
#define GSYSTEM_ADC_TIMEOUT_MS ((uint32_t)SECOND_MS)

static gtimer_t adc_timer   = {};
static gtimer_t adc_timeout = {};
static bool adc_started     = false;


void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
	(void)hadc;
	adc_started = false;
	gtimer_start(&adc_timer, GSYSTEM_ADC_DELAY_MS);
	gtimer_start(&adc_timeout, GSYSTEM_ADC_TIMEOUT_MS);
	set_status(GSYS_ADC_READY);
}

extern "C" void adc_watchdog_check()
{
	if (!is_status(SYSTEM_SOFTWARE_STARTED)) {
		return;
	}

	if (!gtimer_wait(&adc_timeout)) {
		reset_status(GSYS_ADC_READY);
	}

	if (adc_started) {
		return;
	}

	extern ADC_HandleTypeDef hadc1;
	extern uint32_t SYSTEM_ADC_VOLTAGE[GSYSTEM_ADC_VOLTAGE_COUNT];
#   ifdef STM32F1
	HAL_ADCEx_Calibration_Start(&hadc1);
#   endif
	HAL_ADC_Start_DMA(&hadc1, (uint32_t*)SYSTEM_ADC_VOLTAGE, GSYSTEM_ADC_VOLTAGE_COUNT);

	adc_started = true;
}

#endif
