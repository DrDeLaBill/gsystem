/* Copyright © 2025 Georgy E. All rights reserved. */

#ifndef _G_SETTINGS_H_
#define _G_SETTINGS_H_


#include "gdefines.h"
#include "gconfig.h"


#ifndef GSYSTEM_NO_DEVICE_SETTINGS


#ifdef __cplusplus
extern "C" {
#endif


#include <stdint.h>
#include <stdbool.h>

#include "settings.h"


#if !defined(BUILD_VERSION)
    #define DEFAULT_DEVICE_MAJOR   (0)
    #define DEFAULT_DEVICE_MINOR   (1)
    #define DEFAULT_DEVICE_PATCH   (0)
#endif

#if !defined(GSYSTEM_DEVICE_TYPE)
    #define GSYSTEM_DEVICE_TYPE    (0)
#endif

#if !defined(GSYSTEM_STG_VERSION)
    #define GSYSTEM_STG_VERSION    ((uint8_t)0x01)
#endif

#if !defined(GSYSTEM_FW_VERSION)
    #define GSYSTEM_FW_VERSION     ((uint8_t)0x01)
#endif


typedef enum _gSettingsStatus {
    G_SETTINGS_OK = 0,
    G_SETTINGS_ERROR
} GSettingsStatus;


typedef struct __attribute__((packed)) _gs_settings_t {
    // Start code
    uint32_t bedacode;
    // Device type
    uint16_t dv_type;
    // Settings version
    uint8_t  stg_id;
    // Firmware version
    uint8_t  fw_id;
    // Settings data
    uint8_t  data[sizeof(settings_t)];
    // Settings CRC16
    uint16_t crc;
} gs_settings_t;

typedef struct __attribute__((packed)) _gs_settings_bytes_t {
    // Settings data
    uint8_t  data[sizeof(gs_settings_t) - sizeof(uint16_t)];
    // Settings CRC16
    uint16_t crc;
} gs_settings_bytes_t;

typedef union _device_settings_storage_t {
    gs_settings_t       gs_settings;
    gs_settings_bytes_t gs_settings_bytes;
} device_settings_storage_t;


extern settings_t* settings;


uint32_t settings_size();

settings_t* get_settings();
void set_settings(settings_t* const other);

void settings_update();
bool settings_ready();
bool has_new_settings();

// Ypu need to create this functions in your settings.c file
bool settings_check(settings_t* const other);
void settings_repair(settings_t* const other, const uint8_t version);
void settings_reset(settings_t* const other);
void settings_before_save(settings_t* const other);
void settings_show();

#ifdef __cplusplus
}
#endif


#endif


#endif
