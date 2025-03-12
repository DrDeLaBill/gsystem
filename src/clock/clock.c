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
#include "hal_defs.h"


#if defined(GSYSTEM_DS1307_CLOCK)
#   include "ds1307.h"
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
#if defined(GSYSTEM_DS1307_CLOCK)
	clock_started = DS1307_Init() == DS1307_OK;
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
#if defined(GSYSTEM_DS1307_CLOCK)
	uint16_t year = 0;
	if (DS1307_GetYear(&year) != DS1307_OK) {
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
#if defined(GSYSTEM_DS1307_CLOCK)
	uint8_t month = 0;
	if (DS1307_GetMonth(&month) != DS1307_OK) {
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
#if defined(GSYSTEM_DS1307_CLOCK)
	uint8_t date = 0;
	if (DS1307_GetDate(&date) != DS1307_OK) {
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
#if defined(GSYSTEM_DS1307_CLOCK)
	uint8_t hour = 0;
	if (DS1307_GetHour(&hour) != DS1307_OK) {
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
#if defined(GSYSTEM_DS1307_CLOCK)
	uint8_t minute = 0;
	if (DS1307_GetMinute(&minute) != DS1307_OK) {
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
#if defined(GSYSTEM_DS1307_CLOCK)
	uint8_t second = 0;
	if (DS1307_GetSecond(&second) != DS1307_OK) {
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
#if defined(GSYSTEM_DS1307_CLOCK)
	if (DS1307_SetHour(time.Hours) != DS1307_OK) {
		return false;
	}
	if (DS1307_SetMinute(time.Minutes) != DS1307_OK) {
		return false;
	}
	if (DS1307_SetSecond(time.Seconds) != DS1307_OK) {
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
#if defined(GSYSTEM_DS1307_CLOCK)
	if (DS1307_SetYear((uint16_t)date.Year) != DS1307_OK) {
		return false;
	}
	if (DS1307_SetMonth(date.Month) != DS1307_OK) {
		return false;
	}
	if (DS1307_SetDate(date.Date) != DS1307_OK) {
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
#if defined(GSYSTEM_DS1307_CLOCK)
	if (DS1307_GetHour(&time->Hours) != DS1307_OK) {
		return false;
	}
	if (DS1307_GetMinute(&time->Minutes) != DS1307_OK) {
		return false;
	}
	if (DS1307_GetSecond(&time->Seconds) != DS1307_OK) {
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
#if defined(GSYSTEM_DS1307_CLOCK)
	if (DS1307_GetYear(&date->Year) != DS1307_OK) {
		return false;
	}
	if (DS1307_GetMonth(&date->Month) != DS1307_OK) {
		return false;
	}
	if (DS1307_GetDate(&date->Date) != DS1307_OK) {
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

#if !defined(GSYSTEM_DS1307_CLOCK)
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
		"%u-%02u-%02uT%02u:%02u:%02u",
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
#if defined(GSYSTEM_DS1307_CLOCK)
	bool need_erase = !is_clock_ready();
	printTagLog("CLCK", "Update clock %s", (need_erase ? "(erase)" : ""));
	uint32_t value = BEDAC0DE;
	for (uint8_t i = 0; i < sizeof(BEDAC0DE); i++) {
		if (DS1307_SetRegByte(
				(uint8_t)DS1307_REG_RAM_BEGIN + i,
				((uint8_t*)&value)[i]
			) != DS1307_OK
		) {
			return false;
		}
	}
	if (!need_erase) {
		return true;
	}
	for (
		unsigned i = DS1307_REG_RAM_BEGIN + sizeof(BEDAC0DE);
		i <= DS1307_REG_RAM_END;
		i++
	)  {
		if (DS1307_SetRegByte((uint8_t)i, 0xFF) != DS1307_OK) {
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
#if defined(GSYSTEM_DS1307_CLOCK)
	uint32_t value = 0;
	for (uint8_t i = 0; i < sizeof(BEDAC0DE); i++) {
		if (DS1307_GetRegByte(
				(uint8_t)DS1307_REG_RAM_BEGIN + i,
				&((uint8_t*)&value)[i]
			) != DS1307_OK
		) {
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
#if defined(GSYSTEM_DS1307_CLOCK)
	if (DS1307_REG_RAM_BEGIN + sizeof(BEDAC0DE) + idx > DS1307_REG_RAM_END) {
		return false;
	}
	return DS1307_GetRegByte(DS1307_REG_RAM_BEGIN + sizeof(BEDAC0DE) + idx, data) == DS1307_OK;
#else
	if (RTC_BKP_DR3 + (idx / 2U) > RTC_BKP_NUMBER) {
		return false;
	}
	HAL_PWR_EnableBkUpAccess();
	uint32_t value = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR3 + idx / 2U);
	HAL_PWR_DisableBkUpAccess();
	*data = ((uint8_t*)&value)[idx % 2U];
	return true;
#endif
}

bool set_clock_ram(const uint8_t idx, uint8_t data)
{
#if defined(GSYSTEM_DS1307_CLOCK)
	if (DS1307_REG_RAM_BEGIN + sizeof(BEDAC0DE) + idx > DS1307_REG_RAM_END) {
		return false;
	}
	return DS1307_SetRegByte(DS1307_REG_RAM_BEGIN + sizeof(BEDAC0DE) + idx, data) == DS1307_OK;
#else
	if (RTC_BKP_DR3 + (idx / 4U) > RTC_BKP_NUMBER) {
		return false;
	}

	HAL_PWR_EnableBkUpAccess();
	uint32_t value = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR3 + idx / 2U);
	((uint8_t*)&value)[idx % 2U] = data;
	HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR3 + idx / 2U, value);
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
