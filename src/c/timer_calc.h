// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Tuomas Airaksinen
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MAX_TIMERS 16    // matches MAX_TIMERS in src/ts/timer_config.ts
#define NAME_LEN   31    // matches NAME_MAX in src/ts/timer_config.ts

typedef enum { TS_IDLE = 0, TS_RUNNING = 1, TS_PAUSED = 2 } TimerState;

// List sort modes (matches SortOrder values in src/ts/config_clay.ts).
typedef enum { SORT_MRU = 0, SORT_SHORTEST = 1, SORT_LONGEST = 2 } SortMode;

// Detail-window actions, in no particular order; tc_detail_actions() returns them
// in display order per timer state.
typedef enum {
  DACT_STOP,        // running/paused -> reset to IDLE
  DACT_PAUSE,       // running -> pause
  DACT_START,       // idle/done/paused -> start or resume in place
  DACT_PLUS,        // +1 min
  DACT_MINUS,       // -1 min
  DACT_DELETE,      // delete this timer (after a confirm screen)
} DetailAction;

typedef struct {
  char name[NAME_LEN + 1];
  int32_t duration;     // configured length, seconds (>=1)
  TimerState state;
  int64_t end_time;     // epoch secs; valid when RUNNING (may be in the past: "overtime")
  int32_t remaining;    // secs left; valid when IDLE/PAUSED
  int64_t last_used;    // epoch secs of last user action; drives list order
  bool custom;          // true = created on the watch; preserved across config reconcile until absorbed
  bool alarm_pending;   // true = queued, still owed a fullscreen alarm screen. Set by
                        // tc_check_expiry(); cleared by tc_start/tc_extend/tc_reset (episode
                        // ends) OR by an explicit "Run overtime" dismissal (still overtime,
                        // just no longer queued for a screen).
  bool alarm_notified;  // true once tc_check_expiry() has fired for the CURRENT overtime
                        // episode - the one-shot guard. Unlike alarm_pending, this is NOT
                        // cleared by "Run overtime": end_time stays <= now forever once a
                        // timer is left running in overtime, so without this separate,
                        // longer-lived flag tc_check_expiry would refire on every sweep.
                        // Cleared only by tc_start/tc_extend/tc_reset.
} Timer;

// Parse the TimerConfig string into config-only timers (state IDLE, remaining=
// duration, end_time=0, last_used=0). Returns count (<= MAX_TIMERS).
int tc_parse_config(const char *buf, Timer *out, int max);

// "M:SS" when < 1h, else "H:MM:SS". Writes into buf (size n).
void tc_format_remaining(char *buf, size_t n, int32_t secs);

// Fixed-width "HH:MM:SS" (leading zeros, always all fields) so a column of times
// aligns and is easy to compare at a glance. Writes into buf (size n).
void tc_format_fixed(char *buf, size_t n, int32_t secs);

// Seconds left to show for a timer at time `now`. Negative when RUNNING and
// past end_time (overtime) - not clamped to 0, so display code can show the
// timer counting up past zero instead of sitting at "0:00".
int32_t tc_remaining_now(const Timer *t, int64_t now);

// Soonest FUTURE end_time among RUNNING timers (excludes overtime timers -
// end_time <= now - since there's nothing upcoming to schedule for those).
// Returns true + *out set, or false.
bool tc_soonest_end(const Timer *t, int count, int64_t now, int64_t *out);

// Fill order[count] with timer indices sorted per `mode` (SORT_MRU: last_used
// desc; SORT_SHORTEST/LONGEST: remaining-at-`now` asc/desc), ties by index asc.
// When running_first is true, timers are grouped as RUNNING first, then PAUSED,
// then all other states, preserving each group's intra-order.
void tc_display_order(const Timer *t, int count, SortMode mode, int64_t now, int *order, bool running_first);

// State transitions (each stamps last_used = now where it represents a user action).
// tc_start: non-running timers start from t->remaining (fallback t->duration when <1s); RUNNING restarts from duration.
void tc_start(Timer *t, int64_t now);
void tc_pause(Timer *t, int64_t now);
void tc_reset(Timer *t, int64_t now);

// Run the timer for `secs` more from `now` (e.g. "+1 min" on the alarm), regardless
// of its current state. Sets RUNNING, end_time = now + secs, stamps last_used.
void tc_extend(Timer *t, int32_t secs, int64_t now);

// Add `secs` to the time left. RUNNING (including overtime): end_time += secs
// (extends/reduces the live countdown). PAUSED: remaining += secs (clamped >=
// 0). IDLE: no-op. Stamps last_used = now. Distinct from tc_extend, which SETS
// end_time and forces RUNNING.
void tc_add(Timer *t, int32_t secs, int64_t now);

// True while RUNNING with end_time <= now (i.e. past due but not yet stopped).
bool tc_is_overtime(const Timer *t, int64_t now);

// If RUNNING, end_time <= now, and not already alarm_notified: set alarm_notified
// and alarm_pending, return true (state stays RUNNING - the timer is now "in
// overtime"). Else false. This is a one-shot "just crossed zero" detector, not a
// state change; alarm_notified (not alarm_pending) is the re-fire guard, since a
// perpetually-overtime timer keeps end_time <= now forever.
bool tc_check_expiry(Timer *t, int64_t now);

// Merge a freshly parsed config (cfg/cfgN) over current runtime state (cur/curN)
// by list position into out (size MAX_TIMERS); returns new count. Unchanged rows
// keep their state; duration-changed rows keep state with remaining re-derived
// for non-RUNNING; new rows start IDLE; dropped rows disappear. Custom (watch-created)
// rows beyond cfgN are appended after the config rows and preserved until a later config
// absorbs them by position.
int tc_reconcile(const Timer *cur, int curN, const Timer *cfg, int cfgN, Timer *out);

// Fill `out` (capacity >= 7) with the ordered detail-window actions for a timer in
// state `st` (`overtime` true when RUNNING with end_time <= now, i.e. tc_is_overtime).
// Returns the number written.
int tc_detail_actions(TimerState st, bool overtime, DetailAction *out);
