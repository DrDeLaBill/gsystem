/* Copyright © 2025 Georgy E. All rights reserved. */

#include "w25qxx.h"


#include "gdefines.h"
#include "gconfig.h"


#if defined(GSYSTEM_FLASH_MODE ) && defined(GSYSTEM_MEMORY_DMAcd )


#include "glog.h"
#include "fsm_gc.h"
#include "hal_defs.h"
#include "circle_buf_gc.h"


#define W25Q_SPI_TIMEOUT_MS       ((uint32_t)SECOND_MS)
#define W25Q_SPI_ERASE_TIMEOUT_MS ((uint32_t)5 * SECOND_MS)
#define W25Q_SPI_WRITE_TIMEOUT_MS ((uint32_t)5 * SECOND_MS)
#define W25Q_SPI_COMMAND_SIZE_MAX ((uint8_t)10)
#define W25Q_SPI_ATTEMPTS_CNT     (5)


typedef enum _dma_status_t {
	W25Q_DMA_READY,
	W25Q_DMA_READ,
	W25Q_DMA_WRITE,
	W25Q_DMA_ERASE,
	W25Q_DMA_FREE,
	W25Q_DMA_WRITE_OFF,
	W25Q_DMA_WRITE_ON,
} dma_status_t;

typedef struct _w25q_dma_t {
	circle_buf_gc_t queue;

    uint8_t         counter;
	gtimer_t        timer;
	uint32_t        addr;
	uint8_t         buffer[W25Q_SECTOR_SIZE];
	uint8_t*        rx_ptr;
	uint8_t*        tx_ptr;
	uint32_t        len;
    uint8_t         cmd[W25Q_SPI_COMMAND_SIZE_MAX];
    uint8_t         sr1;
    flash_status_t  result;

	bool            write_done;
	bool            write_erase;
	uint32_t        write_addr;
	uint8_t*        write_ptr;
	uint8_t         write_buf[W25Q_PAGE_SIZE];

    uint32_t*       erase_addr;
    uint32_t        erase_cnt;
    uint32_t        erase_iter;
} w25q_dma_t;


bool        _w25q_ready();

static void _write_dma_internal_callback(const flash_status_t status);
static void _read_dma_internal_callback(const flash_status_t status);
static void _erase_dma_internal_callback(const flash_status_t status);

static void _w25q_route(const dma_status_t status);

static bool _w25q_tx(const uint8_t* data, const uint32_t len);
static bool _w25q_rx(uint8_t* data, const uint32_t len);


#ifdef GSYSTEM_BEDUG
extern const char W25Q_TAG[];
#endif


extern bool _w25q_initialized();
extern bool _w25q_24bit();
extern void _W25Q_CS_set();
extern void _W25Q_CS_reset();

static dma_status_t _queue[10] = {0};
static w25q_dma_t w25q_dma = {
	.queue        = {0},

    .counter      = 0,
    .timer        = {0},
    .addr         = 0,
    .buffer       = {0},
    .rx_ptr       = NULL,
    .tx_ptr       = NULL,
    .len          = 0,
    .cmd          = {0},
    .sr1          = 0,
    .result       = FLASH_OK,

	.write_done    = false,
	.write_erase   = false,
	.write_addr    = 0,
	.write_ptr     = NULL,
	.write_buf     = {0},

    .erase_addr   = NULL,
    .erase_cnt    = 0,
	.erase_iter   = 0,
};


void w24qxx_tick()
{
	static bool initialized = false;
	if (!initialized) {
		fsm_gc_init(&w25qxx_fsm, w25qxx_fsm_table, __arr_len(w24qxx_fsm_table));
		initialized = true;
	}
	fsm_gc_process(&w25qxx_fsm);
}

flash_status_t w25qxx_read_dma(const uint32_t addr, uint8_t* data, const uint32_t len)
{
    if (!_w25q_ready()) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash DMA read addr=%08lX len=%lu (flash not ready)", addr, len);
#endif
    	return FLASH_ERROR;
    }

    if (addr + len > _flash_get_storage_bytes_size()) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash DMA read addr=%08lX len=%lu: error (unacceptable address)", addr, len);
#endif
        return FLASH_OOM;
    }

	if (!data) {
#if W25Q_BEDUG
		printTagLog(W25Q_TAG, "flash DMA read addr=%08lX len=%lu: error (NULL buffer)", addr, len);
#endif
		return FLASH_ERROR;
	}

	circle_buf_gc_push_back(&w25q_dma.queue, W25Q_DMA_READ);
	w25q_dma.addr   = addr;
	w25q_dma.len    = len;
	w25q_dma.rx_ptr = data;

	return FLASH_OK;
}

flash_status_t w25qxx_write_dma(const uint32_t addr, const uint8_t* data, const uint32_t len)
{
    if (!_w25q_ready()) {
#if W25Q_BEDUG
		printTagLog(W25Q_TAG, "flash DMA write addr=%08lX len=%lu (flash not ready)", addr, len);
#endif
		return FLASH_ERROR;
	}

	if (addr + len > _flash_get_storage_bytes_size()) {
#if W25Q_BEDUG
		printTagLog(W25Q_TAG, "flash DMA write addr=%08lX len=%lu: error (unacceptable address)", addr, len);
#endif
		return FLASH_OOM;
	}

	if (!data) {
#if W25Q_BEDUG
		printTagLog(W25Q_TAG, "flash DMA write addr=%08lX len=%lu: error (NULL buffer)", addr, len);
#endif
		return FLASH_ERROR;
	}

	if (len > sizeof(w25q_dma.buffer)) {
#if W25Q_BEDUG
		printTagLog(W25Q_TAG, "flash DMA write addr=%08lX len=%lu: error (overflow buffer)", addr, len);
#endif
		return FLASH_ERROR;
	}

	circle_buf_gc_push_back(&w25q_dma.queue, W25Q_DMA_WRITE);
	w25q_dma.addr   = addr;
	w25q_dma.len    = len;
	w25q_dma.tx_ptr = data;

	return FLASH_OK;
}

flash_status_t w25qxx_erase_addresses_dma(const uint32_t* addrs, const uint32_t count)
{
    if (!_w25q_ready()) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "erase flash addresses error:flash not ready");
#endif
        return FLASH_ERROR;
    }

    if (!addrs) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "erase flash addresses error: addresses=NULL");
#endif
        return FLASH_ERROR;
    }

    if (!count) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "erase flash addresses error: count=%lu", count);
#endif
        return FLASH_ERROR;
    }

#if W25Q_BEDUG
    printTagLog(W25Q_TAG, "erase flash addresses: ")
    for (uint32_t i = 0; i < count; i++) {
        gprint("%08lX ", addrs[i]);
    }
    gprint("\n");
#endif

	circle_buf_gc_push_back(&w25q_dma.queue, W25Q_DMA_ERASE);
    w25q_dma.erase_addr = addrs;
    w25q_dma.erase_cnt  = count;

    return FLASH_OK;
}

__attribute__((weak)) void w25qxx_read_event(const flash_status_t status)
{
	(void)status;
}

__attribute__((weak)) void w25qxx_write_event(const flash_status_t status)
{
	(void)status;
}

__attribute__((weak)) void w25qxx_erase_event(const flash_status_t status)
{
	(void)status;
}

void w25qxx_tx_dma_callback()
{
	switch (*(dma_status_t*)circle_buf_gc_back(&w25q_dma.queue))
	{
	case W25Q_DMA_READ:
		fsm_gc_push_event(&w25qxx_read_fsm, &transmit_e);
		break;
	case W25Q_DMA_WRITE:
		fsm_gc_push_event(&w25qxx_write_fsm, &transmit_e);
		break;
	case W25Q_DMA_ERASE:
		fsm_gc_push_event(&w25qxx_erase_fsm, &transmit_e);
		break;
	case W25Q_DMA_FREE:
		fsm_gc_push_event(&w25qxx_fsm, &transmit_e);
		break;
	case W25Q_DMA_WRITE_ON:
		fsm_gc_push_event(&w25qxx_write_on_fsm, &transmit_e);
		break;
	case W25Q_DMA_WRITE_OFF:
		fsm_gc_push_event(&w25qxx_write_off_fsm, &transmit_e);
		break;
	default:
		fsm_gc_push_event(&w25qxx_fsm, &transmit_e);
		break;
	}
}

void w25qxx_rx_dma_callback()
{
	switch (*(dma_status_t*)circle_buf_gc_back(&w25q_dma.queue))
	{
	case W25Q_DMA_READ:
		fsm_gc_push_event(&w25qxx_read_fsm, &receive_e);
		break;
	case W25Q_DMA_WRITE:
		fsm_gc_push_event(&w25qxx_write_fsm, &receive_e);
		break;
	case W25Q_DMA_ERASE:
		fsm_gc_push_event(&w25qxx_erase_fsm, &receive_e);
		break;
	case W25Q_DMA_FREE:
		fsm_gc_push_event(&w25qxx_fsm, &receive_e);
		break;
	case W25Q_DMA_WRITE_ON:
		fsm_gc_push_event(&w25qxx_write_on_fsm, &receive_e);
		break;
	case W25Q_DMA_WRITE_OFF:
		fsm_gc_push_event(&w25qxx_write_off_fsm, &receive_e);
		break;
	default:
		fsm_gc_push_event(&w25qxx_fsm, &receive_e);
		break;
	}
    _W25Q_CS_reset();
}

void w25qxx_error_dma_callback()
{
	switch (*(dma_status_t*)circle_buf_gc_back(&w25q_dma.queue))
	{
	case W25Q_DMA_READ:
		fsm_gc_push_event(&w25qxx_read_fsm, &error_e);
		break;
	case W25Q_DMA_WRITE:
		fsm_gc_push_event(&w25qxx_write_fsm, &error_e);
		break;
	case W25Q_DMA_ERASE:
		fsm_gc_push_event(&w25qxx_erase_fsm, &error_e);
		break;
	case W25Q_DMA_FREE:
		fsm_gc_push_event(&w25qxx_fsm, &error_e);
		break;
	case W25Q_DMA_WRITE_ON:
		fsm_gc_push_event(&w25qxx_write_on_fsm, &error_e);
		break;
	case W25Q_DMA_WRITE_OFF:
		fsm_gc_push_event(&w25qxx_write_off_fsm, &error_e);
		break;
	default:
		fsm_gc_push_event(&w25qxx_fsm, &error_e);
		break;
	}
    _W25Q_CS_reset();
}

bool _w25q_tx(const uint8_t* data, const uint32_t len)
{
    return HAL_SPI_Transmit_DMA(&GSYSTEM_FLASH_SPI, data, len) == HAL_OK;
}

bool _w25q_rx(uint8_t* data, const uint32_t len)
{
    return HAL_SPI_Receive_DMA(&GSYSTEM_FLASH_SPI, data, len) == HAL_OK;
}

bool _w25q_ready()
{
	return _w25q_initialized() && circle_buf_gc_empty(&w25q_dma.queue);
}

void _write_dma_internal_callback(const flash_status_t status)
{
	w25qxx_write_event(status);
}

void _read_dma_internal_callback(const flash_status_t status)
{
	w25qxx_read_event(status);
}

void _erase_dma_internal_callback(const flash_status_t status)
{
	w25qxx_erase_event(status);
}


FSM_GC_CREATE(w25qxx_fsm)
FSM_GC_CREATE(w25qxx_free_fsm)
FSM_GC_CREATE(w25qxx_write_on_fsm)
FSM_GC_CREATE(w25qxx_write_off_fsm)
FSM_GC_CREATE(w25qxx_read_fsm)
FSM_GC_CREATE(w25qxx_write_fsm)
FSM_GC_CREATE(w25qxx_erase_fsm)

FSM_GC_CREATE_EVENT(success_e,   0)
FSM_GC_CREATE_EVENT(router_e,    0)
FSM_GC_CREATE_EVENT(free_e,      0)
FSM_GC_CREATE_EVENT(write_on_e,  0)
FSM_GC_CREATE_EVENT(write_off_e, 0)
FSM_GC_CREATE_EVENT(read_e,      0)
FSM_GC_CREATE_EVENT(write_e,     0)
FSM_GC_CREATE_EVENT(erase_e,     0)
FSM_GC_CREATE_EVENT(receive_e,   0)
FSM_GC_CREATE_EVENT(transmit_e,  0)  
FSM_GC_CREATE_EVENT(next_e,      0)
FSM_GC_CREATE_EVENT(timeout_e,   1)
FSM_GC_CREATE_EVENT(error_e,     2)

FSM_GC_CREATE_STATE(init_s,      _init_s)
FSM_GC_CREATE_STATE(idle_s,      _idle_s)
FSM_GC_CREATE_STATE(router_s,    _router_s)
FSM_GC_CREATE_STATE(free_s,      _free_s)
FSM_GC_CREATE_STATE(write_on_s,  _write_on_s)
FSM_GC_CREATE_STATE(write_off_s, _write_off_s)
FSM_GC_CREATE_STATE(read_s,      _read_s)
FSM_GC_CREATE_STATE(write_s,     _write_s)
FSM_GC_CREATE_STATE(erase_s,     _erase_s)

FSM_GC_CREATE_ACTION(idle_a,      _idle_a)
FSM_GC_CREATE_ACTION(router_a,    _router_a)
FSM_GC_CREATE_ACTION(free_a,      _free_a)
FSM_GC_CREATE_ACTION(write_on_a,  _write_on_a)
FSM_GC_CREATE_ACTION(write_off_a, _write_off_a)
FSM_GC_CREATE_ACTION(read_a,      _read_a)
FSM_GC_CREATE_ACTION(write_a,     _write_a)
FSM_GC_CREATE_ACTION(erase_a,     _erase_a)
FSM_GC_CREATE_ACTION(callback_a,  _callback_a)

FSM_GC_CREATE_TABLE(
	w25qxx_fsm_table,
	{&init_s,      &success_e,   &idle_s,      &idle_a},

	{&idle_s,      &router_e,    &router_s,    &router_a},

	{&router_s,    &read_e,      &read_s,      &read_a},
	{&router_s,    &write_e,     &write_s,     &write_a},
	{&router_s,    &erase_e,     &erase_s,     &erase_a},
	{&router_s,    &free_a,      &free_s,      &free_a},
	{&router_s,    &write_on_e,  &write_on_s,  &write_on_a},
	{&router_s,    &write_off_e, &write_off_s, &write_off_a},
	{&router_s,    &error_e,     &idle_s,      &idle_a},

	{&free_s,      &timeout_e,   &free_s,      &free_a},
	{&free_s,      &transmit_e,  &free_s,      &free_rx_a},
	{&free_s,      &receive_e,   &idle_s,      &callback_a},
	{&free_s,      &error_e,     &idle_s,      &callback_a},

	{&write_on_s,  &success_e,   &idle_s,      &callback_a},
	{&write_on_s,  &router_e,    &router_s,    &router_a},
	{&write_on_s,  &error_e,     &idle_s,      &callback_a},

	{&write_off_s, &success_e,   &idle_s,      &callback_a},
	{&write_off_s, &router_e,    &router_s,    &router_a},
	{&write_off_s, &error_e,     &idle_s,      &callback_a},

	{&read_s,      &success_e,   &idle_s,      &callback_a},
	{&read_s,      &router_e,    &router_s,    &router_a},
	{&read_s,      &error_e,     &idle_s,      &callback_a},

	{&write_s,     &success_e,   &idle_s,      &callback_a},
	{&write_s,     &router_e,    &router_s,    &router_a},
	{&write_s,     &error_e,     &idle_s,      &callback_a},

	{&erase_s,     &success_e,   &idle_s,      &callback_a},
	{&erase_s,     &router_e,    &router_s,    &router_a},
	{&erase_s,     &error_e,     &idle_s,      &callback_a},
)

void _init_s(void)
{
	if (w25qxx_init() == FLASH_OK) {
		circle_buf_gc_init(&w25q_dma.queue, (uint8_t*)&_queue, sizeof(_queue[0]), __arr_len(_queue));
		fsm_gc_set_event(&w25qxx_fsm, success_e);
	}
}

void _idle_a(void)
{
	circle_buf_gc_free(&w25q_dma.queue);
}

void _idle_s(void)
{
	if (!circle_buf_gc_empty(&w25q_dma.queue)) {
		fsm_gc_set_event(&w25qxx_fsm, router_e);
	}
}

void _router_a(void) {}

void _router_s(void)
{
	if (circle_buf_gc_empty(&w25q_dma.queue)) {
		return;
	}
	switch (*(dma_status_t*)circle_buf_gc_pop_back(&w25q_dma.queue))
	{
	case W25Q_DMA_READ:
		fsm_gc_push_event(&w25qxx_fsm, &read_e);
		break;
	case W25Q_DMA_WRITE:
		fsm_gc_push_event(&w25qxx_fsm, &write_e);
		break;
	case W25Q_DMA_ERASE:
		fsm_gc_push_event(&w25qxx_fsm, &erase_e);
		break;
	case W25Q_DMA_FREE:
		fsm_gc_push_event(&w25qxx_fsm, &free_e);
		break;
	case W25Q_DMA_WRITE_ON:
		fsm_gc_push_event(&w25qxx_fsm, &write_on_e);
		break;
	case W25Q_DMA_WRITE_OFF:
		fsm_gc_push_event(&w25qxx_fsm, &write_off_e);
		break;
	default:
		circle_buf_gc_free(&w25q_dma.queue);
		break;
	}
}

void _callback_a(void)
{
	switch (*(dma_status_t*)circle_buf_gc_back(&w25q_dma.queue))
	{
	case W25Q_DMA_READ:
        if (!fsm_gc_is_state(&w25qxx_fsm, &read_s)) {
            return;
        }
		circle_buf_gc_free(&w25q_dma.queue);
		w25qxx_read_event(w25q_dma.result);
		break;
	case W25Q_DMA_WRITE:
        if (!fsm_gc_is_state(&w25qxx_fsm, &write_s)) {
            return;
        }
		circle_buf_gc_free(&w25q_dma.queue);
		w25qxx_write_event(w25q_dma.result);
		break;
	case W25Q_DMA_ERASE:
        if (!fsm_gc_is_state(&w25qxx_fsm, &erase_s)) {
            return;
        }
		circle_buf_gc_free(&w25q_dma.queue);
		w25qxx_erase_event(w25q_dma.result);
		break;
	default:
		break;
	}
    w25q_dma.count = 0;
}

void _w25q_route(const dma_status_t status)
{
	if (circle_buf_gc_full(&w25q_dma.queue)) {
		fsm_gc_reset(&w25qxx_fsm);
		fsm_gc_reset(&w25qxx_free_fsm);
		fsm_gc_reset(&w25qxx_write_on_fsm);
		fsm_gc_reset(&w25qxx_write_off_fsm);
		fsm_gc_reset(&w25qxx_read_fsm);
		fsm_gc_reset(&w25qxx_write_fsm);
		fsm_gc_reset(&w25qxx_erase_fsm);
	} else {
#if W25Q_BEDUG
		printTagLog(W25Q_TAG, "push: %u", status);
#endif
		circle_buf_gc_push_back(&w25q_dma.queue, status);
		fsm_gc_push_event(&w25qxx_fsm, &router_e);
	}
}

FSM_GC_CREATE_STATE(free_init_s, _free_init_s)
FSM_GC_CREATE_STATE(free_free_s, _free_free_s)

FSM_GC_CREATE_ACTION(free_count_a,    _free_count_a)
FSM_GC_CREATE_ACTION(free_tx_a,      _free_tx_a)
FSM_GC_CREATE_ACTION(free_rx_a,      _free_rx_a)
FSM_GC_CREATE_ACTION(free_success_a, _free_success_a)
FSM_GC_CREATE_ACTION(free_error_a,   _free_error_a)

FSM_GC_CREATE_TABLE(
	w25qxx_free_fsm_table,
	{&free_init_s,  &success_e,   &free_s,      &free_tx_a},

	{&free_free_s,  &timeout_e,   &free_free_s, &free_count_a},
	{&free_free_s,  &transmit_e,  &free_free_s, &free_rx_a},
	{&free_free_s,  &receive_e,   &free_init_s, &free_success_a},
	{&free_free_s,  &error_e,     &free_init_s, &free_error_a},
)

void _free_a(void) {}

void _free_s(void)
{
	static bool initialized = false;
	if (!initialized) {
		fsm_gc_init(&w25qxx_free_fsm, w25qxx_free_fsm_table, __arr_len(w25qxx_free_fsm_table));
		initialized = true;
	}
	fsm_gc_process(&w25qxx_free_fsm);
}

void _free_init_s(void)
{
    _W25Q_CS_reset();
	w25q_dma.count = 0;
    fsm_gc_clear(&w25qxx_free_fsm);
    fsm_gc_push_event(&w25qxx_free_fsm, &success_e);
}

void _free_count_a(void)
{
    if (w25q_dma.count > W25Q_SPI_ATTEMPTS_CNT) {
        w25q_dma.result = FLASH_BUSY;
        fsm_gc_push_event(&w25qxx_free_fsm, &error_e);
    } else {
        w25q_dma.count++;
        _free_tx_a();
    }
}

void _free_tx_a(void)
{
    _W25Q_CS_set();
    w25q_dma.sr1 = 0;
    w25q_dma.cmd[0] = { W25Q_CMD_READ_SR1 };
	if (_w25q_tx(w25q_dma.cmd, 1)) {
        w25q_dma.result = FLASH_ERROR;
		fsm_gc_push_event(&w25qxx_free_fsm, &error_e);
	}
	gtimer_start(&w25q_dma.timer, W25Q_SPI_TIMEOUT_MS);
}

void _free_rx_a(void)
{
	if (_w25q_rx(w25q_dma.sr1, 1)) {
        w25q_dma.result = FLASH_ERROR;
		fsm_gc_push_event(&w25qxx_free_fsm, &error_e);
	}
	gtimer_start(&w25q_dma.timer, W25Q_SPI_TIMEOUT_MS);
}

void _free_free_s(void)
{
	if (gtimer_wait(&w25q_dma.timer)) {
        return;
	}
    fsm_gc_push_event(&w25qxx_free_fsm, &timeout_e);
}


FSM_GC_CREATE_STATE(write_on_init_s,         _write_on_init_s)
FSM_GC_CREATE_STATE(write_on_unblock_free_s, _write_on_unblock_free_s)
FSM_GC_CREATE_STATE(write_on_unblock1_s,     _write_on_unblock1_s)
FSM_GC_CREATE_STATE(write_on_unblock2_s,     _write_on_unblock2_s)
FSM_GC_CREATE_STATE(write_on_enable_free_s,  _write_on_enable_free_s)
FSM_GC_CREATE_STATE(write_on_enable_s,       _write_on_enable_s)

FSM_GC_CREATE_ACTION(write_on_success_a,      _write_on_success_a)
FSM_GC_CREATE_ACTION(write_on_error_a,        _write_on_error_a)
FSM_GC_CREATE_ACTION(write_on_unblock_free_a, _write_on_unblock_free_a)
FSM_GC_CREATE_ACTION(write_on_unblock1_a,     _write_on_unblock1_a)
FSM_GC_CREATE_ACTION(write_on_unblock2_a,     _write_on_unblock2_a)
FSM_GC_CREATE_ACTION(write_on_enable_free_a,  _write_on_enable_free_a)
FSM_GC_CREATE_ACTION(write_on_enable_a,       _write_on_enable_a)

FSM_GC_CREATE_TABLE(
	write_on_fsm_table,

	// if free uses
	{&write_on_init_s,         &success_e,  &write_on_unblock_free_s, &write_on_unblock_free_a},

    {&write_on_unblock_free_s, &success_e,  &write_on_unblock1_s,     &write_on_unblock1_a},
	{&write_on_unblock_free_s, &error_e,    &write_on_init_s,         &write_on_error_a},

	{&write_on_unblock1_s,     &transmit_e, &write_on_unblock2_s,     &write_on_unblock2_a},
	{&write_on_unblock1_s,     &error_e,    &write_on_init_s,         &write_on_error_a},

	{&write_on_unblock2_s,     &transmit_e, &write_on_enable_free_s,  &write_on_enable_free_a},
    {&write_on_unblock2_s,     &error_e,    &write_on_init_s,         &write_on_error_a},

	{&write_on_enable_free_s,  &success_e,  &write_on_enable_s,       &write_on_enable_a},
    {&write_on_enable_free_s,  &error_e,    &write_on_init_s,         &write_on_error_a},
	// else
	// {&write_on_init_s,         &success_e,  &write_on_unblock1_s,     &write_on_unblock1_a},

	// {&write_on_unblock1_s,     &transmit_e, &write_on_unblock2_s,     &write_on_unblock2_a},
	// {&write_on_unblock1_s,     &error_e,    &write_on_init_s,         &write_on_error_a},
	
	// {&write_on_unblock2_s,     &transmit_e, &write_on_enable_s,       &write_on_enable_a},
    // {&write_on_unblock2_s,     &error_e,    &write_on_init_s,         &write_on_error_a},
	// endif

    {&write_on_enable_s,       &transmit_e, &write_on_init_s,         &write_on_success_a},
    {&write_on_enable_s,       &error_e,    &write_on_init_s,         &write_on_error_a},
)

void _write_on_a(void) {}

void _write_on_s(void)
{
	static bool initialized = false;
	if (!initialized) {
		fsm_gc_init(&w25qxx_write_on_fsm, write_on_fsm_table, __arr_len(write_on_fsm_table));
		initialized = true;
	}
	fsm_gc_process(&w25qxx_write_on_fsm);
}

void _write_on_init_s(void)
{
    _W25Q_CS_reset();
    fsm_gc_clear(&w25qxx_write_on_fsm);
    fsm_gc_push_event(&w25qxx_write_on_fsm, &success_e);
}

void _write_on_unblock_free_a(void)
{
	_w25q_route(W25Q_DMA_FREE);
}

void _write_on_unblock_free_s(void)
{
    if (w25q.result == FLASH_OK) {
        fsm_gc_push_event(&w25qxx_write_on_fsm, &success_e);
    } else {
        fsm_gc_push_event(&w25qxx_write_on_fsm, &error_e);
    }
}

void _write_on_unblock1_a(void)
{
    _W25Q_CS_set();
	fsm_gc_clear(&w25qxx_write_on_fsm);
    w25q.cmd[0] = W25Q_CMD_WRITE_ENABLE_SR;
	if (_w25q_tx(w25q_dma.cmd, 1)) {
		fsm_gc_push_event(&w25qxx_write_on_fsm, &error_e);
	}
	gtimer_start(&w25q_dma.timer, W25Q_SPI_TIMEOUT_MS);
}

void _write_on_unblock1_s(void)
{
	if (!gtimer_wait(&w25q_dma.timer)) {
		fsm_gc_push_event(&w25qxx_write_on_fsm, &error_e);
	}
}

void _write_on_unblock2_a(void)
{
    _W25Q_CS_set();
	fsm_gc_clear(&w25qxx_write_on_fsm);
    w25q.cmd[0] = W25Q_CMD_WRITE_SR1;
    w25q.cmd[1] = ((W25Q_SR1_UNBLOCK_VALUE & 0x0F) << 2);
	if (_w25q_tx(w25q_dma.cmd, 2)) {
		fsm_gc_push_event(&w25qxx_write_on_fsm, &error_e);
	}
	gtimer_start(&w25q_dma.timer, W25Q_SPI_TIMEOUT_MS);
}

void _write_on_unblock2_s(void)
{
	if (!gtimer_wait(&w25q_dma.timer)) {
        fsm_gc_push_event(&w25qxx_write_on_fsm, &error_e);
    }
}

void _write_on_enable_free_a(void)
{
	_w25q_route(W25Q_DMA_FREE);
}

void _write_on_enable_free_s(void)
{
    if (w25q.result == FLASH_OK) {
        fsm_gc_push_event(&w25qxx_write_on_fsm, &success_e);
    } else {
        fsm_gc_push_event(&w25qxx_write_on_fsm, &error_e);
    }
}

void _write_on_enable_a(void)
{
    _W25Q_CS_set();
	fsm_gc_clear(&w25qxx_write_on_fsm);
    w25q.cmd[0] = W25Q_CMD_WRITE_ENABLE;
	if (_w25q_tx(w25q_dma.cmd, 1)) {
		fsm_gc_push_event(&w25qxx_write_on_fsm, &error_e);
	}
	gtimer_start(&w25q_dma.timer, W25Q_SPI_TIMEOUT_MS);
}

void _write_on_enable_s(void)
{
    if (!gtimer_wait(&w25q_dma.timer)) {
        fsm_gc_push_event(&w25qxx_write_on_fsm, &error_e);
    }
}

void _write_on_success_a(void)
{
    w25q_dma.result = FLASH_OK;
	fsm_gc_clear(&w25qxx_write_on_fsm);
	fsm_gc_push_event(&w25qxx_fsm, &success_e);
}

void _write_on_error_a(void)
{
    if (w25q_dma.result != FLASH_OK) {
        w25q_dma.result = FLASH_ERROR;
    }
	fsm_gc_clear(&w25qxx_write_on_fsm);
	fsm_gc_push_event(&w25qxx_fsm, &error_e);
}


FSM_GC_CREATE_STATE(write_off_init_s,         _write_off_init_s)
FSM_GC_CREATE_STATE(write_off_disable_free_s, _write_off_disable_free_s)
FSM_GC_CREATE_STATE(write_off_disable_s,      _write_off_disable_s)
FSM_GC_CREATE_STATE(write_off_block_free_s,   _write_off_block_free_s)
FSM_GC_CREATE_STATE(write_off_block1_s,       _write_off_block1_s)
FSM_GC_CREATE_STATE(write_off_block2_s,       _write_off_block2_s)

FSM_GC_CREATE_ACTION(write_off_disable_free_a, _write_off_disable_free_a)
FSM_GC_CREATE_ACTION(write_off_disable_a,      _write_off_disable_a)
FSM_GC_CREATE_ACTION(write_off_block_free_a,   _write_off_block_free_a)
FSM_GC_CREATE_ACTION(write_off_block1_a,       _write_off_block1_a)
FSM_GC_CREATE_ACTION(write_off_block2_a,       _write_off_block2_a)
FSM_GC_CREATE_ACTION(write_off_success_a,      _write_off_success_a)
FSM_GC_CREATE_ACTION(write_off_error_a,        _write_off_error_a)

FSM_GC_CREATE_TABLE(
	w25qxx_write_off_fsm_table,
	// if free uses
	{&write_off_init_s,         &success_e,  &write_off_disable_free_s, &write_off_disable_free_a},

	{&write_off_disable_free_s, &success_e,  &write_off_disable_s,      &write_off_disable_a},
    {&write_off_disable_free_s, &error_e,    &write_off_init_s,         &write_off_error_a},

    {&write_off_disable_s,      &transmit_e, &write_off_block_free_s,   &write_off_block_free_a},
    {&write_off_disable_s,      &error_e,    &write_off_init_s,         &write_off_error_a},

	{&write_off_block_free_s,   &success_e,  &write_off_block1_s,       &write_off_block1_a},
    {&write_off_block_free_s,   &error_e,    &write_off_init_s,         &write_off_error_a},
    
    {&write_off_block1_s,       &transmit_e, &write_off_block2_s,       &write_off_block2_a},
    {&write_off_block1_s,       &error_e,    &write_off_init_s,         &write_off_error_a},
    
    {&write_off_block2_s,       &transmit_e, &write_off_init_s,         &write_off_success_a},
    {&write_off_block2_s,       &error_e,    &write_off_init_s,         &write_off_error_a},
	// else
	// {&write_off_init_s,         &success_e,  &write_off_disable_s,      &write_off_disable_a},

    // {&write_off_disable_s,      &transmit_e, &write_off_block1_s,       &write_off_block1_a},
    // {&write_off_disable_s,      &error_e,    &write_off_init_s,         &write_off_error_a},
    
    // {&write_off_block1_s,       &transmit_e, &write_off_block2_s,       &write_off_block2_a},
    // {&write_off_block1_s,       &error_e,    &write_off_init_s,         &write_off_error_a},
    
    // {&write_off_block2_s,       &transmit_e, &write_off_init_s,         &write_off_success_a},
    // {&write_off_block2_s,       &error_e,    &write_off_init_s,         &write_off_error_a},
	//endif
)

void _write_off_a(void) {}

void _write_off_s(void) 
{
	static bool initialized = false;
	if (!initialized) {
		fsm_gc_init(&w25qxx_write_off_fsm, w25qxx_write_off_fsm_table, __arr_len(w25qxx_write_off_fsm_table));
		initialized = true;
	}
	fsm_gc_process(&w25qxx_write_off_fsm);
}

void _write_off_init_s(void)
{
    _W25Q_CS_reset();
    fsm_gc_clear(&w25qxx_write_off_fsm);
    fsm_gc_push_event(&w25qxx_write_off_fsm, &success_e);
}

void _write_off_disable_free_a(void)
{
	_w25q_route(W25Q_DMA_FREE);
}

void _write_off_disable_free_s(void)
{
    if (w25q.result == FLASH_OK) {
        fsm_gc_push_event(&w25qxx_write_off_fsm, &success_e);
    } else {
        fsm_gc_push_event(&w25qxx_write_off_fsm, &error_e);
    }
}

void _write_off_disable_a(void)
{
    _W25Q_CS_set();
	fsm_gc_clear(&w25qxx_write_off_fsm);
    w25q.cmd[0] = W25Q_CMD_WRITE_DISABLE;
	if (_w25q_tx(w25q_dma.cmd, 1)) {
		fsm_gc_push_event(&w25qxx_write_off_fsm, &error_e);
	}
	gtimer_start(&w25q_dma.timer, W25Q_SPI_TIMEOUT_MS);
}

void _write_off_disable_s(void)
{
    if (!gtimer_wait(&w25q_dma.timer)) {
        fsm_gc_push_event(&w25qxx_write_off_fsm, &error_e);
    }
}

void _write_off_block_free_a(void)
{
	_w25q_route(W25Q_DMA_FREE);
}

void _write_off_block_free_s(void)
{
    if (w25q.result == FLASH_OK) {
        fsm_gc_push_event(&w25qxx_write_off_fsm, &success_e);
    } else {
        fsm_gc_push_event(&w25qxx_write_off_fsm, &error_e);
    }
}

void _write_off_block1_a(void)
{
    _W25Q_CS_set();
	fsm_gc_clear(&w25qxx_write_off_fsm);
    w25q.cmd[0] = W25Q_CMD_WRITE_ENABLE_SR;
	if (_w25q_tx(w25q_dma.cmd, 1)) {
		fsm_gc_push_event(&w25qxx_write_off_fsm, &error_e);
	}
	gtimer_start(&w25q_dma.timer, W25Q_SPI_TIMEOUT_MS);
}

void _write_off_block1_s(void)
{
	if (!gtimer_wait(&w25q_dma.timer)) {
		fsm_gc_push_event(&w25qxx_write_off_fsm, &error_e);
	}
}

void _write_off_block2_a(void)
{
    _W25Q_CS_set();
	fsm_gc_clear(&w25qxx_write_off_fsm);
    w25q.cmd[0] = W25Q_CMD_WRITE_SR1;
    w25q.cmd[1] = ((W25Q_SR1_BLOCK_VALUE & 0x0F) << 2);
	if (_w25q_tx(w25q_dma.cmd, 2)) {
		fsm_gc_push_event(&w25qxx_write_off_fsm, &error_e);
	}
	gtimer_start(&w25q_dma.timer, W25Q_SPI_TIMEOUT_MS);
}

void _write_off_block2_s(void)
{
	if (!gtimer_wait(&w25q_dma.timer)) {
		fsm_gc_push_event(&w25qxx_write_off_fsm, &error_e);
	}
}

void _write_off_success_a(void)
{
    w25q_dma.result = FLASH_OK;
	fsm_gc_clear(&w25qxx_write_off_fsm);
	fsm_gc_push_event(&w25qxx_fsm, &success_e);
}

void _write_off_error_a(void)
{
    if (w25q_dma.result != FLASH_OK) {
        w25q_dma.result = FLASH_ERROR;
    }
	fsm_gc_clear(&w25qxx_write_off_fsm);
	fsm_gc_push_event(&w25qxx_fsm, &error_e);
}


FSM_GC_CREATE_STATE(read_init_s, _read_init_s)
FSM_GC_CREATE_STATE(read_free_s, _read_free_s)
FSM_GC_CREATE_STATE(read_send_s, _read_send_s)

FSM_GC_CREATE_ACTION(read_free_a,    _read_free_a)
FSM_GC_CREATE_ACTION(read_send_tx_a, _read_send_tx_a)
FSM_GC_CREATE_ACTION(read_send_rx_a, _read_send_rx_a)
FSM_GC_CREATE_ACTION(read_error_a,   _read_error_a)
FSM_GC_CREATE_ACTION(read_success_a, _read_success_a)

FSM_GC_CREATE_TABLE(
	w25qxx_read_fsm_table,
	{&read_init_s, &success_e,  &read_free_s, &read_free_a},

	{&read_free_s, &success_e,  &read_send_s, &read_send_tx_a},
	{&read_free_s, &error_e,    &read_init_s, &read_error_a},

	{&read_send_s, &transmit_e, &read_send_s, &read_send_rx_a},
	{&read_send_s, &receive_e,  &read_init_s, &read_success_a},
	{&read_send_s, &error_e,    &read_init_s, &read_error_a},
)

void _read_a(void) {}

void _read_s(void)
{
	static bool initialized = false;
	if (!initialized) {
		fsm_gc_init(&w25qxx_read_fsm, w25qxx_read_fsm_table, __arr_len(w25qxx_read_fsm_table));
		initialized = true;
	}
	fsm_gc_process(&w25qxx_read_fsm);
}

void _read_init_s(void)
{
    _W25Q_CS_reset();
    fsm_gc_clear(&w25qxx_read_fsm);
    fsm_gc_push_event(&w25qxx_read_fsm, &success_e);
}

void _read_free_a(void)
{
	_w25q_route(W25Q_DMA_FREE);
}

void _read_free_s(void) 
{
    if (w25q_dma.result == FLASH_OK) {
        fsm_gc_push_event(&w25qxx_read_fsm, &success_e);    
    } else {
        fsm_gc_push_event(&w25qxx_read_fsm, &error_e);
	}
}

void _read_send_tx_a(void)
{
    _W25Q_CS_set();
	fsm_gc_clear(&w25qxx_read_fsm);
    uint8_t counter = 0;
    w25q.cmd[counter++] = W25Q_CMD_READ;
    if (_w25q_24bit()) {
        w25q.cmd[counter++] = (uint8_t)(addr >> 24) & 0xFF;
    }
    w25q.cmd[counter++] = (addr >> 16) & 0xFF;
    w25q.cmd[counter++] = (addr >> 8) & 0xFF;
    w25q.cmd[counter++] = addr & 0xFF;
	if (_w25q_tx(w25q.cmd, counter)) {
		fsm_gc_push_event(&w25qxx_read_fsm, &error_e);
	}
	gtimer_start(&w25q_dma.timer, W25Q_SPI_TIMEOUT_MS);
}

void _read_send_rx_a(void)
{
	fsm_gc_clear(&w25qxx_read_fsm);
	if (_w25q_rx(w25q_dma.rx_ptr, w25q_dma.len)) {
		fsm_gc_push_event(&w25qxx_read_fsm, &error_e);
	}
	gtimer_start(&w25q_dma.timer, W25Q_SPI_TIMEOUT_MS);
}

void _read_send_s(void)
{
	if (!gtimer_wait(&w25q_dma.timer)) {
		fsm_gc_push_event(&w25qxx_read_fsm, &error_e);
	}
}

void _read_success_a(void)
{
    w25q_dma.result = FLASH_OK;
	fsm_gc_clear(&w25qxx_read_fsm);
	fsm_gc_push_event(&w25qxx_fsm, &success_e);
}

void _read_error_a(void)
{
    if (w25q_dma.result != FLASH_OK) {
        w25q_dma.result = FLASH_ERROR;
    }
	fsm_gc_clear(&w25qxx_read_fsm);
	fsm_gc_push_event(&w25qxx_fsm, &error_e);
}


FSM_GC_CREATE_STATE(write_init_s,         _write_init_s)
FSM_GC_CREATE_STATE(write_cmp_s,          _write_cmp_s)
FSM_GC_CREATE_STATE(write_erase_s,        _write_erase_s)
FSM_GC_CREATE_STATE(write_enable_s,       _write_enable_s)
FSM_GC_CREATE_STATE(write_cmd_free_s,     _write_cmd_free_s)
FSM_GC_CREATE_STATE(write_cmd_s,          _write_cmd_s)
FSM_GC_CREATE_STATE(write_data_free_s,    _write_data_free_s)
FSM_GC_CREATE_STATE(write_data_s,         _write_data_s)
FSM_GC_CREATE_STATE(write_disable_s,      _write_disable_s)

FSM_GC_CREATE_ACTION(write_cmp_a,         _write_cmp_a)
FSM_GC_CREATE_ACTION(write_erase_a,       _write_erase_a)
FSM_GC_CREATE_ACTION(write_enable_a,      _write_enable_a)
FSM_GC_CREATE_ACTION(write_cmd_free_a,    _write_cmd_free_a)
FSM_GC_CREATE_ACTION(write_cmd_a,         _write_cmd_a)
FSM_GC_CREATE_ACTION(write_data_free_a,   _write_data_free_a)
FSM_GC_CREATE_ACTION(write_data_a,        _write_data_a)
FSM_GC_CREATE_ACTION(write_disable_a,     _write_disable_a)
FSM_GC_CREATE_ACTION(write_error_a,       _write_error_a)
FSM_GC_CREATE_ACTION(write_success_a,     _write_success_a)

FSM_GC_CREATE_TABLE(
	w25qxx_write_fsm_table,
	{&write_init_s,         &success_e,  &write_cmp_s,          &write_cmp_a},

	{&write_cmp_s,          &success_e,  &write_init_s,         &write_success_a},
	{&write_cmp_s,          &next_e,     &write_erase_s,        &write_success_a},
	{&write_cmp_s,          &write_e,    &write_erase_s,        &write_erase_a},
	{&write_cmp_s,          &erase_e,    &write_enable_s,       &write_enable_a},
	{&write_cmp_s,          &error_e,    &write_init_s,         &write_error_a},

	{&write_erase_s,        &success_e,  &write_enable_s,       &write_enable_a},
	{&write_erase_s,        &erase_e,    &write_enable_s,       &write_enable_a},
	{&write_erase_s,        &error_e,    &write_init_s,         &write_error_a},

    {&write_enable_s,       &success_e,  &write_cmd_free_s,     &write_cmd_free_a},
    {&write_enable_s,       &error_e,    &write_init_s,         &write_error_a},

	{&write_cmd_free_s,     &success_e,  &write_cmd_s,          &write_cmd_a},
    {&write_cmd_free_s,     &error_e,    &write_init_s,         &write_error_a},

	{&write_cmd_s,          &transmit_e, &write_data_free_s,    &write_data_free_a},
    {&write_cmd_s,          &error_e,    &write_init_s,         &write_error_a},

	{&write_data_free_s,    &success_e,  &write_data_s,         &write_data_a},
    {&write_data_free_s,    &error_e,    &write_init_s,         &write_error_a},

	{&write_data_s,         &transmit_e, &write_disable_s,      &write_disable_a},
    {&write_data_s,         &error_e,    &write_init_s,         &write_error_a},

    {&write_disable_s,      &transmit_e, &write_cmp_s,          &write_cmp_a},
    {&write_disable_s,      &error_e,    &write_init_s,         &write_error_a},
)

void _write_a(void) {}

void _write_s(void)
{
	static bool initialized = false;
	if (!initialized) {
		fsm_gc_init(&w25qxx_write_fsm, w25qxx_write_fsm_table, __arr_len(w25qxx_write_fsm_table));
		initialized = true;
	}
	fsm_gc_process(&w25qxx_write_fsm);
}

void _write_init_s(void)
{
    _W25Q_CS_reset();
    fsm_gc_clear(&w25qxx_erase_fsm);
	fsm_gc_push_event(&w25qxx_write_fsm, &success_e);
	w25q_dma.write_done = false;
}

void _write_cmp_a(void)
{
	w25q_dma.rx_ptr = w25q_dma.write_erase ? w25q_dma.write_buf : w25q_dma.buffer;
	_w25q_route(W25Q_DMA_READ);
}

void _write_cmp_s(void)
{
	uint8_t* ptr = w25q_dma.write_erase ? w25q_dma.write_ptr : w25q_dma.tx_ptr;
	uint32_t len = w25q_dma.write_erase ? W25Q_PAGE_SIZE : w25q_dma.len;
	if (!memcmp(w25q_dma.rx_ptr, ptr, len)) {
		if (w25q_dma.write_erase) {
			fsm_gc_push_event(&w25qxx_write_fsm, &next_e);
		} else {
			fsm_gc_push_event(&w25qxx_write_fsm, &success_e);
		}
		return;
	}
	if (!w25q_dma.write_done) {
		fsm_gc_push_event(&w25qxx_write_fsm, &success_e);
		return;
	}
	bool need_erase = false;
	for (unsigned i = 0; i < w25q_dma.len; i++) {
		if (w25q_dma.buffer[i] != 0xFF) {
			need_erase = true;
			break;
		}
	}
	if (need_erase) {
		fsm_gc_push_event(&w25qxx_write_fsm, &erase_e);
		return;
	}
	fsm_gc_push_event(&w25qxx_write_fsm, &write_e);
}

void _write_erase_a(void)
{
    w25q_dma.erase_cnt  = 1;
	w25q_dma.erase_addr = &w25q_dma.addr;
	_w25q_route(W25Q_DMA_ERASE);
}

void _write_erase_s(void)
{
	if (w25q_dma.write_erase) {
        fsm_gc_push_event(&w25qxx_write_fsm, &erase_e);
    } else if (w25q.result == FLASH_OK) {
        fsm_gc_push_event(&w25qxx_write_fsm, &success_e);
    } else {
        fsm_gc_push_event(&w25qxx_write_fsm, &error_e);
    }
}

void _write_enable_a(void)
{
	_w25q_route(W25Q_DMA_WRITE_ON);
}

void _write_enable_s(void)
{
    if (w25q.result == FLASH_OK) {
        fsm_gc_push_event(&w25qxx_write_fsm, &success_e);
    } else {
        fsm_gc_push_event(&w25qxx_write_fsm, &error_e);
    }
}

void _write_cmd_free_a(void)
{
	_w25q_route(W25Q_DMA_FREE);
}

void _write_cmd_free_s(void)
{
    if (w25q.result == FLASH_OK &&
        w25q.sr1 & W25Q_SR1_WEL
    ) {
        fsm_gc_push_event(&w25qxx_write_fsm, &success_e);
    } else {
        fsm_gc_push_event(&w25qxx_write_fsm, &error_e);
    }
}

void _write_cmd_a(void)
{
    _W25Q_CS_set();
    fsm_gc_clear(&w25qxx_write_fsm);
    uint8_t counter = 0;
    w25q.cmd[counter++] = W25Q_CMD_PAGE_PROGRAMM;
	uint32_t addr = w25q_dma.write_erase ? w25q_dma.write_addr : w25q_dma.addr;
    if (_w25q_24bit()) {
        w25q.cmd[counter++] = (uint8_t)(addr >> 24) & 0xFF;
    }
    w25q.cmd[counter++] = (addr >> 16) & 0xFF;
    w25q.cmd[counter++] = (addr >> 8) & 0xFF;
    w25q.cmd[counter++] = addr & 0xFF;
	if (_w25q_tx(w25q_dma.cmd, 1)) {
		fsm_gc_push_event(&w25qxx_write_fsm, &error_e);
	}
	gtimer_start(&w25q_dma.timer, W25Q_SPI_TIMEOUT_MS);
}

void _write_cmd_s(void)
{
    if (!gtimer_wait(&w25q_dma.timer)) {
        fsm_gc_push_event(&w25qxx_write_fsm, &error_e);
    }
}

void _write_data_free_a(void)
{
	_w25q_route(W25Q_DMA_FREE);
}

void _write_data_free_s(void)
{
    if (w25q.result == FLASH_OK) {
        fsm_gc_push_event(&w25qxx_write_fsm, &success_e);
    } else {
        fsm_gc_push_event(&w25qxx_write_fsm, &error_e);
    }
}

void _write_data_a(void)
{
    _W25Q_CS_set();
    fsm_gc_clear(&w25qxx_write_fsm);
	uint8_t* ptr = w25q_dma.write_erase ? w25q_dma.write_ptr : w25q_dma.tx_ptr;
	uint32_t len = w25q_dma.write_erase ? w25q_dma.write_len : W25Q_PAGE_SIZE;
	if (_w25q_tx(ptr, len)) {
		fsm_gc_push_event(&w25qxx_write_fsm, &error_e);
	}
	gtimer_start(&w25q_dma.timer, W25Q_SPI_TIMEOUT_MS);
}

void _write_data_s(void)
{
    if (!gtimer_wait(&w25q_dma.timer)) {
        fsm_gc_push_event(&w25qxx_write_fsm, &error_e);
    }
}

void _write_disable_a(void)
{
	_w25q_route(W25Q_DMA_WRITE_OFF);
}

void _write_disable_s(void)
{
    if (w25q.result == FLASH_OK) {
		w25q_dma.write_done = true;
		fsm_gc_push_event(&w25qxx_fsm, &success_e);
	} else {
		fsm_gc_push_event(&w25qxx_fsm, &error_e);
	}
}

void _write_success_a(void)
{
    w25q_dma.result = FLASH_OK;
	w25q_dma.write_erase = false;
	fsm_gc_clear(&w25qxx_write_fsm);
	fsm_gc_push_event(&w25qxx_fsm, &success_e);
}

void _write_error_a(void)
{
    if (w25q_dma.result != FLASH_OK) {
        w25q_dma.result = FLASH_ERROR;
    }
	w25q_dma.write_erase = false;
	fsm_gc_clear(&w25qxx_write_fsm);
	fsm_gc_push_event(&w25qxx_fsm, &error_e);
}


FSM_GC_CREATE_STATE(erase_init_s,        _erase_init_s)
FSM_GC_CREATE_STATE(erase_loop_s,        _erase_loop_s)
FSM_GC_CREATE_STATE(erase_read_s,        _erase_read_s)
FSM_GC_CREATE_STATE(erase_enable_s,      _erase_enable_s)
FSM_GC_CREATE_STATE(erase_erase_s,       _erase_erase_s)
FSM_GC_CREATE_STATE(erase_disable_s,     _erase_disable_s)
FSM_GC_CREATE_STATE(erase_repair_free_s, _erase_repair_free_s)
FSM_GC_CREATE_STATE(erase_repair_s,      _erase_repair_s)

FSM_GC_CREATE_ACTION(erase_loop_a,        _erase_loop_a)
FSM_GC_CREATE_ACTION(erase_iter_a,        _erase_iter_a)
FSM_GC_CREATE_ACTION(erase_read_a,        _erase_read_a)
FSM_GC_CREATE_ACTION(erase_enable_a,      _erase_enable_a)
FSM_GC_CREATE_ACTION(erase_erase_a,       _erase_erase_a)
FSM_GC_CREATE_ACTION(erase_disable_a,     _erase_disable_a)
FSM_GC_CREATE_ACTION(erase_success_a,     _erase_success_a)
FSM_GC_CREATE_ACTION(erase_error_a,       _erase_error_a)
FSM_GC_CREATE_ACTION(erase_repair_free_a, _erase_repair_free_a)
FSM_GC_CREATE_ACTION(erase_repair_a,      _erase_repair_a)
FSM_GC_CREATE_ACTION(erase_repair_iter_a, _erase_repair_iter_a)


FSM_GC_CREATE_TABLE(
	w25qxx_erase_fsm_table,
	{&erase_init_s,         &success_e,  &erase_loop_s,          &erase_loop_a},

    {&erase_loop_s,         &success_e,  &erase_init_s,          &erase_success_a},
    {&erase_loop_s,         &next_e,     &erase_read_s,          &erase_read_a},

    {&erase_read_s,         &success_e,  &erase_loop_s,          &erase_iter_a},
    {&erase_read_s,         &erase_e,    &erase_erase_s,         &erase_erase_a},
    {&erase_read_s,         &error_e,    &erase_init_s,          &erase_error_a},

	{&erase_enable_s,       &success_e,  &erase_erase_s,         &erase_erase_a},
	{&erase_enable_s,       &error_e,    &erase_init_s,          &erase_error_a},

	{&erase_erase_s,        &transmit_e, &erase_disable_s,       &erase_disable_a},
	{&erase_erase_s,        &error_e,    &erase_init_s,          &erase_error_a},

	{&erase_disable_s,      &success_e,  &erase_repair_free_s,   &erase_repair_free_a},
	{&erase_disable_s,      &error_e,    &erase_init_s,          &erase_error_a},

	{&erase_repair_free_s,  &success_e,  &erase_repair_s,        &erase_repair_a},
	{&erase_repair_free_s,  &error_e,    &erase_init_s,          &erase_error_a},

	{&erase_repair_s,       &success_e,  &erase_loop_s,          &erase_iter_a},
	{&erase_repair_s,       &write_e,    &erase_repair_s,        &erase_repair_iter_a}|,
	{&erase_repair_s,       &error_e,    &erase_init_s,          &erase_error_a},
)

void _erase_a(void) {}

void _erase_s(void)
{
	static bool initialized = false;
	if (!initialized) {
		fsm_gc_init(&w25qxx_erase_fsm_table, w25qxx_erase_fsm_table, __arr_len(w25qxx_erase_fsm_table));
		initialized = true;
	}
	fsm_gc_process(&w25qxx_erase_fsm_table);
}

void _erase_init_s(void)
{
    _W25Q_CS_reset();
    fsm_gc_clear(&w25qxx_erase_fsm);
    fsm_gc_push_event(&w25qxx_erase_fsm, &success_e);
}

void _erase_loop_a(void)
{
    w25q_dma.count = 0;
    fsm_gc_clear(&w25qxx_erase_fsm);
}

void _erase_iter_a(void)
{
	if (w25q_dma.count + 1 >= w25q_dma.erase_cnt) {
		return;
	}
	bool next_found = false;
	uint32_t last_sector_addr = (w25q_dma.erase_addr[w25q_dma.count++] / W25Q_SECTOR_SIZE) * W25Q_SECTOR_SIZE;
	for (uint32_t i = w25q_dma.count; i < w25q_dma.count; i++) {
		uint32_t sector_addr = (w25q_dma.erase_addr[i] / W25Q_SECTOR_SIZE) * W25Q_SECTOR_SIZE;
		if (last_sector_addr != sector_addr) {
			next_found = true;
			w25q_dma.count = i;
			break;
		}
	}
	if (!next_found) {
		w25q_dma.count = w25q_dma.erase_cnt;
	}
}

void _erase_loop_s(void)
{
    if (w25q_dma.count + 1 >= w25q_dma.erase_cnt) {
        fsm_gc_push_event(&w25qxx_erase_fsm, &success_e);
    } else {
        fsm_gc_push_event(&w25qxx_erase_fsm, &next_e);
    }
}

void _erase_read_a(void)
{
	uint32_t cur_sector_addr = cur_sector_idx * W25Q_SECTOR_SIZE;

    w25q_dma.rx_ptr = w25q_dma.buffer;
    w25q_dma.len    = W25Q_SECTOR_SIZE;
	w25q_dma.addr   = cur_sector_addr;
	_w25q_route(W25Q_DMA_READ);
}

void _erase_read_s(void)
{
    if (w25q.result != FLASH_OK) {
        fsm_gc_push_event(&w25qxx_erase_fsm, &error_e);
		return;
    }

	bool need_erase = false;
	for (unsigned i = 0; i < w25q.erase_cnt && !need_erase; i++) {
		uint32_t addr_in_sector = w25q.erase_addr[i] % W25Q_SECTOR_SIZE;
		for (unsigned j = 0; j < W25Q_PAGE_SIZE; j++) {
			if (w25q_dma.buffer[j] != 0xFF) {
				need_erase = true;
				break;
			}
		}
	}
	if (need_erase) {
		fsm_gc_push_event(&w25qxx_erase_fsm, &erase_e);
	} else {
		fsm_gc_push_event(&w25qxx_erase_fsm, &success_e);
	}	
	// fsm_gc_push_event(&w25qxx_erase_fsm, &success_e);
	
	// uint32_t cur_sector_idx  = addrs[i] / W25Q_SECTOR_SIZE;
	// uint32_t cur_sector_addr = cur_sector_idx * W25Q_SECTOR_SIZE;

	// /* Addresses for erase in current sector BEGIN */
	// uint32_t next_sector_i = 0;
	// for (uint32_t i = w25q_dma.counter; i < w25q_dma.erase_cnt; i++) {
	// 	if (addrs[i] / W25Q_SECTOR_SIZE != cur_sector_idx) {
	// 		next_sector_i = i;
	// 		break;
	// 	}
	// }
	// if (!next_sector_i) {
	// 	next_sector_i = w25q_dma.erase_cnt;
	// }
	// /* Addresses for erase in current sector END */
}

void _erase_enable_a(void)
{
	_w25q_route(W25Q_DMA_WRITE_ON);
}

void _erase_enable_s(void)
{
    if (w25q.result == FLASH_OK) {
        fsm_gc_push_event(&w25qxx_erase_fsm, &success_e);
    } else {
        fsm_gc_push_event(&w25qxx_erase_fsm, &error_e);
    }
}

void _erase_erase_a(void)
{
	_W25Q_CS_set();
    fsm_gc_clear(&w25qxx_erase_fsm);
    uint8_t counter = 0;
    w25q.cmd[counter++] = W25Q_CMD_ERASE_SECTOR;
    if (_w25q_24bit()) {
        w25q.cmd[counter++] = (uint8_t)(addr >> 24) & 0xFF;
    }
    w25q.cmd[counter++] = (addr >> 16) & 0xFF;
    w25q.cmd[counter++] = (addr >> 8) & 0xFF;
    w25q.cmd[counter++] = addr & 0xFF;
	if (_w25q_tx(w25q_dma.cmd, 1)) {
		fsm_gc_push_event(&w25qxx_erase_fsm, &error_e);
	} 
	gtimer_start(&w25q_dma.timer, W25Q_SPI_ERASE_TIMEOUT_MS);
}

void _erase_erase_s(void)
{
    if (!gtimer_wait(&w25q_dma.timer)) {
        fsm_gc_push_event(&w25qxx_erase_fsm, &error_e);
    }
}

void _erase_disable_a(void)
{
	_w25q_route(W25Q_DMA_WRITE_OFF);
}

void _erase_disable_s(void)
{
    if (w25q.result == FLASH_OK) {
        fsm_gc_push_event(&w25qxx_erase_fsm, &success_e);
    } else {
        fsm_gc_push_event(&w25qxx_erase_fsm, &error_e);
    }
}

void _erase_repair_free_a(void)
{
	_w25q_route(W25Q_DMA_FREE);
}

void _erase_repair_free_s(void)
{
    if (w25q.result == FLASH_OK) {
		fsm_gc_push_event(&w25qxx_erase_fsm, &success_e);
	} else {
		fsm_gc_push_event(&w25qxx_erase_fsm, &error_e);
	}
}

void _erase_repair_a(void)
{
	for (unsigned i = 0; i < w25q_dma.erase_cnt; i++) {	
		memset(
			w25q_dma.buffer + (w25q_dma.erase_addr[i] % W25Q_SECTOR_SIZE),
			0xFF, 
			W25Q_PAGE_SIZE
		);
	}

	bool found = false;
	uint32_t first_addr = 0;
	for (unsigned i = first_addr; i < W25Q_SECTOR_SIZE; i++) {
		if (w25q_dma.buffer[i] != 0xFF) {
			first_addr = (i / W25Q_PAGE_SIZE) * W25Q_PAGE_SIZE;
			found = true;
			break;
		}
	}

	if (!found) {
		fsm_gc_push_event(&w25qxx_fsm, &success_e);
		return;
	}

	w25q_dma.erase_iter  = first_addr;

	w25q_dma.write_erase = true;
	w25q_dma.write_addr  = first_addr;
	w25q_dma.write_ptr   = w25q_dma.buffer + first_addr;
	_w25q_route(W25Q_DMA_WRITE);
}

void _erase_repair_iter_a(void)
{
	bool found = false;
	uint32_t addr = w25q_dma.erase_iter;
	for (unsigned i = addr; i < W25Q_SECTOR_SIZE; i++) {
		if (w25q_dma.buffer[i] != 0xFF) {
			addr = (i / W25Q_PAGE_SIZE) * W25Q_PAGE_SIZE;
			found = true;
			break;
		}
	}

	if (!found) {
		fsm_gc_push_event(&w25qxx_fsm, &success_e);
		return;
	}

	w25q_dma.erase_iter  = addr;

	w25q_dma.write_erase = true;
	w25q_dma.write_addr  = addr;
	w25q_dma.write_ptr   = w25q_dma.buffer + addr;
	_w25q_route(W25Q_DMA_WRITE);
}

void _erase_repair_s(void)
{
    if (w25q.result == FLASH_OK) {
		fsm_gc_push_event(&w25qxx_erase_fsm, &write_e);
	} else {
		fsm_gc_push_event(&w25qxx_erase_fsm, &error_e);
	}
}

void _erase_success_a(void)
{
    w25q_dma.result = FLASH_OK;
	fsm_gc_clear(&w25qxx_erase_fsm);
	fsm_gc_push_event(&w25qxx_fsm, &success_e);
}

void _erase_error_a(void)
{
    if (w25q_dma.result != FLASH_OK) {
        w25q_dma.result = FLASH_ERROR;
    }
	fsm_gc_clear(&w25qxx_erase_fsm);
	fsm_gc_push_event(&w25qxx_fsm, &error_e);
}






#endif