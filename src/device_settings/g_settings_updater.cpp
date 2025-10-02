/* Copyright © 2025 Georgy E. All rights reserved. */

#include "g_settings.h"

#ifndef GSYSTEM_NO_DEVICE_SETTINGS

#include <cstring>

#include "glog.h"
#include "soul.h"
#include "fsm_gc.h"
#include "gsystem.h"

#include "Timer.h"
#include "storage.hpp"
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


#if DEBUG
static const char TAG[] = "STGw";
#endif


static unsigned old_hash = 0;
static utl::Timer saveTimer(SAVE_DELAY_MS);
static const char FILENAME[] = "settings.bin";

extern Storage storage;


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
	return util_hash(device_settings_storage.gs_settings_bytes.data, settings_size());
}

void _stng_init_s(void)
{
	if (!is_status(MEMORY_INITIALIZED)) {
		return;
	}

	uint32_t cnt = 0;
	STORAGE_STATUS status = storage.read(FILENAME, (uint8_t*)&device_settings_storage, sizeof(device_settings_storage), &cnt);
	if (status == STORAGE_OK) {
		SYSTEM_BEDUG("settings loaded");
		if (!_g_settings_check(&device_settings_storage)) {
			SYSTEM_BEDUG("settings check fail");
			status = STORAGE_ERROR;
		}
	}

	if (status != STORAGE_OK) {
		SYSTEM_BEDUG("settings repair");
		_g_settings_repair(&device_settings_storage);
		_g_settings_before_save(&device_settings_storage);
		status = storage.write(FILENAME, (uint8_t*)&device_settings_storage, sizeof(device_settings_storage));
	}

	if (status == STORAGE_OK) {
		SYSTEM_BEDUG("settings OK");

		reset_error(SETTINGS_LOAD_ERROR);
		_g_settings_show();

		set_status(SETTINGS_INITIALIZED);
		set_status(SYSTEM_SOFTWARE_READY);

		_stng_check();
		fsm_gc_push_event(&stng_fsm, &stng_updated_e);
	} else {
		SYSTEM_BEDUG("settings error");
		set_error(SETTINGS_LOAD_ERROR);
	}
}

void _stng_idle_s(void)
{
	bool save_needed = has_new_settings() && !is_status(SETTINGS_STOPPED);
	if (!save_needed) {
		saveTimer.start();
	}
	if (
		save_needed && 
		(is_status(NEED_SAVE_SETTINGS) || !saveTimer.wait())
	) {
		SYSTEM_BEDUG("settings needs save");
#ifdef EMULATOR
		reset_status(NEED_SAVE_SETTINGS);
#else
		reset_status(SYSTEM_SOFTWARE_READY);
#endif
		_stng_check();
#ifndef EMULATOR
		fsm_gc_push_event(&stng_fsm, &stng_updated_e);
#endif
	} else if (is_status(NEED_LOAD_SETTINGS)) {
		SYSTEM_BEDUG("settings needs load");
#ifdef EMULATOR
		reset_status(NEED_LOAD_SETTINGS);
#else
		reset_status(SYSTEM_SOFTWARE_READY);
#endif
		_stng_check();
#ifndef EMULATOR
		fsm_gc_push_event(&stng_fsm, &stng_saved_e);
#endif
	}
}

void _stng_save_s(void)
{
	bool status = G_SETTINGS_OK;
	if (old_hash != _get_new_hash()) {
		SYSTEM_BEDUG("settings is saving");
		_g_settings_before_save(&device_settings_storage);
		status = storage.write(FILENAME, (uint8_t*)&device_settings_storage, sizeof(device_settings_storage));
	}
	if (status == STORAGE_OK) {
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
	uint32_t cnt = 0;
	SYSTEM_BEDUG("settings is loading");
	STORAGE_STATUS status = storage.read(FILENAME, (uint8_t*)&device_settings_storage, sizeof(device_settings_storage), &cnt);
	if (status == STORAGE_OK) {
		SYSTEM_BEDUG("settings OK");
		_stng_check();
		fsm_gc_push_event(&stng_fsm, &stng_updated_e);

		_g_settings_show();

		reset_error(SETTINGS_LOAD_ERROR);
		reset_status(NEED_LOAD_SETTINGS);

		set_status(SYSTEM_SOFTWARE_READY);
	} else {
		SYSTEM_BEDUG("settings error");
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
