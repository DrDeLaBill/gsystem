/* Copyright © 2025 Georgy E. All rights reserved. */

#include "g_settings.h"

#include "gdefines.h"
#include "gconfig.h"

#ifndef GSYSTEM_NO_DEVICE_SETTINGS

#include <cstring>

#include "glog.h"
#include "soul.h"
#include "fsm_gc.h"
#include "gsystem.h"

#include "Timer.h"
#include "SettingsDB.h"
#include "CodeStopwatch.h"


#define SAVE_DELAY_MS (5 * SECOND_MS)
#define TIMEOUT_MS    (100)


static void _stng_check(void);
static unsigned _get_new_hash();

extern "C" void _g_settings_show();
extern "C" bool _g_settings_check(device_settings_storage_t* const other);
extern "C" void _g_settings_repair(device_settings_storage_t* const other);
extern "C" void _g_settings_before_save(device_settings_storage_t* const other);


extern device_settings_storage_t device_settings_storage;


#ifdef DEBUG
static const char TAG[] = "STGw";
#endif


static unsigned old_hash = 0;
static utl::Timer saveTimer(SAVE_DELAY_MS);


FSM_GC_CREATE(stng_fsm)

FSM_GC_CREATE_EVENT(stng_saved_e,   0)
FSM_GC_CREATE_EVENT(stng_updated_e, 1)

FSM_GC_CREATE_STATE(stng_init_s, _stng_init_s)
FSM_GC_CREATE_STATE(stng_idle_s, _stng_idle_s)
FSM_GC_CREATE_STATE(stng_save_s, _stng_save_s)
FSM_GC_CREATE_STATE(stng_load_s, _stng_load_s)

FSM_GC_CREATE_ACTION(stng_update_hash_a, _stng_update_hash_a)

FSM_GC_CREATE_TABLE(
	stng_fsm_table,
	{&stng_init_s, &stng_updated_e, &stng_idle_s, &stng_update_hash_a},

	{&stng_idle_s, &stng_saved_e,   &stng_load_s, NULL},
	{&stng_idle_s, &stng_updated_e, &stng_save_s, NULL},

	{&stng_load_s, &stng_updated_e, &stng_idle_s, &stng_update_hash_a},

	{&stng_save_s, &stng_saved_e,   &stng_load_s, NULL}
)

extern "C" void settings_update()
{
#if SYSTEM_BEDUG
	utl::CodeStopwatch stopwatch(STNGw_TAG, TIMEOUT_MS);
#endif
	if (!stng_fsm._initialized) {
		fsm_gc_init(&stng_fsm, stng_fsm_table, __arr_len(stng_fsm_table));
	}
	fsm_gc_process(&stng_fsm);
}

extern "C" bool settings_ready()
{
	return !is_status(SETTINGS_LOAD_ERROR) &&
           !is_status(NEED_SAVE_SETTINGS) &&
		   !is_status(NEED_LOAD_SETTINGS) &&
		   is_status(SETTINGS_INITIALIZED);
}

bool has_new_settings()
{
	return old_hash != _get_new_hash();
}

void _stng_check(void)
{
	reset_error(SETTINGS_LOAD_ERROR);
	if (!_g_settings_check(&device_settings_storage)) {
		set_error(SETTINGS_LOAD_ERROR);
		SYSTEM_BEDUG("settings check: not valid");
		_g_settings_repair(&device_settings_storage);
		set_status(NEED_SAVE_SETTINGS);
	}
}

unsigned _get_new_hash()
{
	return util_hash(device_settings_storage.gs_settings_bytes.data, sizeof(settings_t));
}

void _stng_init_s(void)
{
	if (!is_status(MEMORY_INITIALIZED)) {
		return;
	}

	SettingsDB& settingsDB = SettingsDB::get();
	GSettingsStatus status = settingsDB.load();
	if (status == G_SETTINGS_OK) {
		SYSTEM_BEDUG("settings loaded");
		if (!_g_settings_check(&device_settings_storage)) {
			status = G_SETTINGS_ERROR;
		}
	}

	if (status != G_SETTINGS_OK) {
		SYSTEM_BEDUG("settings repair");
		_g_settings_repair(&device_settings_storage);
		_g_settings_before_save(&device_settings_storage);
		status = settingsDB.save();
	}

	if (status == G_SETTINGS_OK) {
		SYSTEM_BEDUG("settings OK");
		reset_error(SETTINGS_LOAD_ERROR);
		_g_settings_show();

		set_status(SETTINGS_INITIALIZED);
		set_status(SYSTEM_SOFTWARE_READY);

		fsm_gc_push_event(&stng_fsm, &stng_updated_e);
	} else {
		SYSTEM_BEDUG("settings error");
		set_error(SETTINGS_LOAD_ERROR);
	}
}

void _stng_idle_s(void)
{
	bool has_new_stg = has_new_settings();
	bool save_needed = has_new_stg && !is_status(SETTINGS_STOPPED);
	if (!has_new_stg && is_status(NEED_SAVE_SETTINGS)) {
		reset_status(NEED_SAVE_SETTINGS);
	}
	if (!save_needed) {
		saveTimer.start();
	}
	if (
		save_needed && 
		(is_status(NEED_SAVE_SETTINGS) || !saveTimer.wait())
	) {
		SYSTEM_BEDUG("settings needs save");
		reset_status(SYSTEM_SOFTWARE_READY);
		_stng_check();
		fsm_gc_push_event(&stng_fsm, &stng_updated_e);
	} else if (is_status(NEED_LOAD_SETTINGS)) {
		reset_status(SYSTEM_SOFTWARE_READY);
		_stng_check();
		fsm_gc_push_event(&stng_fsm, &stng_saved_e);
	}
}

void _stng_save_s(void)
{
	GSettingsStatus status = G_SETTINGS_OK;
	SettingsDB& settingsDB = SettingsDB::get();
	if (old_hash != _get_new_hash()) {
		SYSTEM_BEDUG("settings is saving");
		_g_settings_before_save(&device_settings_storage);
		_stng_check();
		status = settingsDB.save();
	}
	if (status == G_SETTINGS_OK) {
		SYSTEM_BEDUG("settings saved");
		_stng_check();
		fsm_gc_push_event(&stng_fsm, &stng_saved_e);

		_g_settings_show();

		reset_error(SETTINGS_LOAD_ERROR);
		reset_status(NEED_SAVE_SETTINGS);
	}
}

void _stng_load_s(void)
{
	SYSTEM_BEDUG("settings is loading");
	SettingsDB& settingsDB = SettingsDB::get();
	GSettingsStatus status = settingsDB.load();
	if (status == G_SETTINGS_OK) {
		_stng_check();
		fsm_gc_push_event(&stng_fsm, &stng_updated_e);

		_g_settings_show();

		reset_error(SETTINGS_LOAD_ERROR);
		reset_status(NEED_LOAD_SETTINGS);

		set_status(SYSTEM_SOFTWARE_READY);
	} else {
		SYSTEM_BEDUG("settings load error");
	}
}

void _stng_update_hash_a(void)
{
	unsigned new_hash = _get_new_hash();
	if (
		(settings_ready() || !is_status(SETTINGS_INITIALIZED)) &&
		old_hash != new_hash
	) {
		SYSTEM_BEDUG("settings update hash");
		old_hash = new_hash;
	}
	fsm_gc_clear(&stng_fsm);
}

#endif
