/* Copyright © 2025 Georgy E. All rights reserved. */

#include "gdefines.h"
#include "gconfig.h"

#include <cstring>

#include "gsystem.h"

#include "drivers.h"


#ifndef GSYSTEM_NO_VTOR_REWRITE


#if defined(STM32F1)
    #define VECTOR_TABLE_SIZE  0x130
    #define VECTOR_TABLE_ALIGN __attribute__((aligned(0x200)))
#elif defined(STM32F4)
    #define VECTOR_TABLE_SIZE  0x194
    #define VECTOR_TABLE_ALIGN __attribute__((aligned(0x200)))
#elif defined(NRF52)
    #define VECTOR_TABLE_SIZE  0x200
    #define VECTOR_TABLE_ALIGN __attribute__((aligned(0x200)))
#else
    #error "Do it better"
#endif


#if !defined(USE_HAL_DRIVER) && !defined(ARDUINO)
    #error "ISR registration is only supported for HAL-based projects or Arduino."
#endif


#if defined(GSYSTEM_FLASH_MODE) && defined(GSYSTEM_MEMORY_DMA) && !defined(GSYSTEM_NO_STORAGE_AT)
    #define USE_FLASH_DMA
#elif defined(GSYSTEM_EEPROM_MODE) && defined(GSYSTEM_MEMORY_DMA) && !defined(GSYSTEM_NO_STORAGE_AT)
    #define USE_EEPROM_DMA
#endif


extern "C" void NMI_Handler(void);
extern "C" void HardFault_Handler(void);
extern "C" void MemManage_Handler(void);
extern "C" void BusFault_Handler(void);
extern "C" void UsageFault_Handler(void);


extern "C" void __attribute__((naked)) gsys_NMI_Handler(void)
{
	set_error(NON_MASKABLE_INTERRUPT);
	NMI_Handler();
}

extern "C" void __attribute__((naked)) gsys_HardFault_Handler(void)
{
	system_error_handler(HARD_FAULT);
}

extern "C" void __attribute__((naked)) gsys_MemManage_Handler(void)
{
	system_error_handler(MEM_MANAGE);
}

extern "C" void __attribute__((naked)) gsys_BusFault_Handler(void)
{
	system_error_handler(BUS_FAULT);
}

extern "C" void __attribute__((naked)) gsys_UsageFault_Handler(void)
{
	system_error_handler(USAGE_FAULT);
}


#if defined(ARDUINO) || defined(NRF52)

void NMI_Handler(void)
{
    gsys_NMI_Handler();
}

void HardFault_Handler(void)
{
    gsys_HardFault_Handler();
}

void MemoryManagement_Handler(void)
{
    gsys_MemManage_Handler();
}

void BusFault_Handler(void)
{
    gsys_BusFault_Handler();
}

void UsageFault_Handler(void)
{
    gsys_UsageFault_Handler();
}

#endif

#if defined(USE_FLASH_DMA) || defined(USE_EEPROM_DMA)
    #ifndef GSYSTEM_NO_STORAGE_AT
        #include "StorageAT.h"
    #endif
#endif

#if defined(USE_FLASH_DMA)

    #include "w25qxx.h"

    #if defined(STM32F1)
        #if GSYSTEM_FLASH_SPI == hspi1
            // SPI1 RX -> DMA1 Channel 2
            #define DMA_RX_FUNC DMA1_Channel2_IRQHandler
            // SPI1 TX -> DMA1 Channel 3
            #define DMA_TX_FUNC DMA1_Channel3_IRQHandler
        #elif GSYSTEM_FLASH_SPI == hspi2
            // SPI2 RX -> DMA1 Channel 4
            #define DMA_RX_FUNC DMA1_Channel4_IRQHandler
            // SPI2 TX -> DMA1 Channel 5
            #define DMA_TX_FUNC DMA1_Channel5_IRQHandler
        #elif GSYSTEM_FLASH_SPI == hspi3
            // SPI3 RX -> DMA2 Channel 1
            #define DMA_RX_FUNC DMA2_Channel1_IRQHandler
            // SPI3 TX -> DMA2 Channel 2
            #define DMA_TX_FUNC DMA2_Channel2_IRQHandler
        #endif
    #elif defined(STM32F4)

        #if GSYSTEM_FLASH_SPI == hspi1
            #if GSYSTEM_MEMORY_STREAM_RX == 0
				#define DMA_RX_FUNC DMA2_Stream0_IRQHandler
            #endif
            #if GSYSTEM_MEMORY_STREAM_RX == 2
				#define DMA_RX_FUNC DMA2_Stream2_IRQHandler
            #endif
            #if GSYSTEM_MEMORY_STREAM_TX == 3
				#define DMA_TX_FUNC DMA2_Stream3_IRQHandler
            #endif
            #if GSYSTEM_MEMORY_STREAM_RX == 5
				#define DMA_TX_FUNC DMA2_Stream5_IRQHandler
            #endif
        #elif GSYSTEM_FLASH_SPI == hspi2
            #if GSYSTEM_MEMORY_STREAM_RX == 3
				#define DMA_RX_FUNC DMA1_Stream3_IRQHandler
            #endif
            #if GSYSTEM_MEMORY_STREAM_RX == 4
				#define DMA_TX_FUNC DMA1_Stream4_IRQHandler
            #endif
        #elif GSYSTEM_FLASH_SPI == hspi3
            #if GSYSTEM_MEMORY_STREAM_RX == 0
				#define DMA_RX_FUNC DMA1_Stream0_IRQHandler
            #endif
            #if GSYSTEM_MEMORY_STREAM_RX == 2
				#define DMA_RX_FUNC DMA1_Stream2_IRQHandler
            #endif
            #if GSYSTEM_MEMORY_STREAM_RX == 5
				#define DMA_TX_FUNC DMA1_Stream5_IRQHandler
            #endif
            #if GSYSTEM_MEMORY_STREAM_RX == 7
				#define DMA_TX_FUNC DMA1_Stream7_IRQHandler
            #endif
        #endif
    #endif

	#if defined(DMA_RX_FUNC) && defined(DMA_TX_FUNC)

	extern SPI_HandleTypeDef GSYSTEM_FLASH_SPI;

		#ifndef GSYSTEM_NO_STORAGE_AT
		extern StorageAT storage;
		#endif

	extern "C" void DMA_RX_FUNC(void);
	void __concat(gsys_, DMA_RX_FUNC)(void)
{
		#if defined(STM32F1)

		uint32_t isr = ((uint32_t*)GSYSTEM_FLASH_SPI.hdmarx->DmaBaseAddress)[0];
		if (isr & (DMA_FLAG_TC1 << (GSYSTEM_FLASH_SPI.hdmarx->ChannelIndex * 4U))) {
		#elif defined(STM32F4)

		uint32_t isr = ((uint32_t*)GSYSTEM_FLASH_SPI.hdmarx->StreamBaseAddress)[0];
		if (isr & (DMA_FLAG_TCIF0_4 << GSYSTEM_FLASH_SPI.hdmarx->StreamIndex)) {
		#endif

			w25qxx_rx_dma_callback();
		} else {
			w25qxx_error_dma_callback();
		}
		DMA_RX_FUNC();
	}

	extern "C" void DMA_TX_FUNC(void);
	void __concat(gsys_, DMA_TX_FUNC)(void)
	{
		#if defined(STM32F1)

		uint32_t isr = ((uint32_t*)GSYSTEM_FLASH_SPI.hdmatx->DmaBaseAddress)[0];
		if (isr & (DMA_FLAG_TC1 << (GSYSTEM_FLASH_SPI.hdmatx->ChannelIndex * 4U))) {
		#elif defined(STM32F4)

		uint32_t isr = ((uint32_t*)GSYSTEM_FLASH_SPI.hdmatx->StreamBaseAddress)[0];
		if (isr & (DMA_FLAG_TCIF0_4 << GSYSTEM_FLASH_SPI.hdmatx->StreamIndex)) {
		#endif

			w25qxx_rx_dma_callback();
		} else {
			w25qxx_error_dma_callback();
		}
		DMA_TX_FUNC();
	}

    #else
        #error "Memory DMA configuration error."
    #endif

#elif defined(USE_EEPROM_DMA)
    #error "EEPROM DMA has not ready yet."
#endif


#if defined(USE_HAL_DRIVER)

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

#endif


#ifdef TIM2
extern "C" void TIM2_IRQHandler(void); extern "C" void gsys_TIM2_IRQHandler(void);
#endif

#ifdef TIM3
extern "C" void TIM3_IRQHandler(void); extern "C" void gsys_TIM3_IRQHandler(void);
#endif

#ifdef TIM4
extern "C" void TIM4_IRQHandler(void); extern "C" void gsys_TIM4_IRQHandler(void);
#endif

#ifdef TIM5
extern "C" void TIM5_IRQHandler(void); extern "C" void gsys_TIM5_IRQHandler(void);
#endif

#ifdef TIM6
	#if defined(STM32F4)
	extern "C" void TIM6_DAC_IRQHandler(void); extern "C" void gsys_TIM6_DAC_IRQHandler(void);
	#else
	extern "C" void TIM6_IRQHandler(void); extern "C" void gsys_TIM6_IRQHandler(void);
	#endif
#endif

#ifdef TIM7
extern "C" void TIM7_IRQHandler(void); extern "C" void gsys_TIM7_IRQHandler(void);
#endif


#if defined(TIM1) || defined(TIM10)
	#if defined(STM32F4) || defined(TIM10)
	extern "C" void TIM1_UP_TIM10_IRQHandler(void); extern "C" void gsys_TIM1_UP_TIM10_IRQHandler(void);
	#else
	extern "C" void TIM1_UP_IRQHandler(void); extern "C" void gsys_TIM1_UP_IRQHandler(void);
	#endif
#endif

#if defined(TIM9)
extern "C" void TIM1_BRK_TIM9_IRQHandler(void); extern "C" void gsys_TIM1_BRK_TIM9_IRQHandler(void);
#endif

#if defined(TIM11)
extern "C" void TIM1_TRG_COM_TIM11_IRQHandler(void); extern "C" void gsys_TIM1_TRG_COM_TIM11_IRQHandler(void);
#endif


#if defined(TIM8) || defined(TIM13)
	#if defined(STM32F4) || defined(TIM13)
	extern "C" void TIM8_UP_TIM13_IRQHandler(void); extern "C" void gsys_TIM8_UP_TIM13_IRQHandler(void);
	#else
	extern "C" void TIM8_UP_IRQHandler(void); extern "C" void gsys_TIM8_UP_IRQHandler(void);
	#endif
#endif

#if defined(TIM12)
extern "C" void TIM8_BRK_TIM12_IRQHandler(void); extern "C" void gsys_TIM8_BRK_TIM12_IRQHandler(void);
#endif

#if defined(TIM14)
extern "C" void TIM8_TRG_COM_TIM14_IRQHandler(void); extern "C" void gsys_TIM8_TRG_COM_TIM14_IRQHandler(void);
#endif


extern "C" void sys_isr_register() {
#if defined(USE_HAL_DRIVER)
    if (!MCUcheck()) {
		system_error_handler(MCU_ERROR);
	}
	
	memcpy(gsys_isr_vector, flash_vector_table, VECTOR_TABLE_SIZE);

    _change_addr(NMI_Handler,        gsys_NMI_Handler);
	_change_addr(HardFault_Handler,  gsys_HardFault_Handler);
	_change_addr(MemManage_Handler,  gsys_MemManage_Handler);
	_change_addr(BusFault_Handler,   gsys_BusFault_Handler);
	_change_addr(UsageFault_Handler, gsys_UsageFault_Handler);


	#ifdef TIM2
	_change_addr(TIM2_IRQHandler, gsys_TIM2_IRQHandler);
	#endif
	#ifdef TIM3
	_change_addr(TIM3_IRQHandler, gsys_TIM3_IRQHandler);
	#endif
	#ifdef TIM4
	_change_addr(TIM4_IRQHandler, gsys_TIM4_IRQHandler);
	#endif
	#ifdef TIM5
	_change_addr(TIM5_IRQHandler, gsys_TIM5_IRQHandler);
	#endif
	#ifdef TIM6
		#if defined(STM32F4)
		_change_addr(TIM6_DAC_IRQHandler, gsys_TIM6_DAC_IRQHandler);
		#else
		_change_addr(TIM6_IRQHandler, gsys_TIM6_IRQHandler);
		#endif
	#endif
	#ifdef TIM7
	_change_addr(TIM7_IRQHandler, gsys_TIM7_IRQHandler);
	#endif


	#if defined(TIM1) || defined(TIM10)
		#if defined(STM32F4) || defined(TIM10)
		_change_addr(TIM1_UP_TIM10_IRQHandler, gsys_TIM1_UP_TIM10_IRQHandler);
		#else
		_change_addr(TIM1_UP_IRQHandler, gsys_TIM1_UP_IRQHandler);
		#endif
	#endif
	#if defined(TIM9)
	_change_addr(TIM1_BRK_TIM9_IRQHandler, gsys_TIM1_BRK_TIM9_IRQHandler);
	#endif
	#if defined(TIM11)
	_change_addr(TIM1_TRG_COM_TIM11_IRQHandler, gsys_TIM1_TRG_COM_TIM11_IRQHandler);
	#endif

	
	#if defined(TIM8) || defined(TIM13)
		#if defined(STM32F4) || defined(TIM13)
		_change_addr(TIM8_UP_TIM13_IRQHandler, gsys_TIM8_UP_TIM13_IRQHandler);
		#else
		_change_addr(TIM8_UP_IRQHandler, gsys_TIM8_UP_IRQHandler);
		#endif
	#endif
	#if defined(TIM12)
	_change_addr(TIM8_BRK_TIM12_IRQHandler, gsys_TIM8_BRK_TIM12_IRQHandler);
	#endif
	#if defined(TIM14)
	_change_addr(TIM8_TRG_COM_TIM14_IRQHandler, gsys_TIM8_TRG_COM_TIM14_IRQHandler);
	#endif


    #ifdef USE_FLASH_DMA
	_change_addr(DMA_RX_FUNC,        __concat(gsys_, DMA_RX_FUNC));
	_change_addr(DMA_TX_FUNC,        __concat(gsys_, DMA_TX_FUNC));
    #endif


	__disable_irq();
	SCB->VTOR = (uint32_t)gsys_isr_vector;
	__enable_irq();
	__DSB();
	__ISB();
#endif
}

#else

extern "C" void sys_isr_register() {}

#endif
