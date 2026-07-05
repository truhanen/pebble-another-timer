// SPDX-License-Identifier: GPL-3.0-only

// Overwrite a timer duration at list index `index` with `seconds` in the phone's
// persisted config and Clay store. Keeps the timer name unchanged.
import { stringToTimerList, timerListToString } from './timer_config';

export function updateTimer(
  get: (k: string) => string | null,
  set: (k: string, v: string) => void,
  index: any,
  seconds: any
): string | null {
  const i = Math.floor(Number(index));
  const secs = Math.floor(Number(seconds));
  if (!(secs >= 1)) { return null; }
  const list = stringToTimerList(get('timer_config') || '');
  if (!(i >= 0 && i < list.length)) { return null; }
  list[i].seconds = secs;
  const str = timerListToString(list);
  set('timer_config', str);
  let cs: any = {};
  try { cs = JSON.parse(get('clay-settings') || '{}') || {}; } catch (e) { cs = {}; }
  cs.TimerList = list;
  set('clay-settings', JSON.stringify(cs));
  return str;
}
