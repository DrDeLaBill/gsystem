/* Copyright © 2023 Georgy E. All rights reserved. */

#ifndef _STORAGE_DRIVER_H_
#define _STORAGE_DRIVER_H_

#ifndef GSYSTEM_NO_MEMORY_W

#include <stdint.h>

#include "gconfig.h"

#include "Timer.h"
#include "StorageAT.h"

#ifdef GSYSTEM_EEPROM_MODE
#    include "at24cm01.h"
#elif defined(GSYSTEM_FLASH_MODE)
#    include "w25qxx.h"
#else
#    warning "Storage driver mode has not selected"
#endif


#ifdef DEBUG
#   define STORAGE_DRIVER_BEDUG   (0)
#endif

#define STORAGE_DRIVER_USE_BUFFER (1)


struct StorageDriver: public IStorageDriver
{
private:
	static constexpr char TAG[] = "DRVR";

	static bool hasError;
	static utl::Timer timer;

#if STORAGE_DRIVER_USE_BUFFER
    static bool     hasBuffer;
    static uint8_t  bufferPage[STORAGE_PAGE_SIZE];
    static uint32_t lastAddress;
#endif

public:
    StorageStatus read(const uint32_t address, uint8_t *data, const uint32_t len) override;
    StorageStatus write(const uint32_t address, const uint8_t *data, const uint32_t len) override;
    StorageStatus erase(const uint32_t*, const uint32_t) override;
};

#endif

#endif
