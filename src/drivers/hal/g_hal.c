/* Copyright © 2025 Georgy E. All rights reserved. */

#include "g_hal.h"


#ifdef USE_HAL_DRIVER


    #include "gconfig.h"
    #include "gdefines.h"

    #include "glog.h"
    #include "gsystem.h"


#if GSYSTEM_BEDUG && !defined(GSYSTEM_NO_CPU_INFO)
static void COREInfo(void);
static void FPUInfo(void);
static void IDCODEInfo(void);
#endif

extern uint32_t _sdata;
extern uint32_t _estack;


static const uint32_t TIMER_FREQ_MUL   = 2;


void g_reboot()
{
    NVIC_SystemReset();
}

void g_timer_start(system_timer_t* timer, hard_tim_t* fw_tim, uint32_t delay_ms)
{
    // TODO: timers after system_timer_start don't work
    memset(timer, 0, sizeof(system_timer_t));
    switch ((uint32_t)fw_tim) {
    case TIM1_BASE:
        timer->enabled = READ_BIT(RCC->APB2ENR, RCC_APB2ENR_TIM1EN);
        break;
    case TIM2_BASE:
        timer->enabled = READ_BIT(RCC->APB1ENR, RCC_APB1ENR_TIM2EN);
        break;
    case TIM3_BASE:
        timer->enabled = READ_BIT(RCC->APB1ENR, RCC_APB1ENR_TIM3EN);
        break;
    case TIM4_BASE:
        timer->enabled = READ_BIT(RCC->APB1ENR, RCC_APB1ENR_TIM4EN);
        break;
    default:
        BEDUG_ASSERT(false, "GSYSTEM TIM WAS NOT SELECTED");
        return;
    }
    if (timer->enabled) {
        memcpy((void*)&timer->bkup_tim, (void*)fw_tim, sizeof(TIM_TypeDef));
    }
    timer->tim = fw_tim;
    timer->end = delay_ms / SECOND_MS;
    memset((void*)fw_tim, 0, sizeof(TIM_TypeDef));

    uint32_t count = 0;
    switch ((uint32_t)fw_tim) {
    case TIM1_BASE:
        __TIM1_CLK_ENABLE();
        count = HAL_RCC_GetPCLK2Freq();
        break;
    case TIM2_BASE:
        __TIM2_CLK_ENABLE();
        count = HAL_RCC_GetPCLK1Freq();
        break;
    case TIM3_BASE:
        __TIM3_CLK_ENABLE();
        count = HAL_RCC_GetPCLK1Freq();
        break;
    case TIM4_BASE:
        __TIM4_CLK_ENABLE();
        count = HAL_RCC_GetPCLK1Freq();
        break;
    default:
#if GSYSTEM_BEDUG
        BEDUG_ASSERT(false, "GSYSTEM TIM WAS NOT SELECTED");
#endif
        return;
    }
    if (count) {
        count *= TIMER_FREQ_MUL;
        count /= SECOND_MS;
    }
    uint32_t presc = SECOND_MS;
    if (delay_ms < SECOND_MS) {
        timer->end = 1;
        presc      = delay_ms;
    }
    while (count > 0xFFFF) {
        presc *= 2;
        count /= 2;
    }
    // TODO: add interrupt with new VTOR
    if (presc > 0xFFFF) {
        presc = 0xFFFF;
    }
    if (presc == 1) {
        count /= TIMER_FREQ_MUL;
    }

    timer->tim->PSC  = presc - 1;
    timer->tim->ARR  = count - 1;
    timer->tim->CR1  = 0;

    timer->tim->EGR  = TIM_EGR_UG;
    timer->tim->CR1 |= TIM_CR1_UDIS;

    timer->tim->CNT  = 0;
    timer->tim->SR   = 0;
    timer->tim->CR1 &= ~(TIM_CR1_DIR);
    timer->tim->CR1 |= TIM_CR1_OPM;
    timer->tim->CR1 |= TIM_CR1_CEN;

    timer->verif = TIMER_VERIF_WORD;
}

bool g_timer_wait(system_timer_t* timer)
{
    if (timer->tim->SR & TIM_SR_CC1IF) {
        timer->count++;
        timer->tim->SR   = 0;
        timer->tim->CNT  = 0;
        timer->tim->CR1 |= TIM_CR1_CEN;
    }
    return timer->count < timer->end;
}

void g_timer_stop(system_timer_t* timer)
{
    timer->tim->SR &= ~(TIM_SR_UIF | TIM_SR_CC1IF);
    timer->tim->CNT = 0;

    if (timer->enabled) {
        memcpy(timer->tim, &timer->bkup_tim, sizeof(TIM_TypeDef));
    } else {
        timer->tim->CR1 &= ~(TIM_CR1_CEN);
        switch ((uint32_t)timer->tim) {
        case TIM1_BASE:
            __TIM1_CLK_DISABLE();
            break;
        case TIM2_BASE:
            __TIM2_CLK_DISABLE();
            break;
        case TIM3_BASE:
            __TIM3_CLK_DISABLE();
            break;
        case TIM4_BASE:
            __TIM4_CLK_DISABLE();
            break;
        default:
#if GSYSTEM_BEDUG
            BEDUG_ASSERT(false, "GSYSTEM TIM WAS NOT SELECTED");
#endif
            return;
        }
        memset((void*)timer->tim, 0, sizeof(TIM_TypeDef));
    }

    timer->verif = 0;
    timer->tim = NULL;
}

void g_restart_check() 
{
    bool flag = false;
    // IWDG check reboot
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST)) {
    	SYSTEM_BEDUG("IWDG just went off");
        flag = true;
    }

    // WWDG check reboot
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST)) {
    	SYSTEM_BEDUG("WWDG just went off");
        flag = true;
    }

    if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST)) {
        SYSTEM_BEDUG("SOFT RESET");
        flag = true;
    }

    if (flag) {
        __HAL_RCC_CLEAR_RESET_FLAGS();
        SYSTEM_BEDUG("DEVICE HAS BEEN REBOOTED");
    }
}

uint32_t g_get_freq()
{
	return HAL_RCC_GetSysClockFreq();
}

uint32_t* g_heap_start()
{
    return &_sdata;
}

uint32_t* g_stack_end()
{
    return &_estack;
}

bool g_pin_read(port_pin_t pin)
{
    return HAL_GPIO_ReadPin(pin.port, pin.pin);
}

uint64_t g_serial()
{
    uint64_t serial = 0;
    uint32_t uid_base = 0x1FFFF7E8;
    uint32_t *idBase1 = (uint32_t*)(uid_base + 0x04);
    uint32_t *idBase2 = (uint32_t*)(uid_base + 0x08);
    serial = (uint64_t)*idBase1 << 32 | (uint64_t)*idBase2;
    return serial;
}

char* g_serial_number()
{
    uint32_t uid_base = 0x1FFFF7E8;

    uint16_t *idBase0 = (uint16_t*)(uid_base);
    uint16_t *idBase1 = (uint16_t*)(uid_base + 0x02);
    uint32_t *idBase2 = (uint32_t*)(uid_base + 0x04);
    uint32_t *idBase3 = (uint32_t*)(uid_base + 0x08);

    static char str_uid[25] = {0};
    memset((void*)str_uid, 0, sizeof(str_uid));
    sprintf(str_uid, "%04X%04X%08lX%08lX", *idBase0, *idBase1, *idBase2, *idBase3);

    return str_uid;
}

#if !defined(GSYSTEM_NO_PRINTF) || defined(GSYSTEM_BEDUG_UART)
int _write(int line, uint8_t *ptr, int len) {
    (void)line;
    (void)ptr;
    (void)len;

    #if defined(GSYSTEM_BEDUG_UART)
    extern UART_HandleTypeDef GSYSTEM_BEDUG_UART;
    HAL_UART_Transmit(&GSYSTEM_BEDUG_UART, (uint8_t*)ptr, (uint16_t)(len), 100);
    #endif

    #if !defined(GSYSTEM_NO_PRINTF)
    for (int DataIdx = 0; DataIdx < len; DataIdx++) {
        ITM_SendChar(*ptr++);
    }
    #endif

    return len;
}
#endif


void SystemInfo(void)
{
#ifndef GSYSTEM_NO_CPU_INFO
	SYSTEM_BEDUG("Core=%lu, %lu MHz", SystemCoreClock, SystemCoreClock / 1000000);
	COREInfo();
	IDCODEInfo();
	FPUInfo();
	printPretty("APB1=%lu\n", HAL_RCC_GetPCLK1Freq());
	printPretty("APB2=%lu\n", HAL_RCC_GetPCLK2Freq());
#endif
}

bool MCUcheck(void)
{
	uint32_t cpuid = SCB->CPUID;
	if ((cpuid & 0xFF000000) != 0x41000000) {
		return false;
	}
#if defined(STM32F1)
	return ((cpuid & 0x0000FFF0) >> 4) == 0xC23;
#elif defined(STM32F4)
	return ((cpuid & 0x0000FFF0) >> 4) == 0xC24;
#else
    #error "Select MCU"
#endif
}

#ifndef GSYSTEM_NO_CPU_INFO
void COREInfo(void)
{
    #if defined(_DEBUG) || defined(DEBUG) || defined(GBEDUG_FORCE)
	uint32_t cpuid = SCB->CPUID;

	printPretty(
		"CPUID 0x%08X DEVID 0x%03X REVID 0x%04X\n",
		(int)cpuid,
		(int)(DBGMCU->IDCODE & 0xFFF),
		(int)((DBGMCU->IDCODE >> 16) & 0xFFFF)
	);

	uint32_t pat = (cpuid & 0x0000000F);
	uint32_t var = (cpuid & 0x00F00000) >> 20;

	if ((cpuid & 0xFF000000) == 0x41000000) { // ARM
		switch((cpuid & 0x0000FFF0) >> 4) {
		case 0xC20:
			printPretty("Cortex M0 r%lup%lu\n", var, pat);
			break;
		case 0xC60:
			printPretty("Cortex M0+ r%lup%lu\n", var, pat);
			break;
		case 0xC21:
			printPretty("Cortex M1 r%lup%lu\n", var, pat);
			break;
		case 0xC23:
			printPretty("Cortex M3 r%lup%lu\n", var, pat);
			break;
		case 0xC24:
			printPretty("Cortex M4 r%lup%lu\n", var, pat);
			break;
		case 0xC27:
			printPretty("Cortex M7 r%lup%lu\n", var, pat);
			break;
		default:
			printPretty("Unknown CORE\n");
		}
	} else {
		printPretty("Unknown CORE IMPLEMENTER\n");
	}
    #endif
}

void FPUInfo(void)
{
	uint32_t mvfr0 = *(volatile uint32_t *)0xE000EF40;

	if (mvfr0) {
		printPretty(
			"0x%08X 0x%08X 0x%08X\n",
			(int)*(volatile uint32_t *)0xE000EF34,   // FPCCR  0xC0000000
			(int)*(volatile uint32_t *)0xE000EF38,   // FPCAR
			(int)*(volatile uint32_t *)0xE000EF3C    // FPDSCR
		);  // MVFR2  0x00000040
		printPretty(
			"0x%08X 0x%08X 0x%08X\n",
			(int)*(volatile uint32_t *)0xE000EF40,   // MVFR0  0x10110021 vs 0x10110221
			(int)*(volatile uint32_t *)0xE000EF44,   // MVFR1  0x11000011 vs 0x12000011
			(int)*(volatile uint32_t *)0xE000EF48
		);
	}

	switch(mvfr0) {
	case 0x00000000:
		printPretty("No FPU\n");
		break;
	case 0x10110021:
		printPretty("FPU-S Single-precision only\n");
		break;
	case 0x10110221:
		printPretty("FPU-D Single-precision and Double-precision\n");
		break;
	default:
		printPretty("Unknown FPU\n");
		break;
	}
}

void IDCODEInfo(void)
{
	uint32_t idcode = DBGMCU->IDCODE & 0xFFF;

	printPretty("");
	switch(idcode) {
	case 0x410:
        gprint("STM32F103\n");
        break;
	case 0x411:
        gprint("STM32F457\n");
        break;
	case 0x413:
        gprint("STM32F407\n");
        break;
	case 0x415:
        gprint("STM32L475xx, L476xx or L486xx\n");
        break;
	case 0x417:
        gprint("STM32L0 Cat 3\n");
        break;
	case 0x419:
        gprint("STM32F429 or F439\n");
        break;
	case 0x421:
        gprint("STM32F446\n");
        break;
	case 0x423:
        gprint("STM32F401\n");
        break;
	case 0x431:
        gprint("STM32F411\n");
        break;
	case 0x433:
        gprint("STM32F401\n");
        break;
	case 0x434:
        gprint("STM32F469\n");
        break;
	case 0x435:
        gprint("STM32L43xxx or L44xxx\n");
        break;
	case 0x440:
        gprint("STM32F030x8\n");
        break;
	case 0x441:
        gprint("STM32F412\n");
        break;
	case 0x442:
        gprint("STM32F030xC\n");
        break;
	case 0x444:
        gprint("STM32F030x4 or F030x6\n");
        break;
	case 0x445:
        gprint("STM32F070x6\n");
        break;
	case 0x447:
        gprint("STM32L0 Cat 5\n");
        break;
	case 0x448:
        gprint("STM32F070x8\n");
        break;
	case 0x449:
        gprint("STM32F74xxx or F75xxx\n");
        break;
	case 0x450:
        gprint("STM32H7xx\n");
        break;
	case 0x451:
        gprint("STM32F76xxx or F77xxx\n");
        break;
	case 0x452:
        gprint("STM32F72xxx or F73xxx\n");
        break;
	case 0x457:
        gprint("STM32L011xx\n");
        break;
	case 0x461:
        gprint("STM32L496xx or L4A6xx\n");
        break;
	case 0x462:
        gprint("STM32L45xxx or L46xxx\n");
        break;
	case 0x470:
        gprint("STM32L4Rxxx or L4Sxxx\n");
        break;
	case 0x480:
        gprint("STM32H7Ax or H7Bx\n");
        break;
	default:
        gprint("Unknown STM32 (IDCODE=0x%X)\n", (int)idcode);
		break;
	}
}
#endif


#endif
