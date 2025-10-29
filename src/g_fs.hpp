/* Copyright © 2025 Georgy E. All rights reserved. */

#ifndef _G_FS_F_
#define _G_FS_H_


#include "gdefines.h"
#include "gconfig.h"


#if defined(GSYSTEM_NO_STORAGE_AT)

#include <cstdint>

#include "g_settings.h"


typedef enum _GFSStatus {
	STORAGE_OK = 0,
    STORAGE_NO_FILE,
	STORAGE_ERROR
} GFSStatus;


class G_FS {
public:
	virtual GFSStatus read(const char* filename, uint8_t* data, const uint32_t size, uint32_t* cnt) = 0;
	virtual GFSStatus write(const char* filename, const uint8_t* data, const uint32_t size)         = 0;
	virtual GFSStatus rm(const char* filename)                                                      = 0;
};


#endif


#endif