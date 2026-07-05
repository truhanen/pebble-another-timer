// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Tuomas Airaksinen
#include <pebble.h>
#include "timer_store.h"
#include <string.h>

// One Timer per persist key (PERSIST_KEY_TIMER_BASE+i). A packed blob can't be
// used: persist_write_data caps at 256 B/key and 16 Timers (~1 KB) exceed that.
int store_load(Timer *out) {
  if (!persist_exists(PERSIST_KEY_SCHEMA) ||
      persist_read_int(PERSIST_KEY_SCHEMA) != STORE_SCHEMA) { return 0; }
  int count = persist_exists(PERSIST_KEY_COUNT) ? persist_read_int(PERSIST_KEY_COUNT) : 0;
  if (count > MAX_TIMERS) { count = MAX_TIMERS; }
  if (count < 0) { count = 0; }
  for (int i = 0; i < count; i++) {
    if (persist_exists(PERSIST_KEY_TIMER_BASE + i)) {
      persist_read_data(PERSIST_KEY_TIMER_BASE + i, &out[i], sizeof(Timer));
    } else {
      memset(&out[i], 0, sizeof(Timer));
    }
  }
  return count;
}

void store_save(const Timer *t, int count) {
  if (count > MAX_TIMERS) { count = MAX_TIMERS; }
  persist_write_int(PERSIST_KEY_SCHEMA, STORE_SCHEMA);
  persist_write_int(PERSIST_KEY_COUNT, count);
  for (int i = 0; i < count; i++) {
    persist_write_data(PERSIST_KEY_TIMER_BASE + i, &t[i], sizeof(Timer));
  }
  // drop any stale keys beyond the new count
  for (int i = count; i < MAX_TIMERS; i++) {
    if (persist_exists(PERSIST_KEY_TIMER_BASE + i)) { persist_delete(PERSIST_KEY_TIMER_BASE + i); }
  }
}

int32_t store_load_wakeup_id(void) {
  if (!persist_exists(PERSIST_KEY_WAKEUPID)) { return -1; }
  return persist_read_int(PERSIST_KEY_WAKEUPID);
}

void store_save_wakeup_id(int32_t id) {
  persist_write_int(PERSIST_KEY_WAKEUPID, id);
}

int store_load_sort(void) {
  if (!persist_exists(PERSIST_KEY_SORTORDER)) { return SORT_MRU; }
  return persist_read_int(PERSIST_KEY_SORTORDER);
}

void store_save_sort(int mode) {
  persist_write_int(PERSIST_KEY_SORTORDER, mode);
}

bool store_load_autoreturn(void) {
  if (!persist_exists(PERSIST_KEY_AUTORETURN)) { return true; }   // default ON
  return persist_read_bool(PERSIST_KEY_AUTORETURN);
}

void store_save_autoreturn(bool on) {
  persist_write_bool(PERSIST_KEY_AUTORETURN, on);
}

bool store_load_runningfirst(void) {
  if (!persist_exists(PERSIST_KEY_RUNNINGFIRST)) { return true; }   // default ON
  return persist_read_bool(PERSIST_KEY_RUNNINGFIRST);
}

void store_save_runningfirst(bool on) {
  persist_write_bool(PERSIST_KEY_RUNNINGFIRST, on);
}

int store_load_idleexit(void) {
  if (!persist_exists(PERSIST_KEY_IDLEEXIT)) { return 15; }   // default 15s ON
  return persist_read_int(PERSIST_KEY_IDLEEXIT);
}

void store_save_idleexit(int seconds) {
  persist_write_int(PERSIST_KEY_IDLEEXIT, seconds);
}

uint32_t store_load_ephemeral_mask(void) {
  if (!persist_exists(PERSIST_KEY_EPHEMERAL)) { return 0; }
  return (uint32_t)persist_read_int(PERSIST_KEY_EPHEMERAL);
}

void store_save_ephemeral_mask(uint32_t mask) {
  persist_write_int(PERSIST_KEY_EPHEMERAL, (int32_t)mask);
}

bool store_load_launchsync(void) {
  if (!persist_exists(PERSIST_KEY_LAUNCHSYNC)) { return false; }   // default OFF
  return persist_read_bool(PERSIST_KEY_LAUNCHSYNC);
}

void store_save_launchsync(bool on) {
  persist_write_bool(PERSIST_KEY_LAUNCHSYNC, on);
}
