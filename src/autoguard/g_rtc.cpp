/* Copyright © 2024 Georgy E. All rights reserved. */

#include "gdefines.h"
#include "gconfig.h"

#include "glog.h"
#include "gsystem.h"


#ifdef GSYSTEM_DS1307_CLOCK
#   define RTC_ERROR_END() system_reset_i2c_errata();     \
	                       set_error(RTC_ERROR);          \
                           return;
#else
#   define RTC_ERROR_END() set_error(RTC_ERROR);          \
                           return;
#endif

#ifndef GSYSTEM_NO_RTC_W


#   include "clock.h"


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
			printTagLog(SYSTEM_TAG, "Last reload error: %s", get_status_name(get_last_error()));
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
	printTagLog(SYSTEM_TAG, "RTC testing in progress...");
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
	printTagLog(SYSTEM_TAG, "RTC testing done");
#   endif

#   endif
}
#   undef RTC_ERROR_END
#endif
