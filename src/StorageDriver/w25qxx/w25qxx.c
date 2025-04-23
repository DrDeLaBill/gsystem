/* Copyright © 2025 Georgy E. All rights reserved. */

#include "w25qxx.h"


#include "gdefines.h"
#include "gconfig.h"


#ifdef GSYSTEM_FLASH_MODE


#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "glog.h"
#include "main.h"
#include "g_hal.h"
#include "gutils.h"
#include "gsystem.h"


typedef struct _w25q_t {
    bool      	 initialized;
    bool     	 is_24bit_address;

    uint32_t 	 page_size;
    uint32_t 	 pages_count;

    uint32_t 	 sector_size;
    uint32_t 	 sectors_in_block;

    uint32_t 	 block_size;
    uint32_t 	 blocks_count;
} w25q_t;


#define W25Q_JEDEC_ID_SIZE        (sizeof(uint32_t))
#define W25Q_24BIT_ADDR_SIZE      ((uint16_t)512)

#define W25Q_SPI_TIMEOUT_MS       ((uint32_t)SECOND_MS)
#define W25Q_SPI_ERASE_CHIP_MS    ((uint32_t)5 * SECOND_MS)
#define W25Q_SPI_COMMAND_SIZE_MAX ((uint8_t)10)



static flash_status_t _w25q_read_jdec_id(uint32_t* jdec_id);
static flash_status_t _w25q_read_SR1(uint8_t* SR1);

static flash_status_t _w25q_write_enable();
static flash_status_t _w25q_write_disable();
static flash_status_t _w25q_write(const uint32_t addr, const uint8_t* data, const uint32_t len);
static flash_status_t _w25q_set_protect_block(uint8_t value);

static flash_status_t _w25q_read(uint32_t addr, uint8_t* data, uint32_t len);

static flash_status_t _w25q_erase_sector(uint32_t addr);

static flash_status_t _w25q_data_cmp(const uint32_t addr, const uint8_t* data, const uint32_t len, bool* cmp_res);

static flash_status_t _w25q_send_data(const uint8_t* data, const uint32_t len);
static flash_status_t _w25q_recieve_data(uint8_t* data, uint32_t len);

static bool           _w25q_check_FREE();
static bool           _w25q_check_WEL();

bool                  _w25q_24bit();
void                  _W25Q_CS_set();
void                  _W25Q_CS_reset();

#ifdef GSYSTEM_BEDUG
const char W25Q_TAG[] = "FLSH";
#endif

extern bool _w25q_ready();

extern SPI_HandleTypeDef GSYSTEM_FLASH_SPI;

#define W25Q_JDEC_ID_BLOCK_COUNT_MASK ((uint16_t)0x4011)
const uint16_t w25qxx_jdec_id_block_count[] = {
    2,   // w25q10
    4,   // w25q20
    8,   // w25q40
    16,  // w25q80
    32,  // w25q16
    64,  // w25q32
    128, // w25q64
    256, // w25q128
    512, // w25q256
    1024 // w25q512
};


w25q_t w25q = {
    .initialized      = false,
    .is_24bit_address = false,

    .page_size        = W25Q_PAGE_SIZE,
    .pages_count      = W25Q_SECTOR_SIZE / W25Q_PAGE_SIZE,

    .sector_size      = W25Q_SECTOR_SIZE,
    .sectors_in_block = W25Q_SETORS_IN_BLOCK,

    .block_size       = W25Q_SETORS_IN_BLOCK * W25Q_SECTOR_SIZE,
    .blocks_count     = 0,
};


flash_status_t w25qxx_init()
{
#if W25Q_BEDUG
	printTagLog(W25Q_TAG, "flash init: begin");
#endif

	if (w25q.initialized) {
#if W25Q_BEDUG
		printTagLog(W25Q_TAG, "flash init: OK (already inittialized)");
#endif
		return FLASH_OK;
	}

    uint32_t jdec_id = 0;
    flash_status_t status = _w25q_read_jdec_id(&jdec_id);
    if (status != FLASH_OK) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash init: error=%u (read JDEC ID)", status);
#endif
        goto do_error;
    }
    if (!jdec_id) {
    	status = FLASH_ERROR;
    	goto do_error;
    }

    w25q.blocks_count = 0;
    uint16_t jdec_id_2b = (uint16_t)jdec_id;
    for (uint16_t i = 0; i < __arr_len(w25qxx_jdec_id_block_count); i++) {
        if ((uint16_t)(W25Q_JDEC_ID_BLOCK_COUNT_MASK + i) == jdec_id_2b) {
            w25q.blocks_count = w25qxx_jdec_id_block_count[i];
            break;
        }
    }

    if (!w25q.blocks_count) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash init: error - unknown JDEC ID");
#endif
    	status = FLASH_ERROR;
        goto do_error;
    }


#if W25Q_BEDUG
    printTagLog(W25Q_TAG, "flash JDEC ID found: id=%08X, blocks_count=%lu", (unsigned int)jdec_id, w25q.blocks_count);
#endif

	_W25Q_CS_set();
    status = _w25q_set_protect_block(W25Q_SR1_BLOCK_VALUE);
    if (status != FLASH_OK) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash init: error=%u (block W25Q error)", status);
#endif
        goto do_error;
    }
	_W25Q_CS_reset();

	static uint32_t address = 0;
	uint8_t data[W25Q_PAGE_SIZE] = {0};
    _W25Q_CS_set();
    status = _w25q_read(address, data, sizeof(data));
    if (!w25qxx_size()) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash init: W25Q size error");
#endif
        goto do_error;
    }
    address = (address + 1) % w25qxx_size();
    if (status != FLASH_OK) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash init: error=%u (read W25Q error)", status);
#endif
        goto do_error;
    }
	_W25Q_CS_reset();

	w25q.initialized      = true;
	w25q.is_24bit_address = (w25q.blocks_count >= W25Q_24BIT_ADDR_SIZE) ? true : false;

#if W25Q_BEDUG
    printTagLog(W25Q_TAG, "flash init: OK");
#endif

do_error:
	_W25Q_CS_reset();

    return status;
}

flash_status_t w25qxx_erase_chip()
{
#if W25Q_BEDUG
    printTagLog(W25Q_TAG, "flash erase: begin");
#endif

    w25qxx_stop_dma();

	_W25Q_CS_set();
    flash_status_t status = _w25q_set_protect_block(W25Q_SR1_UNBLOCK_VALUE);
    if (status != FLASH_OK) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash erase: error=%u (unset block protect)", status);
#endif
        status = FLASH_BUSY;
        goto do_protect;
    }
	_W25Q_CS_reset();

	_W25Q_CS_set();
    status = _w25q_write_enable();
    if (status != FLASH_OK) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash erase: error=%u (write is not enabled)", status);
#endif
        goto do_protect;
    }
	_W25Q_CS_reset();

    if (!util_wait_event(_w25q_check_FREE, W25Q_SPI_ERASE_CHIP_MS)) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash erase: error (flash is busy)");
#endif
        goto do_protect;
    }

    uint8_t spi_cmd[] = { W25Q_CMD_ERASE_CHIP };
	_W25Q_CS_set();
    status = _w25q_send_data(spi_cmd, sizeof(spi_cmd));
    if (status != FLASH_OK) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash erase: error=%u (send command)", status);
#endif
        status = FLASH_BUSY;
    }
	_W25Q_CS_reset();

    if (!util_wait_event(_w25q_check_FREE, W25Q_SPI_ERASE_CHIP_MS)) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash erase: error (flash is busy)");
#endif
        goto do_protect;
    }

    flash_status_t tmp_status = FLASH_OK;
do_protect:
	_W25Q_CS_set();
	tmp_status = _w25q_write_disable();
	if (tmp_status != FLASH_OK) {
	    if (status == FLASH_OK) {
	        status = tmp_status;
	    } else {
	        return status;
	    }
	}
	_W25Q_CS_reset();

	_W25Q_CS_set();
    tmp_status = _w25q_set_protect_block(W25Q_SR1_BLOCK_VALUE);
    if (tmp_status == FLASH_OK) {
        status = tmp_status;
    } else {
        return status;
    }
	_W25Q_CS_reset();

    if (status != FLASH_OK) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash erase: error=%u (protect error)", status);
#endif
        status = FLASH_BUSY;
    } else {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash erase: OK");
#endif
    }

    w25q.initialized = false;

    return status;
}

flash_status_t w25qxx_read(const uint32_t addr, uint8_t* data, const uint32_t len)
{
    if (!_w25q_ready()) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash read addr=%08lX len=%lu (flash not ready)", addr, len);
#endif
    	return FLASH_ERROR;
    }

    _W25Q_CS_set();
    flash_status_t status = _w25q_read(addr, data, len);
	_W25Q_CS_reset();

    return status;
}

flash_status_t w25qxx_write(const uint32_t addr, const uint8_t* data, const uint32_t len)
{
	/* Check input data BEGIN */
#if W25Q_BEDUG
	printTagLog(W25Q_TAG, "flash write addr=%08lX len=%lu: begin", addr, len);
//	util_debug_hex_dump(data, addr, len);
#endif

    if (!_w25q_ready()) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash write addr=%08lX len=%lu (flash was not initialized)", addr, len);
#endif
        return FLASH_ERROR;
    }

    if (addr % W25Q_PAGE_SIZE) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash write addr=%08lX len=%lu (bad address)", addr, len);
#endif
        return FLASH_ERROR;
    }

	flash_status_t status = FLASH_OK;
    if (addr + len > w25qxx_size()) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash write addr=%08lX len=%lu error (unacceptable address)", addr, len);
#endif
        status = FLASH_OOM;
        goto do_spi_stop;
    }
	/* Check input data END */


    /* Compare old flashed data BEGIN */
	_W25Q_CS_set();
    bool compare_status = false;
    status = _w25q_data_cmp(addr, data, len, &compare_status);
	if (status != FLASH_OK) {
#if W25Q_BEDUG
		printTagLog(W25Q_TAG, "flash write addr=%08lX len=%lu error=%u (compare data)", addr, len, status);
#endif
        goto do_spi_stop;
	}
	_W25Q_CS_reset();

	if (!compare_status) {
#if W25Q_BEDUG
		printTagLog(W25Q_TAG, "flash write addr=%08lX len=%lu: ABORT (already written)", addr, len);
#endif
        goto do_spi_stop;
	}
    /* Compare old flashed data END */

	/* Erase data BEGIN */
	{
		uint32_t erase_addrs[W25Q_SECTOR_SIZE / W25Q_PAGE_SIZE] = {0};
		bool     erase_need       = false;
		unsigned erase_cnt        = 0;
		uint32_t erase_len        = 0;
		uint32_t erase_addr       = addr;
		while (erase_len < len) {
			uint32_t min_erase_size         = W25Q_SECTOR_SIZE;
			uint32_t erase_sector_addr      = (erase_addr / min_erase_size) * min_erase_size;
			uint32_t erase_next_sector_addr = ((erase_addr + W25Q_PAGE_SIZE) / min_erase_size) * min_erase_size;

		    /* Compare old exist data BEGIN */
			_W25Q_CS_set();
		    bool compare_status = false;
		    if (!erase_need) {
		    	status = _w25q_data_cmp(
					erase_addr, 
					data + erase_len, 
					__min(W25Q_PAGE_SIZE, len - erase_len), 
					&compare_status
				);
		    }
			if (status != FLASH_OK) {
#if W25Q_BEDUG
				printTagLog(W25Q_TAG, "flash write addr=%08lX len=%lu error=%u (compare data)", addr, len, status);
#endif
	            goto do_spi_stop;
			}
			_W25Q_CS_reset();

			if (compare_status) {
				erase_need = true;
			}
		    /* Compare old exist data END */

			/* Erase addresses in current sector BEGIN */
			if (erase_len < len) {
				erase_addrs[erase_cnt++] = erase_addr;
			}

			if (erase_len + W25Q_PAGE_SIZE >= len ||
				erase_sector_addr != erase_next_sector_addr
			) {
				if (erase_need) {
					status = w25qxx_erase_addresses(erase_addrs, erase_cnt);
				} else {
					status = FLASH_OK;
				}
				if (status != FLASH_OK) {
#if W25Q_BEDUG
					printTagLog(
						W25Q_TAG,
						"flash write addr=%08lX len=%lu error=%u (unable to erase old data)",
						addr,
						len,
						status
					);
#endif
		            goto do_spi_stop;
				}

				memset(
					(uint8_t*)erase_addrs,
					0,
					sizeof(erase_addrs)
				);

				if (status == FLASH_BUSY) {
		            goto do_spi_stop;
				}
				if (status != FLASH_OK) {
					break;
				}

				erase_need = false;
				erase_cnt  = 0;
			}
			/* Erase addresses in current sector END */

			erase_addr += W25Q_PAGE_SIZE;
			erase_len  += W25Q_PAGE_SIZE;
		}

		if (erase_need && erase_cnt) {
			status = w25qxx_erase_addresses(erase_addrs, erase_cnt);
		}
		if (status != FLASH_OK) {
#if W25Q_BEDUG
			printTagLog(
				W25Q_TAG,
				"flash write addr=%08lX len=%lu error=%u (unable to erase old data)",
				addr,
				len,
				status
			);
#endif
			goto do_spi_stop;
		}
	}
	/* Erase data END */


    /* Write data BEGIN */
    uint32_t cur_len = 0;
    while (cur_len < len) {
    	uint32_t write_len = W25Q_PAGE_SIZE;
    	if (cur_len + write_len > len) {
    		write_len = len - cur_len;
    	}
    	_W25Q_CS_set();
    	status = _w25q_write(addr + cur_len, data + cur_len, write_len);
    	_W25Q_CS_reset();
    	if (status != FLASH_OK) {
#if W25Q_BEDUG
        	printTagLog(W25Q_TAG, "flash write addr=%08lX len=%lu error=%u (write)", addr + cur_len, write_len, status);
#endif
            goto do_spi_stop;
    	}

    	_W25Q_CS_set();
    	uint8_t page_buf[W25Q_PAGE_SIZE] = {0};
		status = _w25q_read(addr + cur_len, page_buf, write_len);
    	_W25Q_CS_reset();
    	if (status != FLASH_OK) {
#if W25Q_BEDUG
        	printTagLog(W25Q_TAG, "flash write addr=%08lX len=%lu error=%u (read written page after write)", addr + cur_len, write_len, status);
#endif
            goto do_spi_stop;
    	}

    	int cmp_res = memcmp(page_buf, data + cur_len, write_len);
		if (cmp_res) {
#if W25Q_BEDUG
        	printTagLog(
				W25Q_TAG,
				"flash write addr=%08lX len=%lu error=%d (compare written page with read)",
				addr + cur_len,
				write_len,
				cmp_res
			);
			printTagLog(W25Q_TAG, "Needed page:");
			util_debug_hex_dump(data + cur_len, addr + cur_len, (uint16_t)write_len);
			printTagLog(W25Q_TAG, "Readed page:");
			util_debug_hex_dump(page_buf, addr + cur_len, (uint16_t)write_len);
#endif
			set_error(EXPECTED_MEMORY_ERROR);
			status = FLASH_ERROR;
	        goto do_spi_stop;
    	}

		reset_error(EXPECTED_MEMORY_ERROR);

    	cur_len += write_len;
    }
    /* Write data END */

#if W25Q_BEDUG
	printTagLog(W25Q_TAG, "flash write addr=%08lX len=%lu: OK", addr, len);
#endif

do_spi_stop:
	_W25Q_CS_reset();

    return status;
}

flash_status_t w25qxx_erase_addresses(const uint32_t* addrs, const uint32_t count)
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
		printPretty("%08lX\n", addrs[i]);
	}
#endif

	for (uint32_t i = 0; i < count;) {
		uint32_t cur_sector_idx  = addrs[i] / W25Q_SECTOR_SIZE;
		uint32_t cur_sector_addr = cur_sector_idx * W25Q_SECTOR_SIZE;
		uint8_t  sector_buf[W25Q_SECTOR_SIZE] = {0};

		/* Addresses for erase in current sector BEGIN */
		uint32_t next_sector_i = 0;
		for (uint32_t j = i; j < count; j++) {
			if (addrs[j] / W25Q_SECTOR_SIZE != cur_sector_idx) {
				next_sector_i = j;
				break;
			}
		}
		if (!next_sector_i) {
			next_sector_i = count;
		}
		/* Addresses for erase in current sector END */

		/* Read target sector BEGIN */
		_W25Q_CS_set();
		flash_status_t status = _w25q_read(cur_sector_addr, sector_buf, sizeof(sector_buf));
		_W25Q_CS_reset();
		if (status != FLASH_OK) {
#if W25Q_BEDUG
			printTagLog(
				W25Q_TAG,
				"flash erase data addr=%08lX error (unable to read sector: block_idx=%lu sector_idx=%lu len=%lu)",
				cur_sector_addr,
				cur_sector_addr / w25q.block_size,
				(cur_sector_addr % w25q.block_size) / w25q.sector_size,
				(long unsigned int)W25Q_SECTOR_SIZE
			);
#endif
			return status;
		}
		/* Read target sector END */


		/* Check target sector need erase BEGIN */
		bool need_erase_sector = false;
		for (uint32_t j = i; j < next_sector_i; j++) {
			uint32_t addr_in_sector = addrs[j] % W25Q_SECTOR_SIZE;
			for (uint32_t k = 0; k < W25Q_PAGE_SIZE; k++) {
				if (sector_buf[addr_in_sector + k] != 0xFF) {
					need_erase_sector = true;
					break;
				}
			}
			if (need_erase_sector) {
				break;
			}
		}
		if (!need_erase_sector) {
#if W25Q_BEDUG
			for (uint32_t j = i; j < next_sector_i; j++) {
				printTagLog(
					W25Q_TAG,
					"flash address=%08lX (sector addr=%08lX) already empty",
					addrs[j],
					cur_sector_addr
				);
			}
#endif
			i = next_sector_i;
			continue;
		}
		/* Check target sector need erase END */


		/* Erase sector BEGIN */
		_W25Q_CS_set();
		status = _w25q_erase_sector(cur_sector_addr);
		_W25Q_CS_reset();
		if (status != FLASH_OK) {
#if W25Q_BEDUG
			printTagLog(
				W25Q_TAG,
				"flash erase data addr=%08lX error=%u (unable to erase sector: block_addr=%08lX sector_addr=%08lX len=%lu)",
				cur_sector_addr,
				status,
				cur_sector_addr / w25q.block_size,
				(cur_sector_addr % w25q.block_size) / w25q.sector_size,
				(long unsigned int)W25Q_SECTOR_SIZE
			);
#endif
			return status;
		}
		_W25Q_CS_set();
		if (!util_wait_event(_w25q_check_FREE, W25Q_SPI_TIMEOUT_MS)) {
			_W25Q_CS_reset();
#if W25Q_BEDUG
			printTagLog(W25Q_TAG, "flash erase data addr=%08lX error (flash is busy)", cur_sector_addr);
#endif
			return FLASH_BUSY;
		}
		_W25Q_CS_reset();
		/* Erase sector END */


		/* Return old data BEGIN */
		for (unsigned j = 0; j < W25Q_SECTOR_SIZE / W25Q_PAGE_SIZE; j++) {
			bool need_restore = true;
			uint32_t tmp_page_addr = cur_sector_addr + j * W25Q_PAGE_SIZE;
			for (unsigned k = i; k < next_sector_i; k++) {
				if (addrs[k] == tmp_page_addr) {
					need_restore = false;
				}
			}
			if (!need_restore) {
#if W25Q_BEDUG
				printTagLog(
					W25Q_TAG,
					"flash restore data addr=%08lX ignored",
					tmp_page_addr
				);
#endif
				continue;
			}

			need_restore = false;
			for (unsigned k = 0; k < W25Q_PAGE_SIZE; k++) {
				if (sector_buf[tmp_page_addr % W25Q_SECTOR_SIZE + k] != 0xFF) {
					need_restore = true;
					break;
				}
			}
			if (!need_restore) {
#if W25Q_BEDUG
				printTagLog(
					W25Q_TAG,
					"flash restore data addr=%08lX ignored (page empty)",
					tmp_page_addr
				);
#endif
				continue;
			}

#if W25Q_BEDUG
			printTagLog(
				W25Q_TAG,
				"flash restore data addr=%08lX begin",
				tmp_page_addr
			);
#endif
			_W25Q_CS_set();
			status = _w25q_write(
				tmp_page_addr,
				&sector_buf[tmp_page_addr % W25Q_SECTOR_SIZE],
				W25Q_PAGE_SIZE
			);
			_W25Q_CS_reset();
			if (status != FLASH_OK) {
#if W25Q_BEDUG
				printTagLog(
					W25Q_TAG,
					"flash erase data addr=%08lX error=%u (unable to write old data page addr=%08lX)",
					cur_sector_addr,
					status,
					tmp_page_addr
				);
#endif
				return status;
			}

	    	uint8_t page_buf[W25Q_PAGE_SIZE] = {0};
	    	_W25Q_CS_set();
			status = _w25q_read(tmp_page_addr, page_buf, W25Q_PAGE_SIZE);
	    	_W25Q_CS_reset();
	    	if (status != FLASH_OK) {
	#if W25Q_BEDUG
	        	printTagLog(
					W25Q_TAG,
					"flash erase data addr=%08lX error=%u (unable to read page addr=%08lX)",
					cur_sector_addr,
					status,
					tmp_page_addr
				);
	#endif
				return status;
	    	}

	    	int cmp_res = memcmp(
				page_buf,
				&sector_buf[tmp_page_addr % W25Q_SECTOR_SIZE],
				W25Q_PAGE_SIZE
			);
			if (cmp_res) {
#if W25Q_BEDUG
	        	printTagLog(
					W25Q_TAG,
					"flash erase data addr=%08lX error=%d (compare written page with read)",
					tmp_page_addr,
					status
				);
				printTagLog(W25Q_TAG, "Needed page:");
				util_debug_hex_dump(
					&sector_buf[tmp_page_addr % W25Q_SECTOR_SIZE],
					tmp_page_addr,
					W25Q_PAGE_SIZE
				);
				printTagLog(W25Q_TAG, "Readed page:");
				util_debug_hex_dump(
					page_buf,
					tmp_page_addr,
					W25Q_PAGE_SIZE
				);
#endif
				set_error(EXPECTED_MEMORY_ERROR);
				status = FLASH_ERROR;
				return status;
	    	}

#if W25Q_BEDUG
			printTagLog(
				W25Q_TAG,
				"flash restore data addr=%08lX OK",
				tmp_page_addr
			);
#endif

			reset_error(EXPECTED_MEMORY_ERROR);
		}
		/* Return old data END */

		i = next_sector_i;
	}

	return FLASH_OK;
}

flash_status_t w25qxx_erase_sector(const uint32_t addr)
{
	if (addr % W25Q_SECTOR_SIZE) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash erase addr=%08lX (bad address)", addr);
#endif
		return FLASH_ERROR;
	}
	uint32_t addrs[W25Q_SECTOR_SIZE / W25Q_PAGE_SIZE] = {0};
	for (unsigned i = 0; i < W25Q_SECTOR_SIZE; i+=W25Q_PAGE_SIZE) {
		addrs[i / W25Q_PAGE_SIZE] = addr + i;
	}
	return w25qxx_erase_addresses(addrs, __arr_len(addrs));
}

flash_status_t _w25q_write(const uint32_t addr, const uint8_t* data, const uint32_t len)
{
	if (len > w25q.page_size) {
#if W25Q_BEDUG
		printTagLog(W25Q_TAG, "flash write addr=%08lX len=%lu error (unacceptable data length)", addr, len);
#endif
		return FLASH_ERROR;
	}

    if (addr + len > w25qxx_size()) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash write addr=%08lX len=%lu error (unacceptable address)", addr, len);
#endif
        return FLASH_OOM;
    }

    flash_status_t status = _w25q_set_protect_block(W25Q_SR1_UNBLOCK_VALUE);
    if (status != FLASH_OK) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash write addr=%08lX len=%lu error=%u (unset block protect)", addr, len, status);
#endif
        goto do_block_protect;
    }
    status = _w25q_write_enable();
    if (status != FLASH_OK) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash write addr=%08lX len=%lu error=%u (write enable)", addr, len, status);
#endif
        goto do_block_protect;
    }
    if (!util_wait_event(_w25q_check_WEL, W25Q_SPI_TIMEOUT_MS)) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash write addr=%08lX len=%lu error=%u (WEL bit wait time exceeded)", addr, len, status);
#endif
		status = FLASH_BUSY;
		goto do_block_protect;
    }

    if (!util_wait_event(_w25q_check_FREE, W25Q_SPI_TIMEOUT_MS)) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash write addr=%08lX len=%lu error (W25Q busy)", addr, len);
#endif
        return FLASH_ERROR;
    }

    uint8_t counter = 0;
    uint8_t spi_cmd[W25Q_SPI_COMMAND_SIZE_MAX] = { 0 };

    spi_cmd[counter++] = W25Q_CMD_PAGE_PROGRAMM;
    if (w25q.is_24bit_address) {
        spi_cmd[counter++] = (uint8_t)(addr >> 24) & 0xFF;
    }
    spi_cmd[counter++] = (addr >> 16) & 0xFF;
    spi_cmd[counter++] = (addr >> 8) & 0xFF;
    spi_cmd[counter++] = addr & 0xFF;

    status = _w25q_send_data(spi_cmd, counter);
    if (status != FLASH_OK) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash write addr=%08lX len=%lu error=%u (send command)", addr, len, (unsigned int)status);
#endif
		goto do_block_protect;
    }

    status = _w25q_send_data(data, len);
    if (status != FLASH_OK) {
#if W25Q_BEDUG
    	printTagLog(W25Q_TAG, "flash write addr=%08lX len=%lu error=%u (wait write data timeout)", addr, len, (unsigned int)status);
#endif
		goto do_block_protect;
    }

do_block_protect:
    status = _w25q_write_disable();
    if (status != FLASH_OK) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash write addr=%08lX len=%lu error=%u (write is not disabled)", addr, len, status);
#endif
    }

    status = _w25q_set_protect_block(W25Q_SR1_BLOCK_VALUE);
    if (status != FLASH_OK) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash write addr=%08lX len=%lu error=%u (set block protected)", addr, len, status);
#endif
    }

    return status;
}

uint32_t w25qxx_size()
{
    return w25q.blocks_count * w25q.block_size;
}

uint32_t w25qxx_get_pages_count()
{
    if (!w25q.initialized) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "w25qxx_get_pages_count: not initialized");
#endif
        return 0;
    }
    return w25q.pages_count * w25q.sectors_in_block * w25q.blocks_count;
}

uint32_t w25qxx_get_blocks_count()
{
	if (!w25q.initialized) {
#if W25Q_BEDUG
		printTagLog(W25Q_TAG, "w25qxx_get_blocks_count: not initialized");
#endif
		return 0;
	}
    return w25q.blocks_count;
}

uint32_t w25qxx_get_block_size()
{
	if (!w25q.initialized) {
#if W25Q_BEDUG
		printTagLog(W25Q_TAG, "w25qxx_get_block_size: not initialized");
#endif
		return 0;
	}
    return w25q.block_size;
}

flash_status_t _w25q_data_cmp(const uint32_t addr, const uint8_t* data, const uint32_t len, bool* cmp_res)
{
	*cmp_res = false;

	uint32_t cur_len = 0;
	while (cur_len < len) {
		uint32_t needed_len = W25Q_PAGE_SIZE;
		if (cur_len + needed_len > len) {
			needed_len = len - cur_len;
		}

		uint8_t read_data[W25Q_PAGE_SIZE] = {0};
		flash_status_t status = _w25q_read(addr + cur_len, read_data, needed_len);
		if (status != FLASH_OK) {
#if W25Q_BEDUG
	        printTagLog(W25Q_TAG, "flash compare addr=%08lX len=%lu error=%u (read)", addr + cur_len, needed_len, status);
#endif
	        return status;
		}

		if (memcmp(read_data, data + cur_len, needed_len)) {
			*cmp_res = true;
			break;
		}

		cur_len += needed_len;
	}

	return FLASH_OK;
}

flash_status_t _w25q_read(uint32_t addr, uint8_t* data, uint32_t len)
{
    if (addr + len > w25qxx_size()) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash read addr=%08lX len=%lu: error (unacceptable address)", addr, len);
#endif
        return FLASH_OOM;
    }

    flash_status_t status = FLASH_OK;
    if (!util_wait_event(_w25q_check_FREE, W25Q_SPI_TIMEOUT_MS)) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash read addr=%08lX len=%lu: error (W25Q busy)", addr, len);
#endif
        return FLASH_BUSY;
    }

    uint8_t spi_cmd[W25Q_SPI_COMMAND_SIZE_MAX] = { 0 };
    uint8_t counter = 0;
    spi_cmd[counter++] = W25Q_CMD_READ;
    if (w25q.is_24bit_address) {
        spi_cmd[counter++] = (uint8_t)(addr >> 24) & 0xFF;
    }
    spi_cmd[counter++] = (addr >> 16) & 0xFF;
    spi_cmd[counter++] = (addr >> 8) & 0xFF;
    spi_cmd[counter++] = addr & 0xFF;

    status = _w25q_send_data(spi_cmd, counter);
    if (status != FLASH_OK) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash read addr=%08lX len=%lu: error=%u (send command)", addr, len, status);
#endif
        return status;
    }

    if (data && len) {
    	status = _w25q_recieve_data(data, len);
    }

    if (status != FLASH_OK) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "flash read addr=%08lX len=%lu: error=%u (recieve data)", addr, len, status);
#endif
    }

    return status;
}

flash_status_t _w25q_read_jdec_id(uint32_t* jdec_id)
{
#if W25Q_BEDUG
	printTagLog(W25Q_TAG, "get JEDEC ID: begin");
#endif

    flash_status_t status = FLASH_BUSY;
    if (!util_wait_event(_w25q_check_FREE, W25Q_SPI_TIMEOUT_MS)) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "get JEDEC ID error (W25Q busy)");
#endif
        goto do_spi_stop;
    }

	_W25Q_CS_set();
    uint8_t spi_cmd[] = { W25Q_CMD_JEDEC_ID };
    status = _w25q_send_data(spi_cmd, sizeof(spi_cmd));
    if (status != FLASH_OK) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "get JDEC ID error=%u (send command)", status);
#endif
        goto do_spi_stop;
    }

    uint8_t data[W25Q_JEDEC_ID_SIZE] = { 0 };
    status = _w25q_recieve_data(data, sizeof(data));
    if (status != FLASH_OK) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "get JDEC ID error=%u (recieve data)", status);
#endif
        goto do_spi_stop;
    }

    *jdec_id = ((((uint32_t)data[0]) << 16) | (((uint32_t)data[1]) << 8) | ((uint32_t)data[2]));

do_spi_stop:
	_W25Q_CS_reset();

    return status;
}

flash_status_t _w25q_read_SR1(uint8_t* SR1)
{
    bool cs_enabled = !(bool)HAL_GPIO_ReadPin(GSYSTEM_FLASH_CS_PORT, GSYSTEM_FLASH_CS_PIN);
	if (cs_enabled) {
	    _W25Q_CS_reset();
	}
    _W25Q_CS_set();

    uint8_t spi_cmd[] = { W25Q_CMD_READ_SR1 };
    HAL_StatusTypeDef status = HAL_SPI_Transmit(&GSYSTEM_FLASH_SPI, spi_cmd, sizeof(spi_cmd), W25Q_SPI_TIMEOUT_MS);
    if (status != HAL_OK) {
        goto do_spi_stop;
    }

    status = HAL_SPI_Receive(&GSYSTEM_FLASH_SPI, SR1, sizeof(uint8_t), W25Q_SPI_TIMEOUT_MS);
    if (status != HAL_OK) {
        goto do_spi_stop;
    }

do_spi_stop:
	_W25Q_CS_reset();
	if (cs_enabled) {
		_W25Q_CS_set();
	}
    if (status == HAL_BUSY) {
    	return FLASH_BUSY;
    }
    if (status != HAL_OK) {
    	return FLASH_ERROR;
    }

    return FLASH_OK;
}

flash_status_t _w25q_write_enable()
{
    if (!util_wait_event(_w25q_check_FREE, W25Q_SPI_TIMEOUT_MS)) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "write enable error (W25Q busy)");
#endif
        return FLASH_BUSY;
    }

    uint8_t spi_cmd[] = { W25Q_CMD_WRITE_ENABLE };
    flash_status_t status = _w25q_send_data(spi_cmd, sizeof(spi_cmd));
#if W25Q_BEDUG
    if (status != FLASH_OK) {
        printTagLog(W25Q_TAG, "write enable error=%u", status);
    }
#endif

    return status;
}

flash_status_t _w25q_write_disable()
{
    if (!util_wait_event(_w25q_check_FREE, W25Q_SPI_TIMEOUT_MS)) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "write disable error (W25Q busy)");
#endif
        return FLASH_BUSY;
    }

    uint8_t spi_cmd[] = { W25Q_CMD_WRITE_DISABLE };
    flash_status_t status = _w25q_send_data(spi_cmd, sizeof(spi_cmd));
#if W25Q_BEDUG
    if (status != FLASH_OK) {
        printTagLog(W25Q_TAG, "write disable error=%u", status);
    }
#endif

    return status;
}

flash_status_t _w25q_erase_sector(uint32_t addr)
{
#if W25Q_BEDUG
	printTagLog(W25Q_TAG, "flash erase sector addr=%08lX: begin", addr);
#endif

    if (addr % w25q.sector_size > 0) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "erase sector addr=%08lX error (unacceptable address)", addr);
#endif
        return FLASH_ERROR;
    }

    if (!util_wait_event(_w25q_check_FREE, W25Q_SPI_TIMEOUT_MS)) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "erase sector addr=%08lX error (flash is busy)", addr);
#endif
        return FLASH_BUSY;
    }

    flash_status_t status = _w25q_set_protect_block(W25Q_SR1_UNBLOCK_VALUE);
    if (status != FLASH_OK) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "erase sector addr=%08lX error=%u (unset block protect)", addr, status);
#endif
        goto do_spi_stop;
    }

    status = _w25q_write_enable();
    if (status != FLASH_OK) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "erase sector addr=%08lX error=%u (write is not enabled)", addr, status);
#endif
        goto do_spi_stop;
    }

    if (!util_wait_event(_w25q_check_WEL, W25Q_SPI_TIMEOUT_MS)) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "erase sector addr=%08lX error=%u (WEL bit wait time exceeded)", addr, FLASH_BUSY);
#endif
        status = FLASH_BUSY;
        goto do_spi_stop;
    }

    if (!util_wait_event(_w25q_check_FREE, W25Q_SPI_TIMEOUT_MS)) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "erase sector addr=%08lX error=%u (BUSY bit wait time exceeded)", addr, FLASH_BUSY);
#endif
        status = FLASH_BUSY;
        goto do_spi_stop;
    }

    uint8_t spi_cmd[W25Q_SPI_COMMAND_SIZE_MAX] = { 0 };
    uint8_t counter = 0;
    spi_cmd[counter++] = W25Q_CMD_ERASE_SECTOR;
    if (w25q.is_24bit_address) {
        spi_cmd[counter++] = (uint8_t)(addr >> 24) & 0xFF;
    }
    spi_cmd[counter++] = (addr >> 16) & 0xFF;
    spi_cmd[counter++] = (addr >> 8) & 0xFF;
    spi_cmd[counter++] = addr & 0xFF;
    status = _w25q_send_data(spi_cmd, counter);
    if (status != FLASH_OK) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "erase sector addr=%08lX error=%u (write is not enabled)", addr, status);
#endif
        goto do_spi_stop;
    }

    status = _w25q_write_disable();
    if (status != FLASH_OK) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "erase sector addr=%08lX error=%u (write is not disabled)", addr, status);
#endif
        goto do_spi_stop;
    }

do_spi_stop:
    if (status != FLASH_OK) {
        goto do_block_protect;
    }

    flash_status_t tmp_status = FLASH_OK;
do_block_protect:
    tmp_status = _w25q_set_protect_block(W25Q_SR1_BLOCK_VALUE);
    if (status == FLASH_OK) {
        status = tmp_status;
    } else {
        return status;
    }

#if W25Q_BEDUG
    if (status == FLASH_OK) {
    	printTagLog(W25Q_TAG, "flash erase sector addr=%08lX: OK", addr);
    } else {
        printTagLog(W25Q_TAG, "erase sector addr=%08lX error=%u (set block protected)", addr, status);
        status = FLASH_BUSY;
    }
#endif

    return status;
}

flash_status_t _w25q_set_protect_block(uint8_t value)
{
    if (!util_wait_event(_w25q_check_FREE, W25Q_SPI_TIMEOUT_MS)) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "set protect block value=%02X error (W25Q busy)", value);
#endif
        return FLASH_BUSY;
    }

    uint8_t spi_cmd_01[] = { W25Q_CMD_WRITE_ENABLE_SR };

    flash_status_t status = _w25q_send_data(spi_cmd_01, sizeof(spi_cmd_01));
    if (status != FLASH_OK) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "set protect block value=%02X error=%u (enable write SR1)", value, status);
#endif
        goto do_spi_stop;
    }


    uint8_t spi_cmd_02[] = { W25Q_CMD_WRITE_SR1, ((value & 0x0F) << 2) };

    status = _w25q_send_data(spi_cmd_02, sizeof(spi_cmd_02));
    if (status != FLASH_OK) {
#if W25Q_BEDUG
        printTagLog(W25Q_TAG, "set protect block value=%02X error=%u (write SR1)", value, status);
#endif
    }

do_spi_stop:
    return status;
}

flash_status_t _w25q_send_data(const uint8_t* data, const uint32_t len)
{
    HAL_StatusTypeDef status = HAL_SPI_Transmit(&GSYSTEM_FLASH_SPI, (uint8_t*)data, (uint16_t)len, W25Q_SPI_TIMEOUT_MS);

    if (status == HAL_BUSY) {
    	return FLASH_BUSY;
    }
    if (status != HAL_OK) {
    	return FLASH_ERROR;
    }

    return FLASH_OK;
}

flash_status_t _w25q_recieve_data(uint8_t* data, uint32_t len)
{
    HAL_StatusTypeDef status =  HAL_SPI_Receive(&GSYSTEM_FLASH_SPI, data, (uint16_t)len, W25Q_SPI_TIMEOUT_MS);

    if (status == HAL_BUSY) {
    	return FLASH_BUSY;
    }
    if (status != HAL_OK) {
    	return FLASH_ERROR;
    }

    return FLASH_OK;
}

void _W25Q_CS_set()
{
    HAL_GPIO_WritePin(GSYSTEM_FLASH_CS_PORT, GSYSTEM_FLASH_CS_PIN, GPIO_PIN_RESET);
}

void _W25Q_CS_reset()
{
    HAL_GPIO_WritePin(GSYSTEM_FLASH_CS_PORT, GSYSTEM_FLASH_CS_PIN, GPIO_PIN_SET);
}

bool _w25q_check_FREE()
{
    uint8_t SR1 = 0x00;
    flash_status_t status = _w25q_read_SR1(&SR1);
    if (status != FLASH_OK) {
        return false;
    }

    return !(SR1 & W25Q_SR1_BUSY);
}

bool _w25q_check_WEL()
{
    uint8_t SR1 = 0x00;
    flash_status_t status = _w25q_read_SR1(&SR1);
    if (status != FLASH_OK) {
        return false;
    }

    return SR1 & W25Q_SR1_WEL;
}

bool _w25q_initialized()
{
	return w25q.initialized;
}

bool _w25q_24bit()
{
	return w25q.is_24bit_address;
}


#endif
