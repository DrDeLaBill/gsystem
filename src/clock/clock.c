/* Copyright © 2023 Georgy E. All rights reserved. */

#include "clock.h"


#include "gconfig.h"
#include "gdefines.h"


#ifndef GSYSTEM_NO_RTC_W


#include <clock.h>
#include <stdint.h>
#include <stdbool.h>

#include "glog.h"
#include "bmacro.h"
#include "drivers.h"


#if defined(GSYSTEM_DS130X_CLOCK)
#   include "ds130x.h"
#else
extern RTC_HandleTypeDef hrtc;
#endif


typedef enum _Months {
	JANUARY = 0,
	FEBRUARY,
	MARCH,
	APRIL,
	MAY,
	JUNE,
	JULY,
	AUGUST,
	SEPTEMBER,
	OCTOBER,
	NOVEMBER,
	DECEMBER
} Months;


static const uint32_t BEDAC0DE = 0xBEDAC0DE;

static bool clock_started = false;


uint8_t _get_days_in_month(uint16_t year, Months month);


void clock_begin()
{
#if defined(GSYSTEM_DS130X_CLOCK)
	clock_started = DS130X_Init() == DS130X_OK;
#else
	clock_started = true;
#endif
}

bool is_clock_started()
{
	return clock_started;
}

uint16_t get_clock_year()
{
#if defined(GSYSTEM_DS130X_CLOCK)
	uint16_t year = 0;
	if (DS130X_GetYear(&year) != DS130X_OK) {
		year = 0;
	}
	return year;
#else
	RTC_DateTypeDef date;
	if (HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BIN) != HAL_OK)
	{
		return 0;
	}
	return date.Year;
#endif
}

uint8_t get_clock_month()
{
#if defined(GSYSTEM_DS130X_CLOCK)
	uint8_t month = 0;
	if (DS130X_GetMonth(&month) != DS130X_OK) {
		month = 0;
	}
	return month;
#else
    RTC_DateTypeDef date;
    if (HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BIN) != HAL_OK)
    {
        return 0;
    }
    return date.Month;
#endif
}

uint8_t get_clock_date()
{
#if defined(GSYSTEM_DS130X_CLOCK)
	uint8_t date = 0;
	if (DS130X_GetDate(&date) != DS130X_OK) {
		date = 0;
	}
	return date;
#else
    RTC_DateTypeDef date;
    if (HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BIN) != HAL_OK)
    {
        return 0;
    }
    return date.Date;
#endif
}

uint8_t get_clock_hour()
{
#if defined(GSYSTEM_DS130X_CLOCK)
	uint8_t hour = 0;
	if (DS130X_GetHour(&hour) != DS130X_OK) {
		hour = 0;
	}
	return hour;
#else
    RTC_TimeTypeDef time;
    if (HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN) != HAL_OK)
    {
        return 0;
    }
    return time.Hours;
#endif
}

uint8_t get_clock_minute()
{
#if defined(GSYSTEM_DS130X_CLOCK)
	uint8_t minute = 0;
	if (DS130X_GetMinute(&minute) != DS130X_OK) {
		minute = 0;
	}
	return minute;
#else
    RTC_TimeTypeDef time;
    if (HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN) != HAL_OK)
    {
        return 0;
    }
    return time.Minutes;
#endif
}

uint8_t get_clock_second()
{
#if defined(GSYSTEM_DS130X_CLOCK)
	uint8_t second = 0;
	if (DS130X_GetSecond(&second) != DS130X_OK) {
		second = 0;
	}
	return second;
#else
    RTC_TimeTypeDef time;
    if (HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN) != HAL_OK)
    {
        return 0;
    }
    return time.Seconds;
#endif
}

bool save_clock_time(const clock_time_t* save_time)
{
	clock_time_t time = {0};
	memcpy((uint8_t*)&time, (uint8_t*)save_time, sizeof(time));
    if (time.Seconds >= SECONDS_PER_MINUTE) {
    	time.Seconds = 0;
    }
    if (time.Minutes >= MINUTES_PER_HOUR) {
    	time.Minutes = 0;
    }
    if (time.Hours   >= HOURS_PER_DAY) {
    	time.Hours   = 0;
    }
#if defined(GSYSTEM_DS130X_CLOCK)
	if (DS130X_SetHour(time.Hours) != DS130X_OK) {
		return false;
	}
	if (DS130X_SetMinute(time.Minutes) != DS130X_OK) {
		return false;
	}
	if (DS130X_SetSecond(time.Seconds) != DS130X_OK) {
		return false;
	}

#   if CLOCK_BEDUG
	printTagLog(
		TAG,
		"clock_save_time: time=%02u:%02u:%02u",
		time.Hours,
		time.Minutes,
		time.Seconds
	);
#   endif

	return true;
#else
    HAL_StatusTypeDef status = HAL_ERROR;
    RTC_TimeTypeDef tmpTime = {0};
    tmpTime.Hours   = time.Hours;
    tmpTime.Minutes = time.Minutes;
    tmpTime.Seconds = time.Seconds;

	HAL_PWR_EnableBkUpAccess();
	status = HAL_RTC_SetTime(&hrtc, &tmpTime, RTC_FORMAT_BIN);
	HAL_PWR_DisableBkUpAccess();

	BEDUG_ASSERT(status == HAL_OK, "Unable to set current time");
#   if CLOCK_BEDUG
	printTagLog(
		TAG,
		"clock_save_time: time=%02u:%02u:%02u",
		tmpTime.Hours,
		tmpTime.Minutes,
		tmpTime.Seconds
	);
#   endif
    return status == HAL_OK;
#endif
}

bool save_clock_date(const clock_date_t* save_date)
{
	clock_date_t date = {0};
	memcpy((uint8_t*)&date, (uint8_t*)save_date, sizeof(date));
	if (!save_date->Date || !save_date->Month) {
		BEDUG_ASSERT(false, "Bad date");
		return false;
	}
	if (date.Date > DAYS_PER_MONTH_MAX) {
		date.Date = DAYS_PER_MONTH_MAX;
	}
	if (date.Month > MONTHS_PER_YEAR) {
		date.Month = MONTHS_PER_YEAR;
	}
#if defined(GSYSTEM_DS130X_CLOCK)
	if (DS130X_SetYear((uint16_t)date.Year) != DS130X_OK) {
		return false;
	}
	if (DS130X_SetMonth(date.Month) != DS130X_OK) {
		return false;
	}
	if (DS130X_SetDate(date.Date) != DS130X_OK) {
		return false;
	}
	return true;
#else
	RTC_DateTypeDef saveDate = {0};
	clock_time_t tmpTime = {0};
	clock_date_t tmpDate = {0};
    uint64_t seconds = 0;
    HAL_StatusTypeDef status = HAL_ERROR;

	/* calculating weekday begin */
	seconds = get_clock_datetime_to_seconds(&tmpDate, &tmpTime);
	get_clock_seconds_to_datetime(seconds, &tmpDate, &tmpTime);
	saveDate.WeekDay = tmpDate.WeekDay;
	if (!saveDate.WeekDay) {
		BEDUG_ASSERT(false, "Error calculating clock weekday");
		saveDate.WeekDay = RTC_WEEKDAY_MONDAY;
	}
	/* calculating weekday end */

	saveDate.Date  = date.Date;
    saveDate.Month = date.Month;
    saveDate.Year  = (uint8_t)(date.Year & 0xFF);
	HAL_PWR_EnableBkUpAccess();
	status = HAL_RTC_SetDate(&hrtc, &saveDate, RTC_FORMAT_BIN);
	HAL_PWR_DisableBkUpAccess();

	BEDUG_ASSERT(status == HAL_OK, "Unable to set current date");
#   if CLOCK_BEDUG
		printTagLog(
			TAG,
			"clock_save_date: seconds=%lu, time=20%02u-%02u-%02u weekday=%u",
			seconds,
			saveDate.Year,
			saveDate.Month,
			saveDate.Date,
			saveDate.WeekDay
		);
#   endif
    return status == HAL_OK;
#endif
}

bool get_clock_rtc_time(clock_time_t* time)
{
#if defined(GSYSTEM_DS130X_CLOCK)
	if (DS130X_GetHour(&time->Hours) != DS130X_OK) {
		return false;
	}
	if (DS130X_GetMinute(&time->Minutes) != DS130X_OK) {
		return false;
	}
	if (DS130X_GetSecond(&time->Seconds) != DS130X_OK) {
		return false;
	}
	return true;
#else
	RTC_TimeTypeDef tmpTime = {0};
	if (HAL_RTC_GetTime(&hrtc, &tmpTime, RTC_FORMAT_BIN) != HAL_OK) {
		return false;
	}
	time->Hours   = tmpTime.Hours;
	time->Minutes = tmpTime.Minutes;
	time->Seconds = tmpTime.Seconds;
	return true;
#endif
}

bool get_clock_rtc_date(clock_date_t* date)
{
#if defined(GSYSTEM_DS130X_CLOCK)
	if (DS130X_GetYear(&date->Year) != DS130X_OK) {
		return false;
	}
	if (DS130X_GetMonth(&date->Month) != DS130X_OK) {
		return false;
	}
	if (DS130X_GetDate(&date->Date) != DS130X_OK) {
		return false;
	}
	return true;
#else
	RTC_DateTypeDef tmpDate = {0};
	if (HAL_RTC_GetDate(&hrtc, &tmpDate, RTC_FORMAT_BIN) != HAL_OK) {
		return false;
	}
	date->Date    = tmpDate.Date;
	date->Month   = tmpDate.Month;
	date->Year    = tmpDate.Year;
	date->WeekDay = tmpDate.WeekDay;
	return true;
#endif
}


uint64_t get_clock_datetime_to_seconds(const clock_date_t* date, const clock_time_t* time)
{
	uint16_t year = date->Year % 100;
	uint32_t days = year * DAYS_PER_YEAR;
	if (year > 0) {
		days += (uint32_t)((year - 1) / LEAP_YEAR_PERIOD) + 1;
	}
	for (unsigned i = 0; i < (unsigned)(date->Month > 0 ? date->Month - 1 : 0); i++) {
		days += _get_days_in_month(year, i);
	}
	days += date->Date;
	days -= 1;
	uint64_t hours   = days * HOURS_PER_DAY + time->Hours;
	uint64_t minutes = hours * MINUTES_PER_HOUR + time->Minutes;
	uint64_t seconds = minutes * SECONDS_PER_MINUTE + time->Seconds;
	return seconds;
}

uint64_t get_clock_timestamp()
{
	clock_date_t date = {0};
	clock_time_t time = {0};

	if (!get_clock_rtc_date(&date)) {
#if CLOCK_BEDUG
		BEDUG_ASSERT(false, "Unable to get current date");
#endif
		memset((void*)&date, 0, sizeof(date));
	}

	if (!get_clock_rtc_time(&time)) {
#if CLOCK_BEDUG
		BEDUG_ASSERT(false, "Unable to get current time");
#endif
		memset((void*)&time, 0, sizeof(time));
	}

	return get_clock_datetime_to_seconds(&date, &time);
}

void get_clock_seconds_to_datetime(const uint64_t seconds, clock_date_t* date, clock_time_t* time)
{
	memset(date, 0, sizeof(clock_date_t));
	memset(time, 0, sizeof(clock_time_t));

	time->Seconds = (uint8_t)(seconds % SECONDS_PER_MINUTE);
	uint64_t minutes = seconds / SECONDS_PER_MINUTE;

	time->Minutes = (uint8_t)(minutes % MINUTES_PER_HOUR);
	uint64_t hours = minutes / MINUTES_PER_HOUR;

	time->Hours = (uint8_t)(hours % HOURS_PER_DAY);
	uint64_t days = 1 + hours / HOURS_PER_DAY;

#if !defined(GSYSTEM_DS130X_CLOCK)
	date->WeekDay = (uint8_t)((RTC_WEEKDAY_THURSDAY + days) % (DAYS_PER_WEEK)) + 1;
	if (date->WeekDay == DAYS_PER_WEEK) {
		date->WeekDay = 0;
	}
#endif
	date->Month = 1;
	while (days) {
		uint16_t days_in_year = (date->Year % LEAP_YEAR_PERIOD > 0) ? DAYS_PER_YEAR : DAYS_PER_LEAP_YEAR;
		if (days > days_in_year) {
			days -= days_in_year;
			date->Year++;
			continue;
		}

		uint8_t days_in_month = _get_days_in_month(date->Year, date->Month - 1);
		if (days > days_in_month) {
			days -= days_in_month;
			date->Month++;
			continue;
		}

		date->Date = (uint8_t)days;
		break;
	}
}

char* get_clock_time_format()
{
	static char format_time[30] = "";
	memset(format_time, '-', sizeof(format_time) - 1);
	format_time[sizeof(format_time) - 1] = 0;

	clock_date_t date = {0};
	clock_time_t time = {0};

	if (!get_clock_rtc_date(&date)) {
#if CLOCK_BEDUG
		BEDUG_ASSERT(false, "Unable to get current date");
#endif
		memset((void*)&date, 0, sizeof(date));
		return format_time;
	}

	if (!get_clock_rtc_time(&time)) {
#if CLOCK_BEDUG
		BEDUG_ASSERT(false, "Unable to get current time");
#endif
		memset((void*)&time, 0, sizeof(time));
		return format_time;
	}

	snprintf(
		format_time,
		sizeof(format_time) - 1,
#ifdef GSYSTEM_DS1302_CLOCK
		"20%02u-%02u-%02uT%02u:%02u:%02u",
#else
		"%u-%02u-%02uT%02u:%02u:%02u",
#endif
		date.Year,
		date.Month,
		date.Date,
		time.Hours,
		time.Minutes,
		time.Seconds
	);

	return format_time;
}

char* get_clock_time_format_by_sec(uint64_t seconds)
{
	static char format_time[30] = "";
	memset(format_time, '-', sizeof(format_time) - 1);
	format_time[sizeof(format_time) - 1] = 0;

	clock_date_t date = {0};
	clock_time_t time = {0};

	get_clock_seconds_to_datetime(seconds, &date, &time);

	snprintf(
		format_time,
		sizeof(format_time) - 1,
		"20%02u-%02u-%02uT%02u:%02u:%02u",
		date.Year,
		date.Month,
		date.Date,
		time.Hours,
		time.Minutes,
		time.Seconds
	);

	return format_time;
}

bool set_clock_ready()
{
#if defined(GSYSTEM_DS130X_CLOCK)
	bool need_erase = !is_clock_ready();
	printTagLog("CLCK", "Update clock %s", (need_erase ? "(erase)" : ""));
	uint32_t value = BEDAC0DE;
	for (uint8_t i = 0; i < sizeof(BEDAC0DE); i++) {
		if (DS130X_SetRAM(i, ((uint8_t*)&value)[i]) != DS130X_OK) {
			return false;
		}
	}
	if (!need_erase) {
		return true;
	}
#ifdef GSYSTEM_DS1307_CLOCK
	uint32_t end = DS130X_REG_RAM_END - DS130X_REG_RAM_BEGIN;
#elif defined(GSYSTEM_DS1302_CLOCK)
	uint32_t end = (DS130X_REG_RAM_END - DS130X_REG_RAM_BEGIN) / 2;
#endif
	for (uint8_t i = sizeof(BEDAC0DE); i <= end; i++) {
		if (DS130X_SetRAM(i, 0xFF) != DS130X_OK) {
			return false;
		}
	}
	return true;
#else
	HAL_PWR_EnableBkUpAccess();
	HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR1, (uint16_t)BEDAC0DE);
	HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR2, (uint16_t)(BEDAC0DE >> 16U));
	HAL_PWR_DisableBkUpAccess();
	return is_clock_ready();
#endif
}

bool is_clock_ready()
{
#if defined(GSYSTEM_DS130X_CLOCK)
	uint32_t value = 0;
	for (uint8_t i = 0; i < sizeof(BEDAC0DE); i++) {
		if (DS130X_GetRAM(i, &((uint8_t*)&value)[i]) != DS130X_OK) {
			return false;
		}
	}
	return value == BEDAC0DE;
#else
	HAL_PWR_EnableBkUpAccess();
	uint32_t value =
		HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR1) |
		HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR2) << 16U;
	HAL_PWR_DisableBkUpAccess();
	return value == BEDAC0DE;
#endif
}

bool get_clock_ram(const uint8_t idx, uint8_t* data)
{
#if defined(GSYSTEM_DS130X_CLOCK)
	return DS130X_GetRAM(sizeof(BEDAC0DE) + idx, data) == DS130X_OK;
#else
	if (RTC_BKP_DR3 + (idx / STM_BCKP_REG_SIZE) > RTC_BKP_NUMBER) {
		return false;
	}
	HAL_PWR_EnableBkUpAccess();
	uint32_t value = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR3 + idx / STM_BCKP_REG_SIZE);
	HAL_PWR_DisableBkUpAccess();
	*data = ((uint8_t*)&value)[idx % STM_BCKP_REG_SIZE];
	return true;
#endif
}

bool set_clock_ram(const uint8_t idx, uint8_t data)
{
#if defined(GSYSTEM_DS130X_CLOCK)
	return DS130X_SetRAM(sizeof(BEDAC0DE) + idx, data) == DS130X_OK;
#else
	if (RTC_BKP_DR3 + (idx / STM_BCKP_REG_SIZE) > RTC_BKP_NUMBER) {
		return false;
	}

	HAL_PWR_EnableBkUpAccess();
	uint32_t value = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR3 + idx / STM_BCKP_REG_SIZE);
	((uint8_t*)&value)[idx % STM_BCKP_REG_SIZE] = data;
	HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR3 + idx / STM_BCKP_REG_SIZE, value);
	HAL_PWR_DisableBkUpAccess();

	uint8_t check = 0;
	if (!get_clock_ram(idx, &check)) {
		return false;
	}
	return check == data;
#endif
}

bool is_same_date(const clock_date_t* date1, const clock_date_t* date2)
{
	return (date1->Date  == date2->Date &&
			date1->Month == date2->Month &&
			date1->Year  == date2->Year);
}

bool is_same_time(const clock_time_t* time1, const clock_time_t* time2)
{
	return (time1->Hours   == time2->Hours &&
			time1->Minutes == time2->Minutes &&
			time1->Seconds == time2->Seconds);
}

uint8_t _get_days_in_month(uint16_t year, Months month)
{
	switch (month) {
	case JANUARY:
		return 31;
	case FEBRUARY:
		return ((year % 4 == 0) ? 29 : 28);
	case MARCH:
		return 31;
	case APRIL:
		return 30;
	case MAY:
		return 31;
	case JUNE:
		return 30;
	case JULY:
		return 31;
	case AUGUST:
		return 31;
	case SEPTEMBER:
		return 30;
	case OCTOBER:
		return 31;
	case NOVEMBER:
		return 30;
	case DECEMBER:
		return 31;
	default:
		break;
	};
	return 0;
}


#endif
