/* Copyright © 2024 Georgy E. All rights reserved. */

#include "gdefines.h"
#include "gconfig.h"

#include "hal_defs.h"


#ifndef GSYSTEM_NO_ADC_W

#   define SYSTEM_ADC_DELAY_MS ((uint32_t)100)

static gtimer_t adc_timer = {};
static bool adc_started = false;


void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
	(void)hadc;
	adc_started = false;
	gtimer_start(&adc_timer, SYSTEM_ADC_DELAY_MS);
}

extern "C" void adc_watchdog_check()
{
	if (!is_status(SYSTEM_SOFTWARE_STARTED)) {
		return;
	}

	if (gtimer_wait(&adc_timer)) {
		return;
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
