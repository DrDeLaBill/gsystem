/* Copyright © 2025 Georgy E. All rights reserved. */

#include "g_settings.h"

#ifndef GSYSTEM_NO_DEVICE_SETTINGS

#include <stdio.h>
#include <string.h>

#include "glog.h"
#include "clock.h"
#include "gutils.h"
#include "gsystem.h"
#include "drivers.h"

#include "settings.h"



#define DEFAULT_CHAR_SETIINGS_SIZE (30)
#define G_SETTINGS_BEDACODE        ((uint32_t)0xBEDAC0DE)


static unsigned _get_settings_payload_size();
static uint16_t _get_settings_hash(device_settings_storage_t* const other);


device_settings_storage_t device_settings_storage = 
{
    .gs_settings.bedacode = G_SETTINGS_BEDACODE,
    .gs_settings.dv_type  = GSYSTEM_DEVICE_TYPE,
    .gs_settings.fw_id    = GSYSTEM_FW_VERSION,
    .gs_settings.stg_id   = GSYSTEM_STG_VERSION,
    .gs_settings.data     = { 0 },
    .gs_settings.crc      = 0
};

settings_t* settings = (settings_t*)device_settings_storage.gs_settings.data;


settings_t* get_settings()
{
	return (settings_t*)device_settings_storage.gs_settings.data;
}

void set_settings(settings_t* other)
{
	memcpy((uint8_t*)settings, (uint8_t*)other, sizeof(settings));
}

uint32_t settings_size()
{
	return sizeof(device_settings_storage.gs_settings_bytes.data);
}

void __attribute__((weak)) settings_before_save(settings_t* const other)
{
	(void)other;
}

void _g_settings_before_save(device_settings_storage_t* const other)
{
    settings_before_save((settings_t*)other->gs_settings.data);
	uint16_t crc = _get_settings_hash(other);
	SYSTEM_BEDUG("new settings CRC16=%u", crc);
    other->gs_settings_bytes.crc = crc;
}

bool _g_settings_check(device_settings_storage_t* const other)
{
	if (other->gs_settings.bedacode != G_SETTINGS_BEDACODE) {
		SYSTEM_BEDUG("check settings error: bedacode 0x%08X != 0x%08X", other->gs_settings.bedacode, G_SETTINGS_BEDACODE);
		return false;
	}
	if (other->gs_settings.dv_type != GSYSTEM_DEVICE_TYPE) {
		SYSTEM_BEDUG("check settings error: DEVICE_TYPE %u != %u", other->gs_settings.dv_type, GSYSTEM_DEVICE_TYPE);
		return false;
	}
	if (other->gs_settings.stg_id != GSYSTEM_STG_VERSION) {
		SYSTEM_BEDUG("check settings error: STG_VERSION %u != %u", other->gs_settings.stg_id, GSYSTEM_STG_VERSION);
		return false;
	}
	if (other->gs_settings.fw_id != GSYSTEM_FW_VERSION) {
		SYSTEM_BEDUG("check settings error: FW_VERSION %u != %u", other->gs_settings.fw_id, GSYSTEM_FW_VERSION);
		return false;
	}
	uint16_t crc = _get_settings_hash(other);
    if (other->gs_settings_bytes.crc != crc) {
		SYSTEM_BEDUG("check settings error: crc %u != %u", other->gs_settings_bytes.crc, crc);
        return false;
    }
    return settings_check((settings_t* const)other->gs_settings.data);
}

void _g_settings_repair(device_settings_storage_t* const other)
{
    settings_t* const stg = (settings_t* const)other->gs_settings.data;
    settings_repair(stg, other->gs_settings.stg_id);
	if (!settings_check(stg)) {
		settings_reset(stg);
	} else {
		other->gs_settings.bedacode = G_SETTINGS_BEDACODE;
		other->gs_settings.dv_type  = GSYSTEM_DEVICE_TYPE;
		other->gs_settings.stg_id   = GSYSTEM_STG_VERSION;
		other->gs_settings.fw_id    = GSYSTEM_FW_VERSION;
	}
}

void _g_settings_reset(device_settings_storage_t* const other)
{
	SYSTEM_BEDUG("Reset settings");

	other->gs_settings.bedacode = G_SETTINGS_BEDACODE;
	other->gs_settings.dv_type  = GSYSTEM_DEVICE_TYPE;
	other->gs_settings.stg_id   = GSYSTEM_STG_VERSION;
	other->gs_settings.fw_id    = GSYSTEM_FW_VERSION;

    settings_reset((settings_t* const)other->gs_settings.data);
}

void device_settings_show()
{
#if defined(GSYSTEM_BEDUG)
    printPretty("######################SETTINGS######################\n");
	printPretty("Device version:                     %s\n", BUILD_VERSION);
	printPretty("Device type:                        %u\n", device_settings_storage.gs_settings.dv_type);
	printPretty("Firmware ID:                        %u\n", device_settings_storage.gs_settings.fw_id);
	printPretty("Device serial:                      %s\n", get_system_serial_str());
	printPretty("Settings version:                   %u\n", device_settings_storage.gs_settings.stg_id);
    settings_show();
	printPretty("CRC16:                              %u\n", device_settings_storage.gs_settings.crc);
	printPretty("######################SETTINGS######################\n");
#endif // #if defined(GSYSTEM_BEDUG)
}

unsigned _get_settings_payload_size()
{
	return sizeof(gs_settings_bytes_t) - sizeof(uint16_t);
}

uint16_t _get_settings_hash(device_settings_storage_t* const other)
{
	return (uint16_t)(util_hash(other->gs_settings_bytes.data, _get_settings_payload_size()) & 0xFFFF);
}

#endif // #ifndef GSYSTEM_NO_DEVICE_SETTINGS
