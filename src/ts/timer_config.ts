// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Tuomas Airaksinen

// Serialization contract shared with the watch C side (timer_calc.c). Records
// separated by RS (0x1e), fields (name, seconds) by US (0x1f). Keep in sync.
export const MAX_TIMERS = 16;      // matches MAX_TIMERS in src/c/timer_calc.h
export const NAME_MAX = 31;        // matches NAME_LEN in src/c/timer_calc.h
const RS = '\x1e';
const US = '\x1f';

export interface TimerEntry { name: string; seconds: number; id: number; }

// Persistent identity generator, disjoint from watch-assigned ids (main.c uses
// the high bit set: 0x80000000 | counter). 0 means "not yet assigned".
export function genTimerId(): number {
  return 1 + Math.floor(Math.random() * 0x7ffffffe);
}

export function hmsToSeconds(h: any, m: any, s: any): number {
  const hh = Math.max(0, parseInt(h, 10) || 0);
  const mm = Math.max(0, parseInt(m, 10) || 0);
  const ss = Math.max(0, parseInt(s, 10) || 0);
  return hh * 3600 + mm * 60 + ss;
}

export function secondsToHms(sec: any): { h: number; m: number; s: number } {
  let n = Math.max(0, parseInt(sec, 10) || 0);
  const h = Math.floor(n / 3600); n -= h * 3600;
  const m = Math.floor(n / 60); n -= m * 60;
  return { h: h, m: m, s: n };
}

export function sanitizeName(name: any): string {
  if (typeof name !== 'string') { return ''; }
  let out = name.replace(/[\x1e\x1f]/g, '').replace(/^\s+|\s+$/g, '');
  if (out.length > NAME_MAX) { out = out.substring(0, NAME_MAX); }
  return out;
}

export function timerListToString(list: any): string {
  const arr: any[] = Array.isArray(list) ? list : [];
  const recs: string[] = [];
  for (let i = 0; i < arr.length && recs.length < MAX_TIMERS; i++) {
    const e = arr[i] || {};
    const seconds = parseInt(e.seconds, 10);
    if (isNaN(seconds) || seconds < 1) { continue; }
    const id = parseInt(e.id, 10) || 0;
    recs.push(sanitizeName(e.name) + US + seconds + US + id);
  }
  return recs.join(RS);
}

export function stringToTimerList(str: any): TimerEntry[] {
  const out: TimerEntry[] = [];
  if (typeof str !== 'string' || str === '') { return out; }
  const recs = str.split(RS);
  for (let i = 0; i < recs.length && out.length < MAX_TIMERS; i++) {
    const fields = recs[i].split(US);
    const seconds = parseInt(fields[1], 10);
    if (isNaN(seconds) || seconds < 1) { continue; }
    // Legacy 2-field records (no id yet) parse as id 0; callers backfill a fresh one.
    const id = fields.length > 2 ? (parseInt(fields[2], 10) || 0) : 0;
    out.push({ name: sanitizeName(fields[0]), seconds: seconds, id: id });
  }
  return out;
}
