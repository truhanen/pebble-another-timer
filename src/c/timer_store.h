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
#define PERSIST_KEY_TIMER_BASE 100   // timer i -> key 100+i (one Timer per key; 256B/key cap)
#define STORE_SCHEMA 4

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
