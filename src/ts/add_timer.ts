// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Tuomas Airaksinen

// Append an unnamed, watch-created timer to the phone's persisted config and to
// the Clay config-page store, so it survives and shows up on the config page.
// `get`/`set` read/write localStorage by key. Returns the new TimerConfig string,
// or null when `seconds` is invalid or the list is already at MAX_TIMERS.
import { stringToTimerList, timerListToString, MAX_TIMERS, sanitizeName } from './timer_config';

export function appendCustomTimer(
  get: (k: string) => string | null,
  set: (k: string, v: string) => void,
  seconds: any,
  name?: any
): string | null {
  const secs = Math.floor(Number(seconds));
  if (!(secs >= 1)) { return null; }
  const list = stringToTimerList(get('timer_config') || '');
  if (list.length >= MAX_TIMERS) { return null; }
  list.push({ name: sanitizeName(name), seconds: secs });
  const str = timerListToString(list);
  set('timer_config', str);
  // Keep the Clay config page in sync (clay-settings holds last-saved values that
  // generateUrl bakes into the page); otherwise a later Save would drop this timer.
  let cs: any = {};
  try { cs = JSON.parse(get('clay-settings') || '{}') || {}; } catch (e) { cs = {}; }
  cs.TimerList = list;
  set('clay-settings', JSON.stringify(cs));
  return str;
}
