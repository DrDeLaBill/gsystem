/*
 * @file g_ram.cpp
 * @brief RAM/stack/heap inspection helpers and watchdog.
 *
 * Provides reporting and basic monitoring of runtime memory usage. The
 * watchdog integrates with the system error flags when low-RAM conditions
 * are detected. Helper functions that build human-readable reports are
 * documented in this file.
 */

#include "gdefines.h"
#include "gconfig.h"

#include <cstdint>
#include <cstdio>
#include <inttypes.h>
#include <cstring>

#include "glog.h"
#include "drivers.h"
#include "gsystem.h"

#ifndef GSYSTEM_NO_RAM_W


static void make_progress_bar(char *buf, size_t buflen, uint32_t free_bytes, uint32_t total_bytes, const int WIDTH = 30);
static void uart_print_ram_report(
	uintptr_t heap_addr,
	uintptr_t stack_addr,
	uint32_t free_bytes,
	uint32_t total_bytes,
	uint32_t threshold_warn_percent = 5 // TODO: move to gconfig.h
);


extern "C" void ram_watchdog_check()
{
    static const unsigned STACK_PERCENT_MIN = 5;
    static unsigned lastFree = ~0U;

    uint32_t freeRamBytes = g_ram_measure_free();
    
	uintptr_t ram_start = (uintptr_t)g_ram_start();
	uintptr_t ram_end   = (uintptr_t)g_ram_end();
    uint32_t totalBytes = (uint32_t)(ram_end - ram_start);
    unsigned freePercent = (totalBytes == 0) ? 0 : (unsigned)((freeRamBytes * 100U) / totalBytes);

    if (lastFree != freeRamBytes) {
        uart_print_ram_report(ram_start, ram_end, freeRamBytes, totalBytes);
    }

    lastFree = freeRamBytes;

	uintptr_t heap_addr = (uintptr_t)g_heap_start();
	uintptr_t stack_addr = (uintptr_t)g_stack_end();
	if (freeRamBytes && lastFree && heap_addr < stack_addr && freePercent > STACK_PERCENT_MIN) {
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

extern "C" void sys_fill_ram()
{
    g_ram_fill();
}

void make_progress_bar(char *buf, size_t buflen, uint32_t free_bytes, uint32_t total_bytes, const int WIDTH)
{
	(void)buf;
	(void)buflen;
	(void)free_bytes;
	(void)total_bytes;
	(void)WIDTH;
#if GSYSTEM_BEDUG
    if (buflen == 0) return;
    if (total_bytes == 0) {
        buf[0] = '\0';
        return;
    }
    uint32_t filled = __proportion(free_bytes, 0, total_bytes, 0, (uint32_t)WIDTH);
    if (filled > (uint32_t)WIDTH) {
		filled = WIDTH;
	} else if (filled == 0 && free_bytes > 0) {
		filled = 1;
	}
    char s[WIDTH * 3 + 1] = "";
	int used = (int)(WIDTH - filled);
    for (int i = 0; i < WIDTH; ++i) {
		const char* ch = "\xE2\x96\x91"; // '░'
        if (i < used) {
			ch = "\xE2\x96\x92"; // '▓'
		}
		strcat(s, ch);
        if ((int)strlen(s) >= (int)buflen - 4) {
			break;
		}
    }
    strncpy(buf, s, buflen - 1);
    buf[buflen - 1] = '\0';
#endif
}

void uart_print_ram_report(
	uintptr_t heap_addr,
	uintptr_t stack_addr,
	uint32_t free_bytes,
	uint32_t total_bytes,
	uint32_t threshold_warn_percent
) {
	(void)heap_addr;
	(void)stack_addr;
	(void)free_bytes;
	(void)total_bytes;
	(void)threshold_warn_percent;
#if GSYSTEM_BEDUG
    uint32_t used = (total_bytes > free_bytes) ? (total_bytes - free_bytes) : 0;
    uint32_t pct_x10 = (total_bytes == 0) ? 0 : (uint32_t)((uint64_t)free_bytes * 1000ULL / (uint64_t)total_bytes);

    const char *color = "\x1b[32m"; // green
    if (pct_x10 <= threshold_warn_percent * 10 / 2) {
        color = "\x1b[31m"; // red
    } else if (pct_x10 <= threshold_warn_percent * 10) {
        color = "\x1b[33m"; // yellow
    }
	gprint("%s", color);
    SYSTEM_BEDUG(
		"[RAM] 0x%08" PRIXPTR "..0x%08" PRIXPTR,
		(uintptr_t)heap_addr, 
		(uintptr_t)stack_addr
	);
	SYSTEM_BEDUG(
		"[RAM] Total: %uB Used: %uB Free: %uB (%u.%u%%)",
		(unsigned)total_bytes,
		(unsigned)used,
		(unsigned)free_bytes,
		(unsigned)(pct_x10/10), (unsigned)(pct_x10%10)
	);

    if (pct_x10 <= threshold_warn_percent * 10) {
        SYSTEM_BEDUG(
			"[RAM] WARNING: Low free memory (<%u%%)!",
			(unsigned)threshold_warn_percent
		);
    }

	char barbuf[128] = "";
	make_progress_bar(barbuf, sizeof(barbuf), free_bytes, total_bytes);
	const char *cfree = (pct_x10 <= threshold_warn_percent * 10 / 2) ? "\x1b[31m" : ((pct_x10 <= threshold_warn_percent * 10) ? "\x1b[33m" : "\x1b[32m");
	SYSTEM_BEDUG(
		"%s[RAM] %s %sFree: %uB\x1b[0m", 
		"\x1b[36m",
		barbuf,
		cfree,
		(unsigned)free_bytes
	);
#endif
}

#else

extern "C" void ram_watchdog_check() {}

extern "C" void sys_fill_ram() {}

#endif
