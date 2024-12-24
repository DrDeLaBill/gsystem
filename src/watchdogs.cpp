/* Copyright © 2024 Georgy E. All rights reserved. */

#include <cmath>
#include <cstdint>

#include "soul.h"
#include "glog.h"
#include "clock.h"
#include "gsystem.h"
#include "hal_defs.h"

#ifndef GSYSTEM_NO_MEMORY_W
#   include "StorageAT.h"
#   include "StorageDriver.h"
#endif

#if GSYSTEM_BUTTONS_COUNT > 0
#   include "button.h"
#endif


#ifndef GSYSTEM_ADC_W
#   define SYSTEM_ADC_DELAY_MS ((uint32_t)100)
#endif


#if GSYSTEM_BEDUG || defined(DEBUG) || defined(GBEDUG_FORCE)
static const char TAG[] = "GSYS";
#endif

#ifndef GSYSTEM_NO_ADC_W
static gtimer_t adc_timer = {};
static bool adc_started = false;
#endif

#ifndef GSYSTEM_NO_MEMORY_W
StorageDriver storageDriver;
StorageAT storage(
#   if defined(GSYSTEM_EEPROM_MODE)
	EEPROM_PAGES_COUNT,
#   elif defined(GSYSTEM_FLASH_MODE)
	0,
#   else
#       error "Memory mode is not selected"
	0,
#   endif

	&storageDriver,

#   if defined(GSYSTEM_EEPROM_MODE)
	EEPROM_PAGE_SIZE
#   elif defined(GSYSTEM_FLASH_MODE)
	FLASH_W25_SECTOR_SIZE
#   else
	0
#   endif
);
#endif


#ifndef GSYSTEM_NO_SYS_TICK_W
extern "C" void sys_clock_watchdog_check()
{
	if (!is_error(SYS_TICK_ERROR) && !is_status(SYS_TICK_FAULT)) {
		return;
	}

	system_sys_tick_reanimation();
}
#endif

#ifndef GSYSTEM_NO_RAM_W
extern "C" void ram_watchdog_check()
{
	static const unsigned STACK_PERCENT_MIN = 5;
	static unsigned lastFree = 0;

	unsigned *start, *end;
	__asm__ volatile ("mov %[end], sp" : [end] "=r" (end) : : );
	unsigned *end_heap = (unsigned*)malloc(sizeof(size_t));
	start = end_heap;
	free(end_heap);
	start++;

	unsigned heap_end = 0;
	unsigned stack_end = 0;
	unsigned last_counter = 0;
	unsigned cur_counter = 0;
	for (;start < end; start++) {
		if ((*start) == SYSTEM_CANARY_WORD) {
			cur_counter++;
		}
		if (cur_counter && (*start) != SYSTEM_CANARY_WORD) {
			if (last_counter < cur_counter) {
				last_counter = cur_counter;
				heap_end     = (unsigned)start - cur_counter;
				stack_end    = (unsigned)start;
			}

			cur_counter = 0;
		}
	}

	extern unsigned _sdata;
	extern unsigned _estack;
	uint32_t freeRamBytes = last_counter * sizeof(SYSTEM_CANARY_WORD);
	unsigned freePercent = (unsigned)__percent(
		(uint32_t)last_counter,
		(uint32_t)__abs_dif(&_sdata, &_estack)
	);
#if GSYSTEM_BEDUG
	if (freeRamBytes && __abs_dif(lastFree, freeRamBytes)) {
		printTagLog(TAG, "-----ATTENTION! INDIRECT DATA BEGIN:-----");
		printTagLog(TAG, "RAM:              [0x%08X->0x%08X]", (unsigned)&_sdata, (unsigned)&_estack);
		printTagLog(TAG, "RAM occupied MAX: %u bytes", (unsigned)(__abs_dif((unsigned)&_sdata, (unsigned)&_estack) - freeRamBytes));
		printTagLog(TAG, "RAM free  MIN:    %u bytes (%u%%) [0x%08X->0x%08X]", (unsigned)freeRamBytes, freePercent, (unsigned)(stack_end - freeRamBytes), (unsigned)stack_end);
		printTagLog(TAG, "------ATTENTION! INDIRECT DATA END-------");
	}
#endif

	if (freeRamBytes) {
		lastFree = freeRamBytes;
	}

	if (freeRamBytes && lastFree && heap_end < stack_end && freePercent > STACK_PERCENT_MIN) {
		reset_error(STACK_ERROR);
	} else {
#if GSYSTEM_BEDUG
		BEDUG_ASSERT(
			is_error(STACK_ERROR),
			"STACK OVERFLOW"
		);
#endif
		set_error(STACK_ERROR);
	}
}
#endif

#ifndef GSYSTEM_NO_RTC_W

#ifdef GSYSTEM_DS1307_CLOCK
#   define RTC_ERROR_END() system_reset_i2c_errata();     \
	                       set_error(RTC_ERROR);          \
                           return;
#else
#   define RTC_ERROR_END() set_error(RTC_ERROR);          \
                           return;
#endif

extern "C" void rtc_watchdog_check()
{
	static bool system_error_loaded = false;
	static bool start_timer_flag = false;
	static gtimer_t timer = {};
#   ifndef GSYSTEM_NO_RTC_CALENDAR_W
	static bool tested = false;
#   endif

	if (!is_system_ready() && !is_error(RTC_ERROR)) {
		return;
	}

	if (!start_timer_flag) {
		gtimer_start(&timer, 15 * SECOND_MS);
		start_timer_flag = true;
	}

	if (!is_clock_started()) {
		clock_begin();
	}
	if (is_clock_started() && !system_error_loaded) {
		SYSTEM_BKUP_STATUS_TYPE status = 0;
		for (uint8_t i = 0; i < sizeof(status); i++) {
			uint8_t data = 0;
			if (!get_clock_ram(i, &data)) {
				status = 0;
				break;
			}
			((uint8_t*)&status)[i] = data;
		}
		set_last_error((SOUL_STATUS)status);
		set_clock_ram(0, 0);
		system_error_loaded = true;

#   if GSYSTEM_BEDUG
		if (get_last_error()) {
			printTagLog(TAG, "Last reload error: %s", get_status_name(get_last_error()));
		}
#   endif
	}

	if (!is_clock_started()) {
		if (!gtimer_wait(&timer)) {
			set_error(RTC_ERROR);
		}
		return;
	}

	if (!is_status(RTC_READY)) {
		if (is_clock_ready()) {
#   ifndef GSYSTEM_NO_RTC_CALENDAR_W
			tested = false;
#   endif
			set_status(RTC_READY);
		} else {
			reset_status(RTC_READY);
		}
	}

#   ifndef GSYSTEM_NO_RTC_CALENDAR_W
	if (is_error(RTC_ERROR)) {
		tested = false;
	}

	if (tested) {
		return;
	}

#   if GSYSTEM_BEDUG
	printTagLog(TAG, "RTC testing in progress...");
#   endif

	clock_date_t dumpDate = {0,0,0,0};
	clock_time_t dumpTime = {0,0,0};
	uint64_t dumpMs       = getMillis();

#   if GSYSTEM_BEDUG
	printPretty("Dump date test: ");
#   endif
	if (!get_clock_rtc_date(&dumpDate)) {
#   if GSYSTEM_BEDUG
		gprint("  error\n");
#   endif
		RTC_ERROR_END();
	}
#   if GSYSTEM_BEDUG
	gprint("  OK\n");
	printPretty("Dump time test: ");
#   endif
	if (!get_clock_rtc_time(&dumpTime)) {
#   if GSYSTEM_BEDUG
		gprint("   error\n");
#   endif
		RTC_ERROR_END();
	}
#   if GSYSTEM_BEDUG
	gprint("  OK\n");
#   endif


#   if defined(GSYSTEM_DS1307_CLOCK)
	clock_date_t saveDate = {0, 04, 28, 24};
#   else
	clock_date_t saveDate = {RTC_WEEKDAY_SUNDAY, 04, 28, 24};
#   endif
	clock_time_t saveTime = {13,37,00};

#   if GSYSTEM_BEDUG
	printPretty("Save date test: ");
#   endif
   	if (!save_clock_date(&saveDate)) {
#   if GSYSTEM_BEDUG
		gprint("  error\n");
#   endif
		RTC_ERROR_END();
	}
#   if GSYSTEM_BEDUG
	gprint("  OK\n");
	printPretty("Save time test: ");
#   endif
	if (!save_clock_time(&saveTime)) {
#   if GSYSTEM_BEDUG
		gprint("  error\n");
#   endif
		RTC_ERROR_END();
	}
#   if GSYSTEM_BEDUG
	gprint("  OK\n");
#   endif


	clock_date_t checkDate = {0,0,0,0};
	clock_time_t checkTime = {0,0,0};
#   if GSYSTEM_BEDUG
	printPretty("Check date test: ");
#   endif
	if (!get_clock_rtc_date(&checkDate)) {
#   if GSYSTEM_BEDUG
		gprint(" error\n");
#   endif
		RTC_ERROR_END();
	}
	if (!is_same_date(&saveDate, &checkDate)) {
#   if GSYSTEM_BEDUG
		gprint(" error\n");
#   endif
		RTC_ERROR_END();
	}
#   if GSYSTEM_BEDUG
	gprint(" OK\n");
	printPretty("Check time test: ");
#   endif
	if (!get_clock_rtc_time(&checkTime)) {
#   if GSYSTEM_BEDUG
		gprint(" error\n");
#   endif
		RTC_ERROR_END();
	}
	if (!is_same_time(&saveTime, &checkTime)) {
#   if GSYSTEM_BEDUG
		gprint(" error\n");
#   endif
		RTC_ERROR_END();
	}
#   if GSYSTEM_BEDUG
	gprint(" OK\n");
#   endif

	uint64_t res_seconds = get_clock_datetime_to_seconds(&dumpDate, &dumpTime);
	res_seconds += ((getMillis() - dumpMs) / SECOND_MS);
	get_clock_seconds_to_datetime(res_seconds, &dumpDate, &dumpTime);
#   if GSYSTEM_BEDUG
	printPretty("Dump date save: ");
#   endif
	if (!save_clock_date(&dumpDate)) {
#   if GSYSTEM_BEDUG
		gprint("  error\n");
#   endif
		RTC_ERROR_END();
	}
#   if GSYSTEM_BEDUG
	gprint("  OK\n");
	printPretty("Dump time save: ");
#   endif
	if (!save_clock_time(&dumpTime)) {
#   if GSYSTEM_BEDUG
		gprint("  error\n");
#   endif
		RTC_ERROR_END();
	}
#   if GSYSTEM_BEDUG
	gprint("  OK\n");
#   endif

#   if GSYSTEM_BEDUG
	printPretty("Check dump date: ");
#   endif
	if (!get_clock_rtc_date(&checkDate)) {
#   if GSYSTEM_BEDUG
		gprint(" error\n");
#   endif
		RTC_ERROR_END();
	}
	if (!is_same_date(&dumpDate, &checkDate)) {
#   if GSYSTEM_BEDUG
		gprint(" error\n");
#   endif
		RTC_ERROR_END();
	}
#   if GSYSTEM_BEDUG
	gprint(" OK\n");
 	printPretty("Check dump time: ");
#   endif
	if (!get_clock_rtc_time(&checkTime)) {
#   if GSYSTEM_BEDUG
		gprint(" error\n");
#   endif
		RTC_ERROR_END();
	}
	if (!is_same_time(&dumpTime, &checkTime)) {
#   if GSYSTEM_BEDUG
		gprint(" error\n");
#   endif
		RTC_ERROR_END();
	}
#   if GSYSTEM_BEDUG
	gprint(" OK\n");
#   endif


#   if GSYSTEM_BEDUG
	printPretty("Weekday test\n");
#   endif
	const clock_date_t dates[] = {
#   if defined(GSYSTEM_DS1307_CLOCK)
		{0, 01, 01, 00},
		{0, 01, 02, 00},
		{0, 04, 27, 24},
		{0, 04, 28, 24},
		{0, 04, 29, 24},
		{0, 04, 30, 24},
		{0, 05, 01, 24},
		{0, 05, 02, 24},
		{0, 05, 03, 24},
#   else
		{RTC_WEEKDAY_SATURDAY,  01, 01, 00},
		{RTC_WEEKDAY_SUNDAY,    01, 02, 00},
		{RTC_WEEKDAY_SATURDAY,  04, 27, 24},
		{RTC_WEEKDAY_SUNDAY,    04, 28, 24},
		{RTC_WEEKDAY_MONDAY,    04, 29, 24},
		{RTC_WEEKDAY_TUESDAY,   04, 30, 24},
		{RTC_WEEKDAY_WEDNESDAY, 05, 01, 24},
		{RTC_WEEKDAY_THURSDAY,  05, 02, 24},
		{RTC_WEEKDAY_FRIDAY,    05, 03, 24},
#   endif
	};
	const clock_time_t times[] = {
		{00, 00, 00},
		{00, 00, 00},
		{03, 24, 49},
		{04, 14, 24},
		{03, 27, 01},
		{23, 01, 40},
		{03, 01, 40},
		{04, 26, 12},
		{03, 52, 35},
	};
	const uint64_t seconds[] = {
		0,
		86400,
		767503489,
		767592864,
		767676421,
		767833300,
		767847700,
		767939172,
		768023555,
	};

	for (unsigned i = 0; i < __arr_len(seconds); i++) {
#   if GSYSTEM_BEDUG
		printPretty("[%02u]: ", i);
#   endif

		clock_date_t tmpDate = {0,0,0,0};
		clock_time_t tmpTime = {0,0,0};
		get_clock_seconds_to_datetime(seconds[i], &tmpDate, &tmpTime);
		if (!is_same_date(&tmpDate, &dates[i])
#   if !defined(GSYSTEM_DS1307_CLOCK)
			&& tmpDate.WeekDay == dates[i].WeekDay
#   endif
		) {
#   if GSYSTEM_BEDUG
			gprint("            error\n");
#   endif
			RTC_ERROR_END();
		}
		if (!is_same_time(&tmpTime, &times[i])) {
#   if GSYSTEM_BEDUG
			gprint("            error\n");
#   endif
			RTC_ERROR_END();
		}

		uint64_t tmpSeconds = get_clock_datetime_to_seconds(&dates[i], &times[i]);
		if (tmpSeconds != seconds[i]) {
#   if GSYSTEM_BEDUG
			gprint("            error\n");
#   endif
			RTC_ERROR_END();
		}

#   if GSYSTEM_BEDUG
		gprint("            OK\n");
#   endif
	}

	reset_error(RTC_ERROR);
	tested = true;


#   if GSYSTEM_BEDUG
	printTagLog(TAG, "RTC testing done");
#   endif

#   endif
}
#   undef RTC_ERROR_END
#endif

#ifndef GSYSTEM_NO_ADC_W
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
#ifdef STM32F1
	HAL_ADCEx_Calibration_Start(&hadc1);
#endif
	HAL_ADC_Start_DMA(&hadc1, (uint32_t*)SYSTEM_ADC_VOLTAGE, GSYSTEM_ADC_VOLTAGE_COUNT);

	adc_started = true;
}
#endif

#ifndef GSYSTEM_NO_MEMORY_W
extern "C" void memory_watchdog_check()
{
	static const uint32_t TIMEOUT_MS = 15000;
	static const unsigned ERRORS_MAX = 5;

	static utl::Timer errorTimer(TIMEOUT_MS);
	static utl::Timer timer(SECOND_MS);
	static uint8_t errors = 0;
	static bool timerStarted = false;

	if (!is_system_ready() && !is_error(MEMORY_ERROR) && !is_error(EXPECTED_MEMORY_ERROR)) {
		return;
	}

	uint8_t data = 0;
#ifdef GSYSTEM_EEPROM_MODE
	eeprom_status_t status = EEPROM_OK;
#else
	flash_status_t status = FLASH_OK;
#endif

#ifndef GSYSTEM_EEPROM_MODE
	if (!is_status(MEMORY_INITIALIZED)) {
		if (flash_w25qxx_init() == FLASH_OK) {
			set_status(MEMORY_INITIALIZED);
			storage.setPagesCount(flash_w25qxx_get_pages_count());
#   ifdef GSYSTEM_BEDUG
			printTagLog(TAG, "flash init success (%lu pages)", flash_w25qxx_get_pages_count());
#   endif
		} else {
#   ifdef GSYSTEM_BEDUG
			printTagLog(TAG, "flash init error");
#   endif
		}
		return;
	}
#else
	set_status(MEMORY_INITIALIZED);
#endif

	if (is_status(MEMORY_READ_FAULT) ||
		is_status(MEMORY_WRITE_FAULT) ||
		is_error(MEMORY_ERROR) ||
		is_error(EXPECTED_MEMORY_ERROR)
	) {
#ifdef GSYSTEM_EEPROM_MODE
		system_reset_i2c_errata();

		uint32_t address = static_cast<uint32_t>(rand()) % eeprom_get_size();

		status = eeprom_read(address, &data, sizeof(data));
		if (status == EEPROM_OK) {
			reset_status(MEMORY_READ_FAULT);
			status = eeprom_write(address, &data, sizeof(data));
		} else {
			errors++;
		}
		if (status == EEPROM_OK) {
			reset_status(MEMORY_WRITE_FAULT);
			timerStarted = false;
			errors = 0;
		} else {
			errors++;
		}
#elif defined(GSYSTEM_FLASH_MODE)
		if (is_status(MEMORY_INITIALIZED) && flash_w25qxx_init() != FLASH_OK) {
			reset_status(MEMORY_INITIALIZED);
		}

		uint32_t address = static_cast<uint32_t>(rand()) % (flash_w25qxx_get_pages_count() * FLASH_W25_PAGE_SIZE);

		status = flash_w25qxx_read(address, &data, sizeof(data));
		if (status == FLASH_OK) {
			reset_status(MEMORY_READ_FAULT);
			status = flash_w25qxx_write(address, &data, sizeof(data));
		} else {
			errors++;
		}
		if (status == FLASH_OK) {
			reset_status(MEMORY_WRITE_FAULT);
			reset_error(EXPECTED_MEMORY_ERROR);
			timerStarted = false;
			errors = 0;
		} else {
			errors++;
		}
#endif
	}

	(errors > ERRORS_MAX) ? set_error(MEMORY_ERROR) : reset_error(MEMORY_ERROR);

	if (!timerStarted && is_error(MEMORY_ERROR)) {
		timerStarted = true;
		errorTimer.start();
	}

	if (timerStarted && !errorTimer.wait()) {
		system_error_handler(MEMORY_ERROR);
	}
}
#endif

#if !defined(GSYSTEM_NO_POWER_W) && !defined(GSYSTEM_NO_ADC_W)
extern "C" void power_watchdog_check()
{
	if (!is_system_ready()) {
		return;
	}

	uint32_t voltage = get_system_power();

	if (STM_MIN_VOLTAGEx10 <= voltage && voltage <= STM_MAX_VOLTAGEx10) {
		reset_error(POWER_ERROR);
	} else {
		set_error(POWER_ERROR);
	}
}
#endif

#ifndef GSYSTEM_NO_RESTART_W
extern "C" void restart_watchdog_check()
{
	static bool flagsCleared = false;

	if (flagsCleared) {
		return;
	}

	bool flag = false;
	// IWDG check reboot
	if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST)) {
#if GSYSTEM_BEDUG
		printTagLog(TAG, "IWDG just went off");
#endif
		flag = true;
	}

	// WWDG check reboot
	if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST)) {
#if GSYSTEM_BEDUG
		printTagLog(TAG, "WWDG just went off");
#endif
		flag = true;
	}

	if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST)) {
#if GSYSTEM_BEDUG
		printTagLog(TAG, "SOFT RESET");
#endif
		flag = true;
	}

	if (flag) {
		__HAL_RCC_CLEAR_RESET_FLAGS();
#if GSYSTEM_BEDUG
		printTagLog(TAG, "DEVICE HAS BEEN REBOOTED");
#endif
//		system_reset_i2c_errata(); // TODO
		HAL_Delay(2500);
	}
}
#endif

#ifndef GSYSTEM_NO_I2C_W
extern "C" void i2c_watchdog_check()
{
	if (!is_error(I2C_ERROR)) {
		return;
	}

	system_reset_i2c_errata();
}
#endif

#if GSYSTEM_BUTTONS_COUNT
extern "C" void btn_watchdog_check()
{
	extern unsigned buttons_count;
	extern button_t buttons[GSYSTEM_BUTTONS_COUNT];

	if (!buttons_count) {
		return;
	}

	static unsigned counter = 0;
	if (counter >= buttons_count) {
		counter = 0;
	}

	button_tick(&buttons[counter++]);
}
#endif
