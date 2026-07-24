// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Tuomas Airaksinen
#pragma once
#include "timer_calc.h"
#include <stdint.h>

#define PERSIST_KEY_SCHEMA    1
#define PERSIST_KEY_COUNT     2
#define PERSIST_KEY_WAKEUPID  3
#define PERSIST_KEY_SORTORDER 4
#define PERSIST_KEY_AUTORETURN_START 5
#define PERSIST_KEY_RUNNINGFIRST 6
#define PERSIST_KEY_IDLEEXIT 7       // idle auto-exit timeout, seconds (0 = off)
#define PERSIST_KEY_EPHEMERAL 8      // bit i => timer i is deleted (not kept) once it finishes/stops
#define PERSIST_KEY_LAUNCHSYNC 9     // launch-sync starts (0/1)
#define PERSIST_KEY_DEFAULT_FINISH_DELETE 10 // default "delete on finish" for newly created timers
#define PERSIST_KEY_RUNONCREATE 11   // run a newly created timer immediately (0/1)
#define PERSIST_KEY_KEYBOARD_NEW_TIMER 12  // show label keyboard after "+ New timer" dial confirm (0/1)
#define PERSIST_KEY_KEYBOARD_MAIN_TOUCH 13 // show label keyboard after main-view touch dial (0/1)
#define PERSIST_KEY_AUTORETURN_STOP 14 // return to watchface after stopping a timer (0/1)
#define PERSIST_KEY_NEXT_LOCAL_ID 15 // counter for watch-assigned Timer.id (see main.c next_watch_timer_id)
#define PERSIST_KEY_VIBE_PATTERN 16  // alarm vibration pattern: 0=Double, 1=Short, 2=Long
#define PERSIST_KEY_AUDIO_VOLUME 17  // alarm beep volume, 0-100 (0 = sound disabled globally)
#define PERSIST_KEY_DEFAULT_VIBRATION_ENABLED 18 // default "Vibration" for newly created timers (0/1)
#define PERSIST_KEY_DEFAULT_SOUND_ENABLED 19      // default "Sound" for newly created timers (0/1)
#define PERSIST_KEY_VIBRATION_MASK 20 // bit i => timer i has its alarm vibration enabled
#define PERSIST_KEY_SOUND_MASK 21     // bit i => timer i has its alarm sound enabled
#define PERSIST_KEY_TIMER_BASE 100   // timer i -> key 100+i (one Timer per key; 256B/key cap)
#define STORE_SCHEMA 6

// Loads timers into out (capacity MAX_TIMERS); returns count, or 0 if none/old schema.
int store_load(Timer *out);
// Persists `count` timers (one per key) + schema + count.
void store_save(const Timer *t, int count);
// Wakeup id (-1 when none).
int32_t store_load_wakeup_id(void);
void store_save_wakeup_id(int32_t id);
// Sort mode (defaults to SORT_MRU=0 when unset).
int store_load_sort(void);
void store_save_sort(int mode);
// Return to watchface after starting a timer (defaults to true when unset).
bool store_load_autoreturn_start(void);
void store_save_autoreturn_start(bool on);
// Return to watchface after stopping a timer (defaults to true when unset).
bool store_load_autoreturn_stop(void);
void store_save_autoreturn_stop(bool on);
// Counter for the next watch-assigned Timer.id (defaults to 1 when unset; combined
// with the high bit by main.c's next_watch_timer_id() to keep watch ids disjoint
// from phone-assigned ones).
uint16_t store_load_next_local_id(void);
void store_save_next_local_id(uint16_t v);
// Running-timers-first list ordering (defaults to true when unset).
bool store_load_runningfirst(void);
void store_save_runningfirst(bool on);
// Idle auto-exit timeout in seconds (defaults to 15 when unset; 0 = off).
int store_load_idleexit(void);
void store_save_idleexit(int seconds);
// Bitmask of "delete on finish" timers (bit i corresponds to row i in persisted timer list).
uint32_t store_load_delete_on_finish_mask(void);
void store_save_delete_on_finish_mask(uint32_t mask);
// Launch-sync starts setting (default OFF).
bool store_load_launchsync(void);
void store_save_launchsync(bool on);
// Default "delete on finish" for newly created timers (defaults to true/Delete when unset).
bool store_load_default_finish_delete(void);
void store_save_default_finish_delete(bool on);
// Run a newly created timer immediately (defaults to true when unset).
bool store_load_runoncreate(void);
void store_save_runoncreate(bool on);
// Show label keyboard after "+ New timer" dial confirm (defaults to true when unset).
bool store_load_keyboard_new_timer(void);
void store_save_keyboard_new_timer(bool on);
// Show label keyboard after main-view touch dial (defaults to false when unset).
bool store_load_keyboard_main_touch(void);
void store_save_keyboard_main_touch(bool on);
// Alarm vibration pattern: 0=Double, 1=Short, 2=Long (defaults to 0 when unset).
int store_load_vibe_pattern(void);
void store_save_vibe_pattern(int pattern);
// Alarm beep volume, 0-100, 0 = disabled (defaults to 0 when unset).
int store_load_audio_volume(void);
void store_save_audio_volume(int volume);
// Default "Vibration" for newly created timers (defaults to true when unset).
bool store_load_default_vibration_enabled(void);
void store_save_default_vibration_enabled(bool on);
// Default "Sound" for newly created timers (defaults to true when unset).
bool store_load_default_sound_enabled(void);
void store_save_default_sound_enabled(bool on);
// Bitmask of per-timer alarm vibration enabled (bit i corresponds to row i in persisted timer list).
uint32_t store_load_vibration_mask(void);
void store_save_vibration_mask(uint32_t mask);
// Bitmask of per-timer alarm sound enabled (bit i corresponds to row i in persisted timer list).
uint32_t store_load_sound_mask(void);
void store_save_sound_mask(uint32_t mask);
