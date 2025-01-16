/* Copyright © 2024 Georgy E. All rights reserved. */

#include "gconfig.h"

#include <unistd.h>
#include <cstdint>

#include "glog.h"
#include "gsystem.h"


#ifndef GSYSTEM_NO_RAM_W
extern "C" void ram_watchdog_check()
{
	static const unsigned STACK_PERCENT_MIN = 5;
	static unsigned lastFree = 0;

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

	extern unsigned _sdata;
	extern unsigned _estack;
	uint32_t freeRamBytes = last_counter * sizeof(SYSTEM_CANARY_WORD);
	unsigned freePercent = (unsigned)__percent(
		(uint32_t)last_counter,
		(uint32_t)__abs_dif(&_sdata, &_estack)
	);
#if GSYSTEM_BEDUG
	if (freeRamBytes && __abs_dif(lastFree, freeRamBytes)) {
		printTagLog(SYSTEM_TAG, "-----ATTENTION! INDIRECT DATA BEGIN:-----");
		printTagLog(SYSTEM_TAG, "RAM:              [0x%08X->0x%08X]", (unsigned)&_sdata, (unsigned)&_estack);
		printTagLog(SYSTEM_TAG, "RAM occupied MAX: %u bytes", (unsigned)(__abs_dif((unsigned)&_sdata, (unsigned)&_estack) - freeRamBytes));
		printTagLog(SYSTEM_TAG, "RAM free  MIN:    %u bytes (%u%%) [0x%08X->0x%08X]", (unsigned)freeRamBytes, freePercent, (unsigned)(stack_end - freeRamBytes), (unsigned)stack_end);
		printTagLog(SYSTEM_TAG, "------ATTENTION! INDIRECT DATA END-------");
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
