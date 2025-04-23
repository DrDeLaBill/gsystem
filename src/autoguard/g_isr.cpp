/* Copyright © 2025 Georgy E. All rights reserved. */

#include "gdefines.h"
#include "gconfig.h"

#include <cstring>

#include "gsystem.h"

#include "g_hal.h"


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

#if defined(GSYSTEM_FLASH_MODE) && defined(GSYSTEM_MEMORY_DMA) && !defined(GSYSTEM_NO_STORAGE_AT)
#   define USE_FLASH_DMA
#elif defined(GSYSTEM_EEPROM_MODE) && defined(GSYSTEM_MEMORY_DMA) && !defined(GSYSTEM_NO_STORAGE_AT)
#   define USE_EEPROM_DMA
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

#if defined(USE_FLASH_DMA) || defiend(USE_EEPROM_DMA)
#   ifndef GSYSTEM_NO_STORAGE_AT
#       include "StorageAT.h"
#   endif
#endif

#if defined(USE_FLASH_DMA)

#   include "w25qxx.h"

#   if defined(STM32F1)
#       error
#   elif defined(STM32F4)

#       if GSYSTEM_FLASH_SPI == hspi1
#           if GSYSTEM_MEMORY_STREAM_RX == 0

// SPI1 RX
#define DMA_RX_FUNC DMA2_Stream0_IRQHandler

#           endif
#           if GSYSTEM_MEMORY_STREAM_RX == 2

// SPI1 RX
#define DMA_RX_FUNC DMA2_Stream2_IRQHandler

#           endif
#           if GSYSTEM_MEMORY_STREAM_TX == 3

// SPI1 TX
#define DMA_TX_FUNC DMA2_Stream3_IRQHandler

#           endif
#           if GSYSTEM_MEMORY_STREAM_RX == 5

// SPI1 TX
#define DMA_TX_FUNC DMA2_Stream5_IRQHandler

#           endif
#       elif GSYSTEM_FLASH_SPI == hspi2
#           if GSYSTEM_MEMORY_STREAM_RX == 3

// SPI2 RX
#define DMA_RX_FUNC DMA1_Stream3_IRQHandler

#           endif
#           if GSYSTEM_MEMORY_STREAM_RX == 4

// SPI2 TX
#define DMA_TX_FUNC DMA1_Stream4_IRQHandler

#           endif
#       elif GSYSTEM_FLASH_SPI == hspi3
#           if GSYSTEM_MEMORY_STREAM_RX == 0

// SPI3 RX
#define DMA_RX_FUNC DMA1_Stream0_IRQHandler

#           endif
#           if GSYSTEM_MEMORY_STREAM_RX == 2

// SPI3 RX
#define DMA_RX_FUNC DMA1_Stream2_IRQHandler

#           endif
#           if GSYSTEM_MEMORY_STREAM_RX == 5

// SPI3 TX
#define DMA_TX_FUNC DMA1_Stream5_IRQHandler

#           endif
#           if GSYSTEM_MEMORY_STREAM_RX == 7

// SPI3 TX
#define DMA_TX_FUNC DMA1_Stream7_IRQHandler

#           endif
#        endif
#   endif
#   if defined(DMA_RX_FUNC) && defined(DMA_TX_FUNC)

extern SPI_HandleTypeDef GSYSTEM_FLASH_SPI;
#       ifndef GSYSTEM_NO_STORAGE_AT
extern StorageAT storage;
#       endif

extern "C" void DMA_RX_FUNC(void);
void __concat(gsys_, DMA_RX_FUNC)(void)
{
	uint32_t isr = ((uint32_t*)GSYSTEM_FLASH_SPI.hdmarx->StreamBaseAddress)[0];
	if (isr & (DMA_FLAG_TCIF0_4 << GSYSTEM_FLASH_SPI.hdmarx->StreamIndex)) {
		w25qxx_rx_dma_callback();
	} else {
		w25qxx_error_dma_callback();
	}
	DMA_RX_FUNC();
}

extern "C" void DMA_TX_FUNC(void);
void __concat(gsys_, DMA_TX_FUNC)(void)
{
	uint32_t isr = ((uint32_t*)GSYSTEM_FLASH_SPI.hdmatx->StreamBaseAddress)[0];
	if (isr & (DMA_FLAG_TCIF0_4 << GSYSTEM_FLASH_SPI.hdmatx->StreamIndex)) {
		w25qxx_rx_dma_callback();
	} else {
		w25qxx_error_dma_callback();
	}
	DMA_TX_FUNC();
}

#   else
#       error Memory DMA configuration error
#   endif
#elif defined(USE_EEPROM_DMA)
#   error EEPROM DMA is not ready
#endif


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
#ifdef USE_FLASH_DMA
	_change_addr(DMA_RX_FUNC,        __concat(gsys_, DMA_RX_FUNC));
	_change_addr(DMA_TX_FUNC,        __concat(gsys_, DMA_TX_FUNC));
#endif

	__disable_irq();
	SCB->VTOR = (uint32_t)gsys_isr_vector;
	__enable_irq();
	__DSB();
}


#endif
