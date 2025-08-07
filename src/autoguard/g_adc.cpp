/* Copyright © 2024 Georgy E. All rights reserved. */

#include "gdefines.h"
#include "gconfig.h"

#include <cstring>

#include "drivers.h"


#ifndef GSYSTEM_NO_ADC_W

#define GSYSTEM_ADC_DELAY_MS   ((uint32_t)100)
#define GSYSTEM_ADC_TIMEOUT_MS ((uint32_t)SECOND_MS)
#define GSYSTEM_ADC_ERROR_MS   ((uint32_t)50) // TODO: 10 ms needed

extern ADC_HandleTypeDef hadc1;
extern uint16_t SYSTEM_ADC_VOLTAGE[GSYSTEM_ADC_VOLTAGE_COUNT];

static uint16_t adc_buff[__arr_len(SYSTEM_ADC_VOLTAGE)] = {};
static gtimer_t adc_timer   = {};
static gtimer_t adc_timeout = {};
static gtimer_t error       = {};
static bool adc_started     = false;
static bool adc_error       = false;


void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef*)
{
	adc_started = false;
	gtimer_start(&adc_timer, GSYSTEM_ADC_DELAY_MS);
	gtimer_start(&adc_timeout, GSYSTEM_ADC_TIMEOUT_MS);
	if (adc_buff[0]) {
		adc_error = false;
		set_status(GSYS_ADC_READY);
	} else if (!gtimer_wait(&error)) {
		adc_error = true;
		gtimer_start(&error, GSYSTEM_ADC_ERROR_MS);
	}
}

void HAL_ADC_ErrorCallback(ADC_HandleTypeDef*)
{
	adc_started = false;
	adc_error = true;
	gtimer_start(&error, GSYSTEM_ADC_ERROR_MS);
}

extern "C" void adc_watchdog_check()
{
	if (!is_status(SYSTEM_SOFTWARE_STARTED)) {
		return;
	}

	if (!gtimer_wait(&adc_timeout)) {
		reset_status(GSYS_ADC_READY);
	}

	if (adc_error && !gtimer_wait(&error) && !adc_buff[0]) {
		SYSTEM_ADC_VOLTAGE[0] = 0;
	}

	if (adc_started) {
		return;
	}

	if (adc_buff[0]) {
		memcpy((uint8_t*)SYSTEM_ADC_VOLTAGE, (uint8_t*)adc_buff, sizeof(adc_buff));
	}
#   ifdef STM32F1
	HAL_ADCEx_Calibration_Start(&hadc1);
#   endif
	if (HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_buff, __arr_len(adc_buff)) == HAL_OK) {
		adc_started = true;
	}
}

#endif
