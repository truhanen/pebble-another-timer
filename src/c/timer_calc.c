// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Tuomas Airaksinen
#include "timer_calc.h"
#include <string.h>
#include <stdio.h>

static void copy_name(char *dst, const char *src, size_t srclen) {
  size_t n = srclen < NAME_LEN ? srclen : NAME_LEN;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

// Parse a non-negative decimal prefix, stopping at the first non-digit (a
// separator or NUL). Hand-rolled instead of strtol: the Core Devices Pebble
// firmware does not export strtol, so calling it jumps to an invalid
// syscall-table slot and hard-faults on-watch (PC in the low syscall region) —
// even though it links and works on the host. snprintf etc. are fine; strtol is not.
static int32_t parse_uint(const char *s) {
  int32_t v = 0;
  while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
  return v;
}

int tc_parse_config(const char *buf, Timer *out, int max) {
  int count = 0;
  if (!buf || buf[0] == '\0') { return 0; }
  const char *p = buf;
  while (*p && count < max) {
    const char *rec_end = strchr(p, '\x1e');
    const char *limit = rec_end ? rec_end : (p + strlen(p));
    const char *us = (const char *)memchr(p, '\x1f', (size_t)(limit - p));
    if (us) {
      int32_t seconds = parse_uint(us + 1);
      if (seconds >= 1) {
        // Optional third field (id), added after name/seconds: name US seconds US id.
        // Legacy 2-field records (no second US) parse as id 0 - "not yet assigned".
        const char *us2 = (const char *)memchr(us + 1, '\x1f', (size_t)(limit - (us + 1)));
        uint32_t id = us2 ? (uint32_t)parse_uint(us2 + 1) : 0;
        Timer *t = &out[count];
        memset(t, 0, sizeof(*t));
        copy_name(t->name, p, (size_t)(us - p));
        t->duration = seconds;
        t->state = TS_IDLE;
        t->remaining = seconds;
        t->id = id;
        count++;
      }
    }
    if (!rec_end) { break; }
    p = rec_end + 1;
  }
  return count;
}

// Negative `secs` (a timer in overtime) renders with a leading "-" and its
// magnitude, so overtime counts up from "-0:00" instead of sitting at zero.
void tc_format_remaining(char *buf, size_t n, int32_t secs) {
  const char *sign = "";
  if (secs < 0) { sign = "-"; secs = -secs; }
  int h = secs / 3600;
  int m = (secs % 3600) / 60;
  int s = secs % 60;
  if (h > 0) { snprintf(buf, n, "%s%d:%02d:%02d", sign, h, m, s); }
  else { snprintf(buf, n, "%s%d:%02d", sign, m, s); }
}

void tc_format_fixed(char *buf, size_t n, int32_t secs) {
  const char *sign = "";
  if (secs < 0) { sign = "-"; secs = -secs; }
  int h = secs / 3600;
  int m = (secs % 3600) / 60;
  int s = secs % 60;
  snprintf(buf, n, "%s%02d:%02d:%02d", sign, h, m, s);
}

int32_t tc_remaining_now(const Timer *t, int64_t now) {
  if (t->state == TS_RUNNING) {
    // May be negative when in overtime (end_time already passed) - callers
    // that want a floor of 0 (e.g. a restart-from-duration fallback) already
    // guard for that themselves.
    return (int32_t)(t->end_time - now);
  }
  return t->remaining;
}

bool tc_soonest_end(const Timer *t, int count, int64_t now, int64_t *out) {
  bool found = false;
  int64_t best = 0;
  for (int i = 0; i < count; i++) {
    // Skip timers already in overtime: there's no future moment to wake up
    // for - end_time is in the past and will stay there until the user
    // explicitly stops/restarts it. Including it here would make it the
    // "soonest" forever, re-arming a ~1s-out wakeup on every check and
    // re-launching the app in a tight loop for as long as it sits unhandled.
    if (t[i].state == TS_RUNNING && t[i].end_time > now) {
      if (!found || t[i].end_time < best) { best = t[i].end_time; found = true; }
    }
  }
  if (found && out) { *out = best; }
  return found;
}

// Comparison key for a timer under `mode`. Returns a 64-bit value; the sort puts
// HIGHER keys first, so we negate where the mode wants ascending order. When
// running_first is set, timers are tiered as RUNNING first, then PAUSED, then
// all other states, while preserving each tier's intra-order from `mode`.
static int64_t order_key(const Timer *t, SortMode mode, int64_t now, bool running_first) {
  int64_t base;
  if (mode == SORT_SHORTEST) { base = -(int64_t)tc_remaining_now(t, now); } // asc -> negate
  else if (mode == SORT_LONGEST)  { base =  (int64_t)tc_remaining_now(t, now); } // desc
  else { base = t->last_used; }                                              // MRU: desc
  if (running_first) {
    if (t->state == TS_RUNNING) { base += (2LL << 40); }
    else if (t->state == TS_PAUSED) { base += (1LL << 40); }
  }
  return base;
}

void tc_display_order(const Timer *t, int count, SortMode mode, int64_t now, int *order, bool running_first) {
  for (int i = 0; i < count; i++) { order[i] = i; }
  // stable insertion sort: higher order_key first, ties keep ascending index
  for (int i = 1; i < count; i++) {
    int key = order[i];
    int64_t kv = order_key(&t[key], mode, now, running_first);
    int j = i - 1;
    while (j >= 0 && order_key(&t[order[j]], mode, now, running_first) < kv) {
      order[j + 1] = order[j];
      j--;
    }
    order[j + 1] = key;
  }
}

void tc_start(Timer *t, int64_t now) {
  // A RUNNING timer (restarting from overtime) always starts from the full
  // duration. A PAUSED timer resumes from `remaining`, which may be negative
  // if it was paused mid-overtime - resuming then re-enters overtime at the
  // same offset, exactly as if it had kept running. An IDLE timer starts
  // from `remaining` too (tuned with +/- before starting), falling back to
  // the full duration when remaining is unset/zero.
  int32_t rem;
  if (t->state == TS_RUNNING) {
    rem = t->duration;
  } else if (t->state == TS_PAUSED) {
    rem = t->remaining;
  } else {
    rem = t->remaining;
    if (rem < 1) { rem = t->duration; }
  }
  t->end_time = now + rem;
  t->state = TS_RUNNING;
  t->last_used = now;
  t->alarm_pending = false;
  t->alarm_notified = false;
}

void tc_pause(Timer *t, int64_t now) {
  if (t->state == TS_RUNNING) {
    // May be negative if paused mid-overtime - preserved as-is so resuming
    // continues from the same overtime offset instead of snapping to 0.
    t->remaining = (int32_t)(t->end_time - now);
  }
  t->state = TS_PAUSED;
  t->last_used = now;
}

void tc_reset(Timer *t, int64_t now) {
  t->state = TS_IDLE;
  t->remaining = t->duration;
  t->end_time = 0;
  t->last_used = now;
  t->alarm_pending = false;
  t->alarm_notified = false;
}

void tc_extend(Timer *t, int32_t secs, int64_t now) {
  t->state = TS_RUNNING;
  t->end_time = now + secs;
  t->last_used = now;
  t->alarm_pending = false;
  t->alarm_notified = false;
}

void tc_add(Timer *t, int32_t secs, int64_t now) {
  if (t->state == TS_RUNNING) {
    bool was_overtime = t->end_time <= now;
    t->end_time += secs;
    // Leaving overtime clears the one-shot alarm guard, so a later crossing
    // back through zero (this timer counting down again) alarms afresh.
    if (was_overtime && t->end_time > now) {
      t->alarm_pending = false;
      t->alarm_notified = false;
    }
  } else if (t->state == TS_PAUSED) {
    // No floor, matching the RUNNING case above: a paused-in-overtime timer's
    // `remaining` is already negative, and -/+ should adjust it the same way
    // a live overtime timer's end_time moves.
    bool was_overtime = t->remaining <= 0;
    t->remaining += secs;
    // Same re-arm as the RUNNING branch: a later resume that counts down
    // into overtime again should alarm, not be silently swallowed by a
    // guard left over from a previous overtime episode.
    if (was_overtime && t->remaining > 0) {
      t->alarm_pending = false;
      t->alarm_notified = false;
    }
  } else {
    return;   // IDLE: not applicable
  }
  t->last_used = now;
}

bool tc_is_overtime(const Timer *t, int64_t now) {
  return t->state == TS_RUNNING && t->end_time <= now;
}

bool tc_check_expiry(Timer *t, int64_t now) {
  if (t->state == TS_RUNNING && t->end_time <= now && !t->alarm_notified) {
    t->alarm_notified = true;
    t->alarm_pending = true;
    return true;
  }
  return false;
}

int tc_reconcile(const Timer *cur, int curN, const Timer *cfg, int cfgN, Timer *out, int *src_index) {
  int n = cfgN > MAX_TIMERS ? MAX_TIMERS : cfgN;
  bool consumed[MAX_TIMERS] = { false };
  for (int i = 0; i < n; i++) {
    Timer t = cfg[i];   // start from config (IDLE, remaining=duration)
    int match = -1;
    if (cfg[i].id != 0) {
      for (int j = 0; j < curN; j++) {
        if (!consumed[j] && cur[j].id == cfg[i].id) { match = j; break; }
      }
    }
    if (match >= 0 && strcmp(cur[match].name, cfg[i].name) == 0 && cur[match].duration == cfg[i].duration) {
      // unchanged: keep all runtime state
      t = cur[match];
    } else if (match >= 0) {
      // same id, changed name/duration: keep state but re-derive timing
      t.state = cur[match].state;
      t.last_used = cur[match].last_used;
      if (cur[match].state == TS_RUNNING) {
        t.end_time = cur[match].end_time;
      } else if (cur[match].state == TS_PAUSED) {
        t.remaining = cur[match].remaining > t.duration ? t.duration : cur[match].remaining;
      } else {
        t.state = TS_IDLE;
        t.remaining = t.duration;
      }
    }
    if (match >= 0) { consumed[match] = true; }
    t.custom = false;   // a config-backed row is no longer watch-local (absorbs custom rows)
    out[i] = t;
    src_index[i] = match;
  }
  // Preserve watch-created (custom) rows with no matching id in cfg, so an on-watch
  // "Save as new" timer is not dropped before the phone config absorbs it.
  // Non-custom unmatched rows are dropped as before.
  for (int j = 0; j < curN && n < MAX_TIMERS; j++) {
    if (!consumed[j] && cur[j].custom) {
      out[n] = cur[j];
      src_index[n] = j;
      n++;
    }
  }
  return n;
}

int tc_detail_actions(TimerState st, DetailAction *out) {
  int n = 0;
  if (st == TS_RUNNING) {
    out[n++] = DACT_STOP;
    out[n++] = DACT_PAUSE;
  } else if (st == TS_PAUSED) {
    out[n++] = DACT_STOP;
    out[n++] = DACT_START;   // resume
  } else {                   // TS_IDLE
    out[n++] = DACT_START;
  }
  out[n++] = DACT_PLUS;
  out[n++] = DACT_MINUS;
  return n;
}
