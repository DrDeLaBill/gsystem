/* Copyright © 2024 Georgy E. All rights reserved. */

#include "gconfig.h"


#ifndef GSYSTEM_NO_MEMORY_W

#   include "StorageAT.h"
#   include "StorageDriver.h"

StorageDriver storageDriver;
StorageAT storage(
#   if defined(GSYSTEM_EEPROM_MODE)
	EEPROM_PAGES_COUNT,
#   elif defined(GSYSTEM_FLASH_MODE)
	0,
#   else
#       error "Memory mode is not selected"
	0,
#   endif

	&storageDriver,

#   if defined(GSYSTEM_EEPROM_MODE)
	EEPROM_PAGE_SIZE
#   elif defined(GSYSTEM_FLASH_MODE)
	FLASH_W25_SECTOR_SIZE
#   else
	0
#   endif
);

extern "C" void memory_watchdog_check()
{
	static const uint32_t TIMEOUT_MS = 15000;
	static const unsigned ERRORS_MAX = 5;

	static utl::Timer errorTimer(TIMEOUT_MS);
	static utl::Timer timer(SECOND_MS);
	static uint8_t errors = 0;
	static bool timerStarted = false;

	if (!is_system_ready() && !is_error(MEMORY_ERROR) && !is_error(EXPECTED_MEMORY_ERROR)) {
		return;
	}

	uint8_t data = 0;
#ifdef GSYSTEM_EEPROM_MODE
	eeprom_status_t status = EEPROM_OK;
#else
	flash_status_t status = FLASH_OK;
#endif

#ifndef GSYSTEM_EEPROM_MODE
	if (!is_status(MEMORY_INITIALIZED)) {
		if (flash_w25qxx_init() == FLASH_OK) {
			set_status(MEMORY_INITIALIZED);
			storage.setPagesCount(flash_w25qxx_get_pages_count());
#   ifdef GSYSTEM_BEDUG
			printTagLog(TAG, "flash init success (%lu pages)", flash_w25qxx_get_pages_count());
#   endif
		} else {
#   ifdef GSYSTEM_BEDUG
			printTagLog(TAG, "flash init error");
#   endif
		}
		return;
	}
#else
	set_status(MEMORY_INITIALIZED);
#endif

	if (is_status(MEMORY_READ_FAULT) ||
		is_status(MEMORY_WRITE_FAULT) ||
		is_error(MEMORY_ERROR) ||
		is_error(EXPECTED_MEMORY_ERROR)
	) {
#ifdef GSYSTEM_EEPROM_MODE
		system_reset_i2c_errata();

		uint32_t address = static_cast<uint32_t>(rand()) % eeprom_get_size();

		status = eeprom_read(address, &data, sizeof(data));
		if (status == EEPROM_OK) {
			reset_status(MEMORY_READ_FAULT);
			status = eeprom_write(address, &data, sizeof(data));
		} else {
			errors++;
		}
		if (status == EEPROM_OK) {
			reset_status(MEMORY_WRITE_FAULT);
			timerStarted = false;
			errors = 0;
		} else {
			errors++;
		}
#elif defined(GSYSTEM_FLASH_MODE)
		if (is_status(MEMORY_INITIALIZED) && flash_w25qxx_init() != FLASH_OK) {
			reset_status(MEMORY_INITIALIZED);
		}

		uint32_t address = static_cast<uint32_t>(rand()) % (flash_w25qxx_get_pages_count() * FLASH_W25_PAGE_SIZE);

		status = flash_w25qxx_read(address, &data, sizeof(data));
		if (status == FLASH_OK) {
			reset_status(MEMORY_READ_FAULT);
			status = flash_w25qxx_write(address, &data, sizeof(data));
		} else {
			errors++;
		}
		if (status == FLASH_OK) {
			reset_status(MEMORY_WRITE_FAULT);
			reset_error(EXPECTED_MEMORY_ERROR);
			timerStarted = false;
			errors = 0;
		} else {
			errors++;
		}
#endif
	}

	(errors > ERRORS_MAX) ? set_error(MEMORY_ERROR) : reset_error(MEMORY_ERROR);

	if (!timerStarted && is_error(MEMORY_ERROR)) {
		timerStarted = true;
		errorTimer.start();
	}

	if (timerStarted && !errorTimer.wait()) {
		system_error_handler(MEMORY_ERROR);
	}
}

#endif