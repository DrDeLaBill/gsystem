/* Copyright © 2025 Georgy E. All rights reserved. */

#include "w25qxx.h"


#include "gdefines.h"
#include "gconfig.h"


#if defined(GSYSTEM_FLASH_MODE) && defined(GSYSTEM_MEMORY_DMA)


#include "glog.h"
#include "fsm_gc.h"
#include "hal_defs.h"
#include "circle_buf_gc.h"


#define W25Q_SPI_TIMEOUT_MS       ((uint32_t)100)
#define W25Q_SPI_ERASE_TIMEOUT_MS ((uint32_t)SECOND_MS)
#define W25Q_SPI_WRITE_TIMEOUT_MS ((uint32_t)SECOND_MS)
#define W25Q_SPI_COMMAND_SIZE_MAX ((uint8_t)10)
#define W25Q_SPI_ATTEMPTS_CNT     (15)


typedef enum _dma_status_t {
    W25Q_DMA_READY,
    W25Q_DMA_READ,
    W25Q_DMA_WRITE,
    W25Q_DMA_ERASE,
    W25Q_DMA_FREE,
    W25Q_DMA_WRITE_OFF,
    W25Q_DMA_WRITE_ON,
} dma_status_t;

typedef struct _w25q_route_t {
	dma_status_t status;
	uint32_t     addr;
	uint32_t     len;
	uint32_t     cnt;
	uint32_t     tmp;
	uint8_t*     rx_ptr;
	uint8_t*     tx_ptr;
} w25q_route_t;

typedef struct _w25q_dma_t {
    circle_buf_gc_t queue;
    w25q_route_t    _queue[10];

    uint8_t         buffer1[W25Q_SECTOR_SIZE];
    uint8_t         buffer2[W25Q_PAGE_SIZE];
    uint32_t        addrs1[W25Q_SECTOR_SIZE / W25Q_PAGE_SIZE];

    gtimer_t        timer;
    uint8_t         cmd[W25Q_SPI_COMMAND_SIZE_MAX];
    uint8_t         sr1;
    flash_status_t  result;
} w25q_dma_t;


bool        _w25q_ready();
uint8_t     _w25q_make_addr(uint8_t* buf, uint32_t addr);

static void _write_dma_internal_callback(const flash_status_t status);
static void _read_dma_internal_callback(const flash_status_t status);
static void _erase_dma_internal_callback(const flash_status_t status);

static void _w25q_route(w25q_route_t route);
static void _w25q_route_res(fsm_gc_t* fsm);

static bool _w25q_tx(const uint8_t* data, const uint32_t len);
static bool _w25q_rx(uint8_t* data, const uint32_t len);
static void _w25q_abort();

static void _w25q_queue_push(w25q_route_t route);
static w25q_route_t _w25q_queue_pop();
static w25q_route_t* _w25q_queue_back();

static bool _w25q_route_call();

#ifdef GSYSTEM_BEDUG
extern const char W25Q_TAG[];
#endif
extern SPI_HandleTypeDef GSYSTEM_FLASH_SPI;

extern bool     _w25q_initialized();
extern bool     _w25q_24bit();
extern void     _W25Q_CS_set();
extern void     _W25Q_CS_reset();

static w25q_dma_t w25q_dma = {
    .queue        = {0},
	._queue       = {{0}},

	.buffer1      = {0},
	.buffer2      = {0},
	.addrs1       = {0},

    .timer        = {0},
    .cmd          = {0},
    .sr1          = 0,
    .result       = FLASH_OK,
};


FSM_GC_CREATE(w25qxx_fsm)
FSM_GC_CREATE(w25qxx_free_fsm)
FSM_GC_CREATE(w25qxx_write_on_fsm)
FSM_GC_CREATE(w25qxx_write_off_fsm)
FSM_GC_CREATE(w25qxx_read_fsm)
FSM_GC_CREATE(w25qxx_write_fsm)
FSM_GC_CREATE(w25qxx_erase_fsm)

FSM_GC_CREATE_EVENT(success_e,   0)
FSM_GC_CREATE_EVENT(router_e,    0)
FSM_GC_CREATE_EVENT(done_e,      0)
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
    {&init_s,      &done_e,      &idle_s,      &idle_a},

    {&idle_s,      &router_e,    &router_s,    &router_a},

    {&router_s,    &read_e,      &read_s,      &read_a},
    {&router_s,    &write_e,     &write_s,     &write_a},
    {&router_s,    &erase_e,     &erase_s,     &erase_a},
    {&router_s,    &free_e,      &free_s,      &free_a},
    {&router_s,    &write_on_e,  &write_on_s,  &write_on_a},
    {&router_s,    &write_off_e, &write_off_s, &write_off_a},
    {&router_s,    &error_e,     &idle_s,      &idle_a},

    {&free_s,      &done_e,      &idle_s,      &callback_a},
    {&free_s,      &router_e,    &router_s,    &router_a},

    {&write_on_s,  &done_e,      &idle_s,      &callback_a},
    {&write_on_s,  &router_e,    &router_s,    &router_a},

    {&write_off_s, &done_e,      &idle_s,      &callback_a},
    {&write_off_s, &router_e,    &router_s,    &router_a},

    {&read_s,      &done_e   ,   &idle_s,      &callback_a},
    {&read_s,      &router_e,    &router_s,    &router_a},

    {&write_s,     &done_e,      &idle_s,      &callback_a},
    {&write_s,     &router_e,    &router_s,    &router_a},

    {&erase_s,     &done_e,      &idle_s,      &callback_a},
    {&erase_s,     &router_e,    &router_s,    &router_a},
)


void w24qxx_tick()
{
    static bool initialized = false;
    if (!initialized) {
        circle_buf_gc_init(&w25q_dma.queue, (uint8_t*)&w25q_dma._queue, sizeof(*w25q_dma._queue), __arr_len(w25q_dma._queue));
        fsm_gc_init(&w25qxx_fsm, w25qxx_fsm_table, __arr_len(w25qxx_fsm_table));
        initialized = true;
    }
    fsm_gc_process(&w25qxx_fsm);
}

flash_status_t w25qxx_read_dma(const uint32_t addr, uint8_t* data, const uint16_t len)
{
    if (!_w25q_ready()) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash DMA read addr=%08lX len=%u (flash not ready)", addr, len);
#endif
        return FLASH_ERROR;
    }

    if (addr % W25Q_PAGE_SIZE) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash DMA read addr=%08lX len=%u (bad address)", addr, len);
#endif
        return FLASH_ERROR;
    }

    if (addr + len > w25qxx_size()) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash DMA read addr=%08lX len=%u: error (unacceptable address)", addr, len);
#endif
        return FLASH_OOM;
    }

    if (!data) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash DMA read addr=%08lX len=%u: error (NULL buffer)", addr, len);
#endif
        return FLASH_ERROR;
    }

#if W25Q_BEDUG
    printTagLog(W25Q_TAG, "read DMA address=%08X len=%u", (unsigned int)addr, len);
#endif

    w25q_route_t route = {
		.status = W25Q_DMA_READ,
		.addr   = addr,
		.len    = len,
		.rx_ptr = (uint8_t*)data
    };
    _w25q_queue_push(route);

    return FLASH_OK;
}

flash_status_t w25qxx_write_dma(const uint32_t addr, const uint8_t* data, const uint16_t len)
{
    if (!_w25q_ready()) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash DMA write addr=%08lX len=%u (flash not ready)", addr, len);
#endif
        return FLASH_ERROR;
    }

    if (addr % W25Q_PAGE_SIZE) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash DMA write addr=%08lX len=%u (bad address)", addr, len);
#endif
        return FLASH_ERROR;
    }

    if (addr + len > w25qxx_size()) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash DMA write addr=%08lX len=%u: error (unacceptable address)", addr, len);
#endif
        return FLASH_OOM;
    }

    if (!data) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash DMA write addr=%08lX len=%u: error (NULL buffer)", addr, len);
#endif
        return FLASH_ERROR;
    }

    if (len > sizeof(w25q_dma.buffer1)) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash DMA write addr=%08lX len=%u: error (overflow buffer)", addr, len);
#endif
        return FLASH_ERROR;
    }

#if W25Q_BEDUG
    printTagLog(W25Q_TAG, "write DMA address=%08X len=%u", (unsigned int)addr, len);
#endif

    w25q_route_t route = {
		.status = W25Q_DMA_WRITE,
		.addr   = addr,
		.len    = len,
		.rx_ptr = w25q_dma.buffer1,
		.tx_ptr = (uint8_t*)data,
		.cnt    = 0,
    };
    _w25q_queue_push(route);

    return FLASH_OK;
}

flash_status_t w25qxx_erase_addresses_dma(const uint32_t* addrs, const uint32_t count)
{
    if (!_w25q_ready()) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "erase flash addresses error: flash not ready");
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

    for (unsigned i = 0; i < count; i++) {
        if (addrs[i] % W25Q_PAGE_SIZE) {
#if W25Q_BEDUG
            printTagLog(W25Q_TAG, "flash DMA erase addr=%08lX index=%u (bad address)",addrs[i], i);
#endif
            return FLASH_ERROR;
        }
    }

#if W25Q_BEDUG
    printTagLog(W25Q_TAG, "erase DMA addresses: ")
    for (uint32_t i = 0; i < count; i++) {
        printPretty("%08lX\n", addrs[i]);
    }
#endif

    w25q_route_t route = {
		.status = W25Q_DMA_ERASE,
		.rx_ptr = w25q_dma.buffer1,
		.tx_ptr = (uint8_t*)addrs,
		.len    = count,
    };
    _w25q_queue_push(route);

    return FLASH_OK;
}

void w25qxx_stop_dma()
{
    fsm_gc_reset(&w25qxx_fsm);
    fsm_gc_reset(&w25qxx_free_fsm);
    fsm_gc_reset(&w25qxx_write_on_fsm);
    fsm_gc_reset(&w25qxx_write_off_fsm);
    fsm_gc_reset(&w25qxx_read_fsm);
    fsm_gc_reset(&w25qxx_write_fsm);
    fsm_gc_reset(&w25qxx_erase_fsm);
    _w25q_abort();
    _W25Q_CS_reset();
    w24qxx_tick();
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
        fsm_gc_push_event(&w25qxx_free_fsm, &transmit_e);
        break;
    case W25Q_DMA_WRITE_ON:
        fsm_gc_push_event(&w25qxx_write_on_fsm, &transmit_e);
        break;
    case W25Q_DMA_WRITE_OFF:
        fsm_gc_push_event(&w25qxx_write_off_fsm, &transmit_e);
        break;
    case W25Q_DMA_READY:
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
        fsm_gc_push_event(&w25qxx_free_fsm, &receive_e);
        break;
    case W25Q_DMA_WRITE_ON:
        fsm_gc_push_event(&w25qxx_write_on_fsm, &receive_e);
        break;
    case W25Q_DMA_WRITE_OFF:
        fsm_gc_push_event(&w25qxx_write_off_fsm, &receive_e);
        break;
    case W25Q_DMA_READY:
    default:
        fsm_gc_push_event(&w25qxx_fsm, &receive_e);
        break;
    }
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
        fsm_gc_push_event(&w25qxx_free_fsm, &error_e);
        break;
    case W25Q_DMA_WRITE_ON:
        fsm_gc_push_event(&w25qxx_write_on_fsm, &error_e);
        break;
    case W25Q_DMA_WRITE_OFF:
        fsm_gc_push_event(&w25qxx_write_off_fsm, &error_e);
        break;
    case W25Q_DMA_READY:
    default:
        fsm_gc_push_event(&w25qxx_fsm, &error_e);
        break;
    }
}

bool _w25q_tx(const uint8_t* data, const uint32_t len)
{
    _W25Q_CS_set();
    return HAL_SPI_Transmit_DMA(&GSYSTEM_FLASH_SPI, data, (uint16_t)len) == HAL_OK;
}

bool _w25q_rx(uint8_t* data, const uint32_t len)
{
    _W25Q_CS_set();
    return HAL_SPI_Receive_DMA(&GSYSTEM_FLASH_SPI, data, (uint16_t)len) == HAL_OK;
}

void _w25q_abort()
{
    HAL_SPI_AbortCpltCallback(&GSYSTEM_FLASH_SPI);
}

void _w25q_queue_push(w25q_route_t status)
{
    circle_buf_gc_push_back(&w25q_dma.queue, (uint8_t*)&status);
}

w25q_route_t _w25q_queue_pop()
{
    return *((w25q_route_t*)circle_buf_gc_pop_back(&w25q_dma.queue));
}

w25q_route_t* _w25q_queue_back()
{
    return (w25q_route_t*)circle_buf_gc_back(&w25q_dma.queue);
}

bool _w25q_route_call()
{
	return circle_buf_gc_count(&w25q_dma.queue) > 1;
}

bool _w25q_ready()
{
    if (!_w25q_initialized()) {
        return false;
    }
    return circle_buf_gc_empty(&w25q_dma.queue);
}

uint8_t _w25q_make_addr(uint8_t* buf, uint32_t addr)
{
    uint8_t counter = 0;
    if (_w25q_24bit()) {
        buf[counter++] = (uint8_t)(addr >> 24) & 0xFF;
    }
    buf[counter++] = (addr >> 16) & 0xFF;
    buf[counter++] = (addr >> 8) & 0xFF;
    buf[counter++] = addr & 0xFF;
    return counter;
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

void _init_s(void)
{
    if (w25qxx_init() == FLASH_OK) {
        fsm_gc_push_event(&w25qxx_fsm, &done_e);
    }
}

void _idle_a(void) {}

void _idle_s(void)
{
    if (!circle_buf_gc_empty(&w25q_dma.queue)) {
        fsm_gc_push_event(&w25qxx_fsm, &router_e);
    }
}

void _router_a(void) {}

void _router_s(void)
{
    if (circle_buf_gc_empty(&w25q_dma.queue)) {
        return;
    }
#if W25Q_BEDUG
    uint32_t need_len = 0;
    printPretty("");
    for (unsigned i = 0; i < circle_buf_gc_count(&w25q_dma.queue); i++) {
    	gprint("-");
    }
    gprint(" %s ", w25q_dma.result == FLASH_OK ? "OK" : "ERR")
    w25q_route_t* route = _w25q_queue_back();
#endif
    switch ((dma_status_t)*circle_buf_gc_back(&w25q_dma.queue))
    {
    case W25Q_DMA_FREE:
#if W25Q_BEDUG
    	gprint("free\n");
#endif
        fsm_gc_push_event(&w25qxx_fsm, &free_e);
        break;
    case W25Q_DMA_WRITE_ON:
#if W25Q_BEDUG
    	gprint("write_on\n");
#endif
        fsm_gc_push_event(&w25qxx_fsm, &write_on_e);
        break;
    case W25Q_DMA_WRITE_OFF:
#if W25Q_BEDUG
    	gprint("write_off\n");
#endif
        fsm_gc_push_event(&w25qxx_fsm, &write_off_e);
        break;
    case W25Q_DMA_READ:
#if W25Q_BEDUG
    	gprint(
			"read addr=0x%08X size=%lu\n",
			(unsigned)route->addr,
			route->len
		);
#endif
        fsm_gc_push_event(&w25qxx_fsm, &read_e);
        break;
    case W25Q_DMA_WRITE:
#if W25Q_BEDUG
    	need_len = route->len - route->cnt;
    	gprint(
			"write addr=0x%08X size=%lu\n",
			(unsigned)route->addr,
			need_len
		);
#endif
        fsm_gc_push_event(&w25qxx_fsm, &write_e);
        break;
    case W25Q_DMA_ERASE:
#if W25Q_BEDUG
    	gprint("erase count=%lu\n", route->len);
#endif
        fsm_gc_push_event(&w25qxx_fsm, &erase_e);
        break;
    case W25Q_DMA_READY:
    default:
#if W25Q_BEDUG
    	gprint("call\n");
#endif
        circle_buf_gc_free(&w25q_dma.queue);
        break;
    }
}

void _callback_a(void)
{
	w25q_route_t route = _w25q_queue_pop();
    if (w25q_dma.result != FLASH_OK && circle_buf_gc_count(&w25q_dma.queue) > 1) {
        while (circle_buf_gc_count(&w25q_dma.queue) > 1) {
            _w25q_queue_pop();
        }
        route = _w25q_queue_pop();
        w25qxx_stop_dma();
    }
    switch (route.status) {
    case W25Q_DMA_READ:
        if (circle_buf_gc_empty(&w25q_dma.queue)) {
            _read_dma_internal_callback(w25q_dma.result);
        }
        break;
    case W25Q_DMA_WRITE:
        if (circle_buf_gc_empty(&w25q_dma.queue)) {
            _write_dma_internal_callback(w25q_dma.result);
        }
        break;
    case W25Q_DMA_ERASE:
        if (circle_buf_gc_empty(&w25q_dma.queue)) {
            _erase_dma_internal_callback(w25q_dma.result);
        }
        break;
    case W25Q_DMA_FREE:
    case W25Q_DMA_WRITE_ON:
    case W25Q_DMA_WRITE_OFF:
    case W25Q_DMA_READY:
    default:
        break;
    }
    _W25Q_CS_reset();
}

void _w25q_route(w25q_route_t route)
{
    _W25Q_CS_reset();
    if (circle_buf_gc_full(&w25q_dma.queue)) {
		w25q_dma.result = FLASH_ERROR;
        fsm_gc_push_event(&w25qxx_fsm, &done_e);
    } else {
        _w25q_queue_push(route);
        fsm_gc_push_event(&w25qxx_fsm, &router_e);
    }
}

void _w25q_route_res(fsm_gc_t* fsm)
{
    if (w25q_dma.result == FLASH_OK) {
        fsm_gc_push_event(fsm, &success_e);
    } else {
        fsm_gc_push_event(fsm, &error_e);
    }
}

FSM_GC_CREATE_STATE(free_init_s, _free_init_s)
FSM_GC_CREATE_STATE(free_free_s, _free_free_s)

FSM_GC_CREATE_ACTION(free_check_a,   _free_check_a)
FSM_GC_CREATE_ACTION(free_count_a,   _free_count_a)
FSM_GC_CREATE_ACTION(free_tx_a,      _free_tx_a)
FSM_GC_CREATE_ACTION(free_rx_a,      _free_rx_a)
FSM_GC_CREATE_ACTION(free_success_a, _free_success_a)
FSM_GC_CREATE_ACTION(free_error_a,   _free_error_a)

FSM_GC_CREATE_TABLE(
    w25qxx_free_fsm_table,
    {&free_init_s,  &success_e,   &free_free_s, &free_tx_a},

	{&free_free_s,  &success_e,   &free_init_s, &free_success_a},
    {&free_free_s,  &timeout_e,   &free_free_s, &free_count_a},
    {&free_free_s,  &transmit_e,  &free_free_s, &free_rx_a},
    {&free_free_s,  &receive_e,   &free_free_s, &free_check_a},
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
	w25q_route_t* route = _w25q_queue_back();
	route->cnt = 0;
    fsm_gc_clear(&w25qxx_free_fsm);
    fsm_gc_push_event(&w25qxx_free_fsm, &success_e);
}

void _free_check_a(void)
{
	if (!(w25q_dma.sr1 & W25Q_SR1_BUSY)) {
	    fsm_gc_push_event(&w25qxx_free_fsm, &success_e);
	}
}

void _free_count_a(void)
{
	w25q_route_t* route = _w25q_queue_back();
    if (route->cnt > W25Q_SPI_ATTEMPTS_CNT) {
        w25q_dma.result = FLASH_BUSY;
        fsm_gc_push_event(&w25qxx_free_fsm, &error_e);
    } else {
    	route->cnt++;
        _free_tx_a();
    }
}

void _free_tx_a(void)
{
    w25q_dma.sr1 = 0;
    w25q_dma.cmd[0] = W25Q_CMD_READ_SR1;
    if (!_w25q_tx(w25q_dma.cmd, 1)) {
        w25q_dma.result = FLASH_ERROR;
        fsm_gc_push_event(&w25qxx_free_fsm, &error_e);
    }
    gtimer_start(&w25q_dma.timer, W25Q_SPI_TIMEOUT_MS);
}

void _free_rx_a(void)
{
    if (!_w25q_rx(&w25q_dma.sr1, 1)) {
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

void _free_success_a(void)
{
    w25q_dma.result = FLASH_OK;
    fsm_gc_clear(&w25qxx_free_fsm);
    fsm_gc_push_event(&w25qxx_fsm, &done_e);
}

void _free_error_a(void)
{
    if (w25q_dma.result == FLASH_OK) {
        w25q_dma.result = FLASH_ERROR;
    }
    fsm_gc_clear(&w25qxx_free_fsm);
    fsm_gc_push_event(&w25qxx_fsm, &done_e);
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
    {&write_on_init_s,         &success_e,  &write_on_unblock_free_s, &write_on_unblock_free_a},

    {&write_on_unblock_free_s, &success_e,  &write_on_unblock1_s,     &write_on_unblock1_a},
    {&write_on_unblock_free_s, &error_e,    &write_on_init_s,         &write_on_error_a},

    {&write_on_unblock1_s,     &transmit_e, &write_on_unblock2_s,     &write_on_unblock2_a},
    {&write_on_unblock1_s,     &error_e,    &write_on_init_s,         &write_on_error_a},

    {&write_on_unblock2_s,     &transmit_e, &write_on_enable_free_s,  &write_on_enable_free_a},
    {&write_on_unblock2_s,     &error_e,    &write_on_init_s,         &write_on_error_a},

    {&write_on_enable_free_s,  &success_e,  &write_on_enable_s,       &write_on_enable_a},
    {&write_on_enable_free_s,  &error_e,    &write_on_init_s,         &write_on_error_a},

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
    fsm_gc_clear(&w25qxx_write_on_fsm);
    fsm_gc_push_event(&w25qxx_write_on_fsm, &success_e);
}

void _write_on_unblock_free_a(void)
{
	w25q_route_t route = {
		.status = W25Q_DMA_FREE
	};
    _w25q_route(route);
}

void _write_on_unblock_free_s(void)
{
    _w25q_route_res(&w25qxx_write_on_fsm);
}

void _write_on_unblock1_a(void)
{
    fsm_gc_clear(&w25qxx_write_on_fsm);
    w25q_dma.cmd[0] = W25Q_CMD_WRITE_ENABLE_SR;
    if (!_w25q_tx(w25q_dma.cmd, 1)) {
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
    fsm_gc_clear(&w25qxx_write_on_fsm);
    w25q_dma.cmd[0] = W25Q_CMD_WRITE_SR1;
    w25q_dma.cmd[1] = ((W25Q_SR1_UNBLOCK_VALUE & 0x0F) << 2);
    if (!_w25q_tx(w25q_dma.cmd, 2)) {
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
	w25q_route_t route = {
		.status = W25Q_DMA_FREE
	};
    _w25q_route(route);
}

void _write_on_enable_free_s(void)
{
    _w25q_route_res(&w25qxx_write_on_fsm);
}

void _write_on_enable_a(void)
{
    fsm_gc_clear(&w25qxx_write_on_fsm);
    w25q_dma.cmd[0] = W25Q_CMD_WRITE_ENABLE;
    if (!_w25q_tx(w25q_dma.cmd, 1)) {
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
    fsm_gc_push_event(&w25qxx_fsm, &done_e);
}

void _write_on_error_a(void)
{
    if (w25q_dma.result == FLASH_OK) {
        w25q_dma.result = FLASH_ERROR;
    }
    fsm_gc_clear(&w25qxx_write_on_fsm);
    fsm_gc_push_event(&w25qxx_fsm, &done_e);
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
    fsm_gc_clear(&w25qxx_write_off_fsm);
    fsm_gc_push_event(&w25qxx_write_off_fsm, &success_e);
}

void _write_off_disable_free_a(void)
{
	w25q_route_t route = {
		.status = W25Q_DMA_FREE
	};
    _w25q_route(route);
}

void _write_off_disable_free_s(void)
{
    _w25q_route_res(&w25qxx_write_off_fsm);
}

void _write_off_disable_a(void)
{
    fsm_gc_clear(&w25qxx_write_off_fsm);
    w25q_dma.cmd[0] = W25Q_CMD_WRITE_DISABLE;
    if (!_w25q_tx(w25q_dma.cmd, 1)) {
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
	w25q_route_t route = {
		.status = W25Q_DMA_FREE
	};
    _w25q_route(route);
}

void _write_off_block_free_s(void)
{
    _w25q_route_res(&w25qxx_write_off_fsm);
}

void _write_off_block1_a(void)
{
    fsm_gc_clear(&w25qxx_write_off_fsm);
    w25q_dma.cmd[0] = W25Q_CMD_WRITE_ENABLE_SR;
    if (!_w25q_tx(w25q_dma.cmd, 1)) {
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
    fsm_gc_clear(&w25qxx_write_off_fsm);
    w25q_dma.cmd[0] = W25Q_CMD_WRITE_SR1;
    w25q_dma.cmd[1] = ((W25Q_SR1_BLOCK_VALUE & 0x0F) << 2);
    if (!_w25q_tx(w25q_dma.cmd, 2)) {
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
    fsm_gc_push_event(&w25qxx_fsm, &done_e);
}

void _write_off_error_a(void)
{
    if (w25q_dma.result == FLASH_OK) {
        w25q_dma.result = FLASH_ERROR;
    }
    fsm_gc_clear(&w25qxx_write_off_fsm);
    fsm_gc_push_event(&w25qxx_fsm, &done_e);
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
    fsm_gc_clear(&w25qxx_read_fsm);
    fsm_gc_push_event(&w25qxx_read_fsm, &success_e);
}

void _read_free_a(void)
{
	w25q_route_t route = {
		.status = W25Q_DMA_FREE
	};
    _w25q_route(route);
}

void _read_free_s(void) 
{
    _w25q_route_res(&w25qxx_read_fsm);
}

void _read_send_tx_a(void)
{
    fsm_gc_clear(&w25qxx_read_fsm);
	w25q_route_t* route = _w25q_queue_back();
    uint8_t counter = 0;
    w25q_dma.cmd[counter++] = W25Q_CMD_READ;
    counter += _w25q_make_addr(&w25q_dma.cmd[counter], route->addr);
    if (!_w25q_tx(w25q_dma.cmd, counter)) {
        fsm_gc_push_event(&w25qxx_read_fsm, &error_e);
    }
    gtimer_start(&w25q_dma.timer, W25Q_SPI_TIMEOUT_MS);
}

void _read_send_rx_a(void)
{
    fsm_gc_clear(&w25qxx_read_fsm);
	w25q_route_t* route = _w25q_queue_back();
    if (!_w25q_rx(route->rx_ptr, route->len)) {
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
    fsm_gc_push_event(&w25qxx_fsm, &done_e);
}

void _read_error_a(void)
{
    if (w25q_dma.result == FLASH_OK) {
        w25q_dma.result = FLASH_ERROR;
    }
    fsm_gc_clear(&w25qxx_read_fsm);
    fsm_gc_push_event(&w25qxx_fsm, &done_e);
}


FSM_GC_CREATE_STATE(write_init_s,         _write_init_s)
FSM_GC_CREATE_STATE(write_cmp_s,          _write_cmp_s)
FSM_GC_CREATE_STATE(write_erase_s,        _write_erase_s)
FSM_GC_CREATE_STATE(write_enable_s,       _write_enable_s)
FSM_GC_CREATE_STATE(write_cmd_free_s,     _write_cmd_free_s)
FSM_GC_CREATE_STATE(write_cmd_s,          _write_cmd_s)
FSM_GC_CREATE_STATE(write_data_s,         _write_data_s)
FSM_GC_CREATE_STATE(write_disable_s,      _write_disable_s)

FSM_GC_CREATE_ACTION(write_cmp_a,         _write_cmp_a)
FSM_GC_CREATE_ACTION(write_erase_a,       _write_erase_a)
FSM_GC_CREATE_ACTION(write_enable_a,      _write_enable_a)
FSM_GC_CREATE_ACTION(write_cmd_free_a,    _write_cmd_free_a)
FSM_GC_CREATE_ACTION(write_cmd_a,         _write_cmd_a)
FSM_GC_CREATE_ACTION(write_data_a,        _write_data_a)
FSM_GC_CREATE_ACTION(write_disable_a,     _write_disable_a)
FSM_GC_CREATE_ACTION(write_error_a,       _write_error_a)
FSM_GC_CREATE_ACTION(write_success_a,     _write_success_a)

FSM_GC_CREATE_TABLE(
    w25qxx_write_fsm_table,
    {&write_init_s,         &success_e,  &write_cmp_s,          &write_cmp_a},

    {&write_cmp_s,          &success_e,  &write_init_s,         &write_success_a},
    {&write_cmp_s,          &next_e,     &write_erase_s,        &write_success_a},
    {&write_cmp_s,          &erase_e,    &write_erase_s,        &write_erase_a},
    {&write_cmp_s,          &write_e,    &write_enable_s,       &write_enable_a},
    {&write_cmp_s,          &error_e,    &write_init_s,         &write_error_a},

    {&write_erase_s,        &success_e,  &write_enable_s,       &write_enable_a},
    {&write_erase_s,        &error_e,    &write_init_s,         &write_error_a},

    {&write_enable_s,       &done_e,     &write_cmp_s,          &write_cmp_a},
    {&write_enable_s,       &success_e,  &write_cmd_free_s,     &write_cmd_free_a},
    {&write_enable_s,       &error_e,    &write_init_s,         &write_error_a},

    {&write_cmd_free_s,     &success_e,  &write_cmd_s,          &write_cmd_a},
    {&write_cmd_free_s,     &error_e,    &write_init_s,         &write_error_a},

    {&write_cmd_s,          &transmit_e, &write_data_s,         &write_data_a},
    {&write_cmd_s,          &error_e,    &write_init_s,         &write_error_a},

    {&write_data_s,         &transmit_e, &write_disable_s,      &write_disable_a},
    {&write_data_s,         &error_e,    &write_init_s,         &write_error_a},

    {&write_disable_s,      &success_e,  &write_enable_s,       &write_enable_a},
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
    fsm_gc_clear(&w25qxx_erase_fsm);
    fsm_gc_push_event(&w25qxx_write_fsm, &success_e);
}

void _write_cmp_a(void)
{
	w25q_route_t* curr = _w25q_queue_back();
	w25q_route_t route = {
		.status = W25Q_DMA_READ,
		.addr   = curr->addr,
		.len    = curr->len,
		.rx_ptr = curr->rx_ptr
	};
    _w25q_route(route);
}

void _write_cmp_s(void)
{
	w25q_route_t* route = _w25q_queue_back();
    if (!memcmp(route->rx_ptr, route->tx_ptr, route->len)) {
        if (_w25q_route_call()) {
            fsm_gc_push_event(&w25qxx_write_fsm, &next_e);
        } else {
            fsm_gc_push_event(&w25qxx_write_fsm, &success_e);
        }
        return;
    } else if (route->cnt >= route->len) {
        fsm_gc_push_event(&w25qxx_write_fsm, &error_e);
        return;
    }
    bool need_erase = false;
    for (unsigned i = 0; i < route->len; i++) {
        if (route->rx_ptr[i] != 0xFF) {
            need_erase = true;
            break;
        }
    }
    if (need_erase && !_w25q_route_call()) {
        fsm_gc_push_event(&w25qxx_write_fsm, &erase_e);
        return;
    }
    fsm_gc_push_event(&w25qxx_write_fsm, &write_e);
}

void _write_erase_a(void)
{
	w25q_route_t* curr = _w25q_queue_back();
	w25q_route_t route = {
		.status = W25Q_DMA_ERASE,
		.rx_ptr = w25q_dma.buffer1,
		.tx_ptr = (uint8_t*)w25q_dma.addrs1,
		.len    = 0,
	};
    for (uint32_t i = curr->addr; i < curr->addr + curr->len; i += W25Q_PAGE_SIZE) {
    	w25q_dma.addrs1[route.len++] = i;
    }
    _w25q_route(route);
}

void _write_erase_s(void)
{
    if (_w25q_route_call()) {
        fsm_gc_push_event(&w25qxx_write_fsm, &success_e);
        return;
    }
    _w25q_route_res(&w25qxx_write_fsm);
}

void _write_enable_a(void)
{
	w25q_route_t* curr = _w25q_queue_back();
    if (curr->cnt >= curr->len) {
        return;
    }
	w25q_route_t route = {
		.status = W25Q_DMA_WRITE_ON,
	};
    _w25q_route(route);
}

void _write_enable_s(void)
{
	w25q_route_t* curr = _w25q_queue_back();
    if (curr->cnt >= curr->len) {
        fsm_gc_push_event(&w25qxx_write_fsm, &done_e);
    	return;
    }
    _w25q_route_res(&w25qxx_write_fsm);
}

void _write_cmd_free_a(void)
{
	w25q_route_t route = {
		.status = W25Q_DMA_FREE,
	};
    _w25q_route(route);
}

void _write_cmd_free_s(void)
{
    if (w25q_dma.result == FLASH_OK &&
        w25q_dma.sr1 & W25Q_SR1_WEL
    ) {
        fsm_gc_push_event(&w25qxx_write_fsm, &success_e);
    } else {
        fsm_gc_push_event(&w25qxx_write_fsm, &error_e);
    }
}

void _write_cmd_a(void)
{
    fsm_gc_clear(&w25qxx_write_fsm);
    uint8_t counter = 0;
    w25q_dma.cmd[counter++] = W25Q_CMD_PAGE_PROGRAMM;
	w25q_route_t* route = _w25q_queue_back();
    counter += _w25q_make_addr(&w25q_dma.cmd[counter], route->addr + route->cnt);
    if (!_w25q_tx(w25q_dma.cmd, counter)) {
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

void _write_data_a(void)
{
    fsm_gc_clear(&w25qxx_write_fsm);
	w25q_route_t* route = _w25q_queue_back();
    uint32_t len = W25Q_PAGE_SIZE;
    if (!_w25q_route_call()) {
    	uint32_t need_len = route->len - route->cnt;
    	len = need_len > W25Q_PAGE_SIZE ? W25Q_PAGE_SIZE : need_len;
    }
    if (!_w25q_tx(route->tx_ptr + route->cnt, len)) {
        fsm_gc_push_event(&w25qxx_write_fsm, &error_e);
    }
    route->cnt += len;
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
	w25q_route_t route = {
		.status = W25Q_DMA_WRITE_OFF,
	};
    _w25q_route(route);
}

void _write_disable_s(void)
{
    if (w25q_dma.result == FLASH_OK) {
        fsm_gc_push_event(&w25qxx_write_fsm, &success_e);
    } else {
        fsm_gc_push_event(&w25qxx_write_fsm, &error_e);
    }
}

void _write_success_a(void)
{
    w25q_dma.result = FLASH_OK;
    fsm_gc_clear(&w25qxx_write_fsm);
    fsm_gc_push_event(&w25qxx_fsm, &done_e);
}

void _write_error_a(void)
{
    if (w25q_dma.result == FLASH_OK) {
        w25q_dma.result = FLASH_ERROR;
    }
    fsm_gc_clear(&w25qxx_write_fsm);
    fsm_gc_push_event(&w25qxx_fsm, &done_e);
}


FSM_GC_CREATE_STATE(erase_init_s,        _erase_init_s)
FSM_GC_CREATE_STATE(erase_loop_s,        _erase_loop_s)
FSM_GC_CREATE_STATE(erase_read_s,        _erase_read_s)
FSM_GC_CREATE_STATE(erase_enable_s,      _erase_enable_s)
FSM_GC_CREATE_STATE(erase_erase_s,       _erase_erase_s)
FSM_GC_CREATE_STATE(erase_disable_s,     _erase_disable_s)
FSM_GC_CREATE_STATE(erase_repair_s,      _erase_repair_s)

FSM_GC_CREATE_ACTION(erase_loop_a,        _erase_loop_a)
FSM_GC_CREATE_ACTION(erase_iter_a,        _erase_iter_a)
FSM_GC_CREATE_ACTION(erase_read_a,        _erase_read_a)
FSM_GC_CREATE_ACTION(erase_enable_a,      _erase_enable_a)
FSM_GC_CREATE_ACTION(erase_erase_a,       _erase_erase_a)
FSM_GC_CREATE_ACTION(erase_disable_a,     _erase_disable_a)
FSM_GC_CREATE_ACTION(erase_success_a,     _erase_success_a)
FSM_GC_CREATE_ACTION(erase_error_a,       _erase_error_a)
FSM_GC_CREATE_ACTION(erase_repair_a,      _erase_repair_a)
FSM_GC_CREATE_ACTION(erase_repair_iter_a, _erase_repair_iter_a)


FSM_GC_CREATE_TABLE(
    w25qxx_erase_fsm_table,
    {&erase_init_s,         &success_e,  &erase_loop_s,          &erase_loop_a},

    {&erase_loop_s,         &success_e,  &erase_init_s,          &erase_success_a},
    {&erase_loop_s,         &next_e,     &erase_read_s,          &erase_read_a},

    {&erase_read_s,         &success_e,  &erase_loop_s,          &erase_iter_a},
    {&erase_read_s,         &erase_e,    &erase_enable_s,        &erase_enable_a},
    {&erase_read_s,         &error_e,    &erase_init_s,          &erase_error_a},

    {&erase_enable_s,       &success_e,  &erase_erase_s,         &erase_erase_a},
    {&erase_enable_s,       &error_e,    &erase_init_s,          &erase_error_a},

    {&erase_erase_s,        &transmit_e, &erase_disable_s,       &erase_disable_a},
    {&erase_erase_s,        &error_e,    &erase_init_s,          &erase_error_a},

    {&erase_disable_s,      &success_e,  &erase_repair_s,        &erase_repair_a},
    {&erase_disable_s,      &error_e,    &erase_init_s,          &erase_error_a},

    {&erase_repair_s,       &success_e,  &erase_loop_s,          &erase_iter_a},
    {&erase_repair_s,       &write_e,    &erase_repair_s,        &erase_repair_iter_a},
    {&erase_repair_s,       &error_e,    &erase_init_s,          &erase_error_a},
)

void _erase_a(void) {}

void _erase_s(void)
{
    static bool initialized = false;
    if (!initialized) {
        fsm_gc_init(&w25qxx_erase_fsm, w25qxx_erase_fsm_table, __arr_len(w25qxx_erase_fsm_table));
        initialized = true;
    }
    fsm_gc_process(&w25qxx_erase_fsm);
}

void _erase_init_s(void)
{
    fsm_gc_clear(&w25qxx_erase_fsm);
    fsm_gc_push_event(&w25qxx_erase_fsm, &success_e);
}

void _erase_loop_a(void)
{
	w25q_route_t* route = _w25q_queue_back();
	route->cnt = 0;
    fsm_gc_clear(&w25qxx_erase_fsm);
}

void _erase_iter_a(void)
{
	w25q_route_t* route = _w25q_queue_back();
    uint32_t last_sector_addr = __rm_mod(((uint32_t*)route->tx_ptr)[route->cnt], W25Q_SECTOR_SIZE);
	route->cnt++;
    if (route->cnt >= route->len) {
        return;
    }
    bool next_found = false;
    for (uint32_t i = route->cnt; i < route->len; i++) {
        uint32_t sector_addr = __rm_mod(((uint32_t*)route->tx_ptr)[i], W25Q_SECTOR_SIZE);
        if (last_sector_addr != sector_addr) {
            next_found = true;
            route->cnt = i;
            break;
        }
    }
    if (!next_found) {
    	route->cnt = route->len;
    }
}

void _erase_loop_s(void)
{
	w25q_route_t* route = _w25q_queue_back();
    uint32_t last_sector_addr = __rm_mod(((uint32_t*)route->tx_ptr)[route->cnt], W25Q_SECTOR_SIZE);
    uint32_t next_sector_addr = last_sector_addr;
    if (route->cnt + 1 < route->len) {
    	next_sector_addr = __rm_mod(((uint32_t*)route->tx_ptr)[route->cnt+1], W25Q_SECTOR_SIZE);
    }
    if (last_sector_addr != next_sector_addr) {
        fsm_gc_push_event(&w25qxx_erase_fsm, &next_e);
    } else if (route->cnt >= route->len) {
        fsm_gc_push_event(&w25qxx_erase_fsm, &success_e);
    } else {
        fsm_gc_push_event(&w25qxx_erase_fsm, &next_e);
    }
}

void _erase_read_a(void)
{
	w25q_route_t* curr = _w25q_queue_back();
	w25q_route_t route = {
		.status = W25Q_DMA_READ,
		.rx_ptr = curr->rx_ptr,
		.len    = W25Q_SECTOR_SIZE,
		.addr   = __rm_mod(
			((uint32_t*)curr->tx_ptr)[curr->cnt],
			W25Q_SECTOR_SIZE
		)
	};
    _w25q_route(route);
}

void _erase_read_s(void)
{
    if (w25q_dma.result != FLASH_OK) {
        fsm_gc_push_event(&w25qxx_erase_fsm, &error_e);
        return;
    }

    bool need_erase = false;
	w25q_route_t* route = _w25q_queue_back();
    for (unsigned i = 0; i < route->len && !need_erase; i++) {
        uint32_t addr_in_sector = ((uint32_t*)route->tx_ptr)[i] % W25Q_SECTOR_SIZE;
        for (unsigned j = 0; j < W25Q_PAGE_SIZE; j++) {
            if (route->rx_ptr[j] != 0xFF) {
            	route->tmp = addr_in_sector;
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
}

void _erase_enable_a(void)
{
	w25q_route_t route = {
		.status = W25Q_DMA_WRITE_ON,
	};
    _w25q_route(route);
}

void _erase_enable_s(void)
{
    _w25q_route_res(&w25qxx_erase_fsm);
}

void _erase_erase_a(void)
{
    fsm_gc_clear(&w25qxx_erase_fsm);
    uint8_t counter = 0;
	w25q_route_t* route = _w25q_queue_back();
    uint32_t addr = __rm_mod(route->tmp, W25Q_SECTOR_SIZE);
    w25q_dma.cmd[counter++] = W25Q_CMD_ERASE_SECTOR;
    counter += _w25q_make_addr(&w25q_dma.cmd[counter], addr);
    if (!_w25q_tx(w25q_dma.cmd, counter)) {
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
	w25q_route_t route = {
		.status = W25Q_DMA_WRITE_OFF,
	};
    _w25q_route(route);
}

void _erase_disable_s(void)
{
    _w25q_route_res(&w25qxx_erase_fsm);
}

void _erase_repair_a(void)
{
	w25q_route_t* route = _w25q_queue_back();
	uint32_t sector_addr = __rm_mod(((uint32_t*)route->tx_ptr)[route->cnt], W25Q_SECTOR_SIZE);
    for (unsigned i = route->cnt; i < route->len; i++) {
    	uint32_t addr = ((uint32_t*)route->tx_ptr)[i];
    	if (sector_addr != __rm_mod(addr, W25Q_SECTOR_SIZE)) {
    		break;
    	}
        memset(
			route->rx_ptr + (addr % W25Q_SECTOR_SIZE),
            0xFF,
            W25Q_PAGE_SIZE
        );
    }

    bool found = false;
    uint32_t addr_in_sector = 0;
    for (unsigned i = addr_in_sector; i < W25Q_SECTOR_SIZE; i++) {
        if (route->rx_ptr[i] != 0xFF) {
            addr_in_sector = __rm_mod(i, W25Q_PAGE_SIZE);
            found = true;
            break;
        }
    }

    if (!found) {
        fsm_gc_push_event(&w25qxx_erase_fsm, &success_e);
        return;
    }

    route->tmp = addr_in_sector;

    w25q_route_t repair = {
    	.status = W25Q_DMA_WRITE,
		.addr   = __rm_mod(
			((uint32_t*)route->tx_ptr)[route->cnt],
			W25Q_SECTOR_SIZE
		) + addr_in_sector,
		.len    = W25Q_PAGE_SIZE,
		.rx_ptr = w25q_dma.buffer2,
		.tx_ptr = w25q_dma.buffer1 + addr_in_sector,
		.cnt    = 0,
    };
    _w25q_route(repair);
}

void _erase_repair_iter_a(void)
{
	w25q_route_t* route = _w25q_queue_back();
    bool found = false;
    uint32_t addr_in_sector = route->tmp + W25Q_PAGE_SIZE;
    for (unsigned i = addr_in_sector; i < W25Q_SECTOR_SIZE; i++) {
        if (route->rx_ptr[i] != 0xFF) {
            addr_in_sector = __rm_mod(i, W25Q_PAGE_SIZE);
            found = true;
            break;
        }
    }

    if (!found) {
        fsm_gc_push_event(&w25qxx_erase_fsm, &success_e);
        return;
    }

    route->tmp = addr_in_sector;

    w25q_route_t repair = {
    	.status = W25Q_DMA_WRITE,
		.addr   = __rm_mod(
			((uint32_t*)route->tx_ptr)[route->cnt],
			W25Q_SECTOR_SIZE
		) + addr_in_sector,
		.len    = W25Q_PAGE_SIZE,
		.rx_ptr = w25q_dma.buffer2,
		.tx_ptr = w25q_dma.buffer1 + addr_in_sector,
		.cnt    = 0,
    };
    _w25q_route(repair);
}

void _erase_repair_s(void)
{
    if (w25q_dma.result == FLASH_OK) {
        fsm_gc_push_event(&w25qxx_erase_fsm, &write_e);
    } else {
        fsm_gc_push_event(&w25qxx_erase_fsm, &error_e);
    }
}

void _erase_success_a(void)
{
    w25q_dma.result = FLASH_OK;
    fsm_gc_clear(&w25qxx_erase_fsm);
    fsm_gc_push_event(&w25qxx_fsm, &done_e);
}

void _erase_error_a(void)
{
    if (w25q_dma.result == FLASH_OK) {
        w25q_dma.result = FLASH_ERROR;
    }
    fsm_gc_clear(&w25qxx_erase_fsm);
    fsm_gc_push_event(&w25qxx_fsm, &done_e);
}


#endif
