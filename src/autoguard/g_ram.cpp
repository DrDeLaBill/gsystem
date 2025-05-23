/* Copyright © 2024 Georgy E. All rights reserved. */

#include "gdefines.h"
#include "gconfig.h"

#include <limits>
#include <cstdint>
#include <unistd.h>

#include "glog.h"
#include "gsystem.h"


#ifndef GSYSTEM_NO_RAM_W
extern "C" void ram_watchdog_check()
{
	static const unsigned STACK_PERCENT_MIN = 5;
	static unsigned lastFree = std::numeric_limits<unsigned>::max();

	unsigned *start, *end;
	__asm__ volatile ("mov %[end], sp" : [end] "=r" (end) : : );
	unsigned *end_heap = (unsigned*)sbrk(0);
	start = end_heap;
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
	
	uint32_t freeRamBytes = last_counter * sizeof(SYSTEM_CANARY_WORD);
	unsigned freePercent = (unsigned)__percent(
		(uint32_t)last_counter,
		(uint32_t)__abs_dif(g_heap_start(), g_stack_end())
	);
#if GSYSTEM_BEDUG
	if (__abs_dif(lastFree, freeRamBytes)) {
		printTagLog(SYSTEM_TAG, "-----ATTENTION! INDIRECT DATA BEGIN:-----");
		printTagLog(SYSTEM_TAG, "RAM:              [0x%08X->0x%08X]", (unsigned)g_heap_start(), (unsigned)g_stack_end());
		printTagLog(SYSTEM_TAG, "RAM occupied MAX: %u bytes", (unsigned)(__abs_dif((unsigned)g_heap_start(), (unsigned)g_stack_end()) - freeRamBytes));
		printTagLog(SYSTEM_TAG, "RAM free  MIN:    %u bytes (%u%%) [0x%08X->0x%08X]", (unsigned)freeRamBytes, freePercent, (unsigned)(stack_end - freeRamBytes), (unsigned)stack_end);
		printTagLog(SYSTEM_TAG, "------ATTENTION! INDIRECT DATA END-------");
	}
#endif

	lastFree = freeRamBytes;

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

extern "C" void sys_fill_ram()
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
#else

extern "C" void ram_watchdog_check() {}

extern "C" void sys_fill_ram() {}

#endif
