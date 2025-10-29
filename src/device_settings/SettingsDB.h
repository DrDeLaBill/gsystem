#ifndef SETTINGS_DB_H
#define SETTINGS_DB_H


#include "gdefines.h"
#include "gconfig.h"

#include <cstdint>

#include "g_settings.h"


class SettingsDB
{
private:
    static const uint32_t TIMEOUT_MS = 300;
    static const char* TAG;
#if defined(GSYSTEM_NO_STORAGE_AT)
    static const char* FILENAME1;
    static const char* FILENAME2;
#else
    static const char* PREFIX;
#endif

	const uint32_t size;
    uint8_t* settings;
    bool needResaveFirst;
	bool needResaveSecond;

    bool check(device_settings_storage_t* settings);

    SettingsDB(uint8_t* settings, uint32_t size);

public:
    static SettingsDB& get();

    GSettingsStatus load();
    GSettingsStatus save();

    bool needSave();
};


#endif
