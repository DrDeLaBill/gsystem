/* Copyright © 2025 Georgy E. All rights reserved. */

#include "gdefines.h"
#include "gconfig.h"

#include <cstring>

#include "gsystem.h"


#ifndef GSYSTEM_NO_VTOR_REWRITE


#ifdef STM32F1
#   define VECTOR_TABLE_SIZE  0x130
#   define VECTOR_TABLE_ALIGN __attribute__((aligned(0x200)))
#elif defined(STM32F4)
#   define VECTOR_TABLE_SIZE  0x194
#   define VECTOR_TABLE_ALIGN __attribute__((aligned(0x200)))
#else
#   error
#endif


extern "C" void NMI_Handler(void);
void gsys_NMI_Handler(void)
{
	set_error(NON_MASKABLE_INTERRUPT);
	NMI_Handler();
}

extern "C" void HardFault_Handler(void);
void gsys_HardFault_Handler(void)
{
	system_error_handler(HARD_FAULT);
}

extern "C" void MemManage_Handler(void);
void gsys_MemManage_Handler(void)
{
	system_error_handler(MEM_MANAGE);
}

extern "C" void BusFault_Handler(void);
void gsys_BusFault_Handler(void)
{
	system_error_handler(BUS_FAULT);
}

extern "C" void UsageFault_Handler(void);
void gsys_UsageFault_Handler(void)
{
	system_error_handler(USAGE_FAULT);
}


static uint32_t* flash_vector_table = (uint32_t*)0x08000000;
static uint32_t VECTOR_TABLE_ALIGN gsys_isr_vector[VECTOR_TABLE_SIZE / sizeof(uint32_t)] = {};


static void _change_addr(void (*original_ptr) (void), void (*target_ptr) (void))
{
	for (unsigned i = 0; i < __arr_len(gsys_isr_vector); i++) {
		if (flash_vector_table[i] == (uint32_t)original_ptr) {
			gsys_isr_vector[i] = (uint32_t)target_ptr;
			return;
		}
	}
	system_error_handler(HARD_FAULT);
}

extern "C" void sys_isr_register()
{
    if (!MCUcheck()) {
		system_error_handler(MCU_ERROR);
        while(1) {}
	}

	memcpy(gsys_isr_vector, flash_vector_table, VECTOR_TABLE_SIZE);
	_change_addr(NMI_Handler,        gsys_NMI_Handler);
	_change_addr(HardFault_Handler,  gsys_HardFault_Handler);
	_change_addr(MemManage_Handler,  gsys_MemManage_Handler);
	_change_addr(BusFault_Handler,   gsys_BusFault_Handler);
	_change_addr(UsageFault_Handler, gsys_UsageFault_Handler);

	__disable_irq();
	SCB->VTOR = (uint32_t)gsys_isr_vector;
	__enable_irq();
	__DSB();
}


#endif
