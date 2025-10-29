#include "SettingsDB.h"

#include "gdefines.h"
#include "gconfig.h"

#ifndef GSYSTEM_NO_DEVICE_SETTINGS

#include <cstring>
#include <algorithm>

#include "glog.h"
#include "soul.h"
#include "gutils.h"
#include "gsystem.h"
#include "g_settings.h"

#include "CodeStopwatch.h"

#if !defined(GSYSTEM_NO_STORAGE_AT)
#include "StorageAT.h"
#include "StorageDriver.h"
extern StorageAT* storage;
using status_t = StorageStatus;
#else
#include "g_fs.hpp"
extern G_FS* storage;
using status_t = GFSStatus;
#endif


const char* SettingsDB::TAG = "STG";
#if defined(GSYSTEM_NO_STORAGE_AT)
const char* SettingsDB::FILENAME1 = "settings1.bin";
const char* SettingsDB::FILENAME2 = "settings2.bin";
#else
const char* SettingsDB::PREFIX = "STG";
#endif


extern "C" bool _g_settings_check(device_settings_storage_t* const other);
extern "C" void _g_settings_repair(device_settings_storage_t* const other);


extern device_settings_storage_t device_settings_storage;


SettingsDB::SettingsDB(uint8_t* settings, uint32_t size):
	size(size), settings(settings), needResaveFirst(false), needResaveSecond(false)
{}

SettingsDB& SettingsDB::get()
{
    static SettingsDB settingsDB((uint8_t*)&device_settings_storage, sizeof(device_settings_storage));
    return settingsDB;
}

bool SettingsDB::check(device_settings_storage_t* settings)
{
    if (!_g_settings_check(settings)) {
    	_g_settings_repair(settings);
    	return _g_settings_check(settings);
    }
    return true;
}

GSettingsStatus SettingsDB::load()
{
	status_t status = STORAGE_OK;
    uint8_t tmpSettings[size] = {};

	needResaveFirst = false;
	needResaveSecond = false;

#if !defined(GSYSTEM_NO_STORAGE_AT)
    uint8_t tmpSettings[size] = {};
	uint32_t address1 = 0, address2 = 0;
    status = storage->find(FIND_MODE_EQUAL, &address1, PREFIX, 1);
    if (status != STORAGE_OK) {
        SYSTEM_BEDUG("STG BD load 1: find err=%02X", status);
        needResaveFirst = true;
    }

    status = storage->find(FIND_MODE_EQUAL, &address2, PREFIX, 2);
    if (status != STORAGE_OK) {
        SYSTEM_BEDUG("STG BD load 2: find err=%02X", status);
        needResaveSecond = true;
    }

    status = STORAGE_NOT_FOUND;
    if (!needResaveFirst) {
        status = storage->load(address1, (uint8_t*)&tmpSettings, this->size);
        if (status != STORAGE_OK) {
            SYSTEM_BEDUG("STG BD load 1: err=%02X addr1=%lu addr2=%lu", status, address1, address2);
			status = STORAGE_ERROR;
        } else if (!check(tmpSettings)) {
			status = STORAGE_ERROR;
		}
    }

    if (!needResaveSecond && status != STORAGE_OK) {
        status = storage->load(address2, (uint8_t*)&tmpSettings, this->size);
        if (status != STORAGE_OK) {
            SYSTEM_BEDUG("STG BD load 2: err=%02X addr1=%lu, addr2=%lu", status, address1, address2);
			status = STORAGE_ERROR;
        } else if (!check(tmpSettings)) {
			status = STORAGE_ERROR;
		}
    }
#else
    uint32_t cnt = 0;
    uint8_t tmpSettings2[size] = {};
    status = storage->read(FILENAME1, (uint8_t*)&tmpSettings, size, &cnt);
    if (status != STORAGE_OK) {
        SYSTEM_BEDUG("STG BD load 1: find err=%02X", status);
        needResaveFirst = true;
    }

    status = storage->read(FILENAME2, (uint8_t*)&tmpSettings2, size, &cnt);
    if (status != STORAGE_OK) {
        SYSTEM_BEDUG("STG BD load 2: find err=%02X", status);
        needResaveSecond = true;
    }

    status = STORAGE_OK;
    bool settings_check = check((device_settings_storage_t*)tmpSettings);
    if (!needResaveFirst && !settings_check) {
        SYSTEM_BEDUG("STG BD check 1: err=%02X", status);
        status = STORAGE_ERROR;
    } else if (status != STORAGE_OK) {
        status = STORAGE_ERROR;
    }

    if (!needResaveSecond && status != STORAGE_OK && check((device_settings_storage_t*)tmpSettings2)) {
        memcpy((uint8_t*)&tmpSettings, (uint8_t*)&tmpSettings2, this->size);
    }   


#endif

    if (status != STORAGE_OK) {
        SYSTEM_BEDUG("STG BD load: err=%02X", status);
        return G_SETTINGS_ERROR;
    }

    memcpy(this->settings, (uint8_t*)&tmpSettings, this->size);

    SYSTEM_BEDUG("STG BD loaded");

    return G_SETTINGS_OK;
}

GSettingsStatus SettingsDB::save()
{
	utl::CodeStopwatch stopwatch(TAG, TIMEOUT_MS);

    status_t status; 

#if !defined(GSYSTEM_NO_STORAGE_AT)
	uint32_t address = 0;
	GStorageStatus status = G_STORAGE_OK;
    status = storage->find(FIND_MODE_EQUAL, &address, PREFIX, 1);
    if (status == STORAGE_NOT_FOUND) {
        SYSTEM_BEDUG("STG BD save 1: find err=%02X", status);
        status = storage->find(FIND_MODE_EMPTY, &address);
    }
    // Search for any address
    if (status == STORAGE_NOT_FOUND) {
        SYSTEM_BEDUG("STG BD save 1: empty err=%02X", status);
    	status = storage->find(FIND_MODE_NEXT, &address, "", 0);
    }
    if (status != STORAGE_OK) {
        SYSTEM_BEDUG("STG BD save 1: any err=%02X", status);
        return G_SETTINGS_ERROR;
    }
    // Save original settings
	status = storage->rewrite(address, PREFIX, 1, this->settings, this->size);
    if (status != STORAGE_OK) {
        SYSTEM_BEDUG("STG BD save 1: err=%02X addr=%lu", status, address);
        return G_SETTINGS_ERROR;
    }

    // Save 2 settings
	status = storage->find(FIND_MODE_EQUAL, &address, PREFIX, 2);
    if (status == STORAGE_NOT_FOUND) {
        SYSTEM_BEDUG("STG BD save 2: find err=%02X", status);
        status = storage->find(FIND_MODE_EMPTY, &address);
    }
    // Search for any address
    if (status == STORAGE_NOT_FOUND) {
        SYSTEM_BEDUG("STG BD save 2: empty err=%02X", status);
    	status = storage->find(FIND_MODE_NEXT, &address, "", 0);
    }
    if (status != STORAGE_OK) {
        SYSTEM_BEDUG("STG BD save 2: any err=%02X", status);
        return G_SETTINGS_ERROR;
    }
	status = storage->rewrite(address, PREFIX, 2, this->settings, this->size);
    if (status != STORAGE_OK) {
        SYSTEM_BEDUG("STG BD save 2: storage save err=%02X addr=%lu", status, address);
        return G_SETTINGS_ERROR;
    }
#else
    status = storage->write(FILENAME1, settings, size);
    if (status != STORAGE_OK) {
        SYSTEM_BEDUG("STG BD save 1: err=%02X", status);
        return G_SETTINGS_ERROR;
    }

    // Save 2 settings
	status = storage->write(FILENAME2, settings, size);
    if (status != STORAGE_OK) {
        SYSTEM_BEDUG("STG BD save 2: storage save err=%02X", status);
        return G_SETTINGS_ERROR;
    }
#endif

	SYSTEM_BEDUG("STG BD saved");

	GSettingsStatus res = load();
    if (res != G_SETTINGS_OK || needSave()) {
	SYSTEM_BEDUG("STG BD save error");
    	return G_SETTINGS_ERROR;
    }
    return G_SETTINGS_OK;
}

bool SettingsDB::needSave()
{
	return needResaveFirst || needResaveSecond;
}

#endif