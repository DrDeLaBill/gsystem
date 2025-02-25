/* Copyright © 2025 Georgy E. All rights reserved. */

#ifndef _W25Q_STORAGE_H_
#define _W25Q_STORAGE_H_


#include "gdefines.h"
#include "gconfig.h"


#ifdef __cplusplus
extern "C" {
#endif


#ifdef GSYSTEM_FLASH_MODE


#include <stdint.h>
#include <stdbool.h>


#define W25Q_BEDUG             (1)

#define W25Q_TEST              (false)

#define W25Q_PAGE_SIZE         (0x100)
#define W25Q_SECTOR_SIZE       (0x1000)
#define W25Q_SETORS_IN_BLOCK   (0x10)

#define W25Q_SR1_WEL           ((uint8_t)0x02)
#define W25Q_SR1_BUSY          ((uint8_t)0x01)
#define W25Q_SR1_UNBLOCK_VALUE ((uint8_t)0x00)
#define W25Q_SR1_BLOCK_VALUE   ((uint8_t)0x0F)


typedef enum _flash_status_t {
    FLASH_OK    = ((uint8_t)0x00),  // OK
    FLASH_ERROR = ((uint8_t)0x01),  // Internal error
    FLASH_BUSY  = ((uint8_t)0x02),  // Memory or bus is busy
    FLASH_OOM   = ((uint8_t)0x03)   // Out Of Memory
} flash_status_t;

typedef enum _w25q_command_t {
    W25Q_CMD_WRITE_SR1       = ((uint8_t)0x01),
    W25Q_CMD_PAGE_PROGRAMM   = ((uint8_t)0x02),
    W25Q_CMD_READ            = ((uint8_t)0x03),
    W25Q_CMD_WRITE_DISABLE   = ((uint8_t)0x04),
    W25Q_CMD_READ_SR1        = ((uint8_t)0x05),
    W25Q_CMD_WRITE_ENABLE    = ((uint8_t)0x06),
    W25Q_CMD_ERASE_SECTOR    = ((uint8_t)0x20),
    W25Q_CMD_WRITE_ENABLE_SR = ((uint8_t)0x50),
    W25Q_CMD_ENABLE_RESET    = ((uint8_t)0x66),
    W25Q_CMD_RESET           = ((uint8_t)0x99),
    W25Q_CMD_JEDEC_ID        = ((uint8_t)0x9f),
	W25Q_CMD_ERASE_CHIP      = ((uint8_t)0xC7),
} flash_command_t;


/**
 *  Initializes the W25Qxx chip.
 *  @return Result status.
 */
flash_status_t w25qxx_init();

/**
 *  Completely clears the W25Q memory.
 *  @return Result status.
 */
flash_status_t w25qxx_erase_chip();

/**
 * The loop of the internal proccess
 */
void w24qxx_tick();

/**
 * Flash TX DMA callback
 */
void w25qxx_tx_dma_callback();

/**
 * Flash RX DMA callback
 */
void w25qxx_rx_dma_callback();

/**
 * Flash ERROR DMA callback
 */
void w25qxx_error_dma_callback();

/**
 *  Reads data from the W25Q memory.
 *  @param addr Target read address.
 *  @param data Data buffer for read.
 *  @param len Data buffer length.
 *  @return Result status.
 */
flash_status_t w25qxx_read(const uint32_t addr, uint8_t* data, const uint32_t len);

/**
 *  Reads data from the W25Q memory using DMA.
 *  @param addr Target read address.
 *  @param data Data buffer for read.
 *  @param len Data buffer length.
 *  @return Result status.
 */
flash_status_t w25qxx_read_dma(const uint32_t addr, uint8_t* data, const uint16_t len);

/**
 *  Writes data to the W25Q memory.
 *  @param addr Target read address.
 *  @param data Buffer with data for write.
 *  @param len Data buffer length (256 units maximum).
 *  @return Result status.
 */
flash_status_t w25qxx_write(const uint32_t addr, const uint8_t* data, const uint32_t len);

/**
 *  Writes data to the W25Q memory using DMA.
 *  @param addr Target read address.
 *  @param data Buffer with data for write.
 *  @param len Data buffer length (256 units maximum).
 *  @return Result status.
 */
flash_status_t w25qxx_write_dma(const uint32_t addr, const uint8_t* data, const uint16_t len);

/**
 *  Erases addresses in the W25Q memory.
 *  @param addrs[] Array of addresses.
 *  @param count   Number of the addresses.
 *  @return Result status.
 */
flash_status_t w25qxx_erase_addresses(const uint32_t* addrs, const uint32_t count);

/**
 *  Erases addresses in the W25Q memory using DMA.
 *  @param addrs[] Array of addresses.
 *  @param count   Number of the addresses.
 *  @return Result status.
 */
flash_status_t w25qxx_erase_addresses_dma(const uint32_t* addrs, const uint32_t count);

/*
 * Stops DMA process
 */
void w25qxx_stop_dma();

/**
 * Returns the result status of the w25qxx_read_dma() function.s
 * @param status Result status.
 */
void w25qxx_read_event(const flash_status_t status);

/**
 * Returns the result status of the w25qxx_write_dma() function.s
 * @param status Result status.
 */
void w25qxx_write_event(const flash_status_t status);

/**
 * Returns the result status of the w25qxx_erase_addresses_dma() function.s
 * @param status Result status.
 */
void w25qxx_erase_event(const flash_status_t status);

/**
 *  @return W25Q memory bytes_size.
 */
uint32_t w25qxx_size();

/**
 *  @return W25Q memory pages count.
 */
uint32_t w25qxx_get_pages_count();

/**
 *  @return W25Q memory blocks count.
 */
uint32_t w25qxx_get_blocks_count();

/**
 *  @return W25Q memory block size.
 */
uint32_t w25qxx_get_block_size();


#ifdef __cplusplus
}
#endif


#endif


#endif
