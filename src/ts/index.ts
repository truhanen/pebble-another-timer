// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Tuomas Airaksinen

import Clay from 'pebble-clay';
import clayConfig from './config_clay';
import timerListComponent from './config_timer_list';
import { timerListToString } from './timer_config';
import { resendDict } from './config_sync';
import { appendCustomTimer } from './add_timer';
import { deleteTimer } from './delete_timer';
import { updateTimer } from './update_timer';

const clay = new Clay(clayConfig, null, { autoHandleEvents: false });
clay.registerComponent(timerListComponent);

// Inbound from the watch: either a custom timer to save (AddTimer), or the launch
// handshake (any other message) where we resend the last-saved config.
Pebble.addEventListener('appmessage', (e: any) => {
  const p = e && e.payload;
  if (p && typeof p.AddTimer === 'number') {
    const saved = appendCustomTimer(
      (k) => window.localStorage.getItem(k),
      (k, v) => window.localStorage.setItem(k, v),
      p.AddTimer,
      p.AddTimerName);
    console.log(saved ? 'AddTimer saved: ' + p.AddTimer + 's'
      : 'AddTimer rejected (invalid or full): ' + p.AddTimer);
    return;   // no echo — the watch already holds the running timer locally as custom
  }
  if (p && typeof p.DeleteTimer === 'number') {
    const left = deleteTimer(
      (k) => window.localStorage.getItem(k),
      (k, v) => window.localStorage.setItem(k, v),
      p.DeleteTimer);
    console.log(left === null ? 'DeleteTimer rejected (out of range): ' + p.DeleteTimer
      : 'DeleteTimer applied at index ' + p.DeleteTimer);
    return;   // no echo — the watch already removed it locally
  }
  if (p && typeof p.UpdateTimerIndex === 'number' && typeof p.UpdateTimerSeconds === 'number') {
    const saved = updateTimer(
      (k) => window.localStorage.getItem(k),
      (k, v) => window.localStorage.setItem(k, v),
      p.UpdateTimerIndex,
      p.UpdateTimerSeconds,
      p.UpdateTimerName);
    console.log(saved === null
      ? 'UpdateTimer rejected: idx=' + p.UpdateTimerIndex + ' secs=' + p.UpdateTimerSeconds
      : 'UpdateTimer applied at index ' + p.UpdateTimerIndex + ' -> ' + p.UpdateTimerSeconds + 's');
    return;   // no echo — the watch already applied it locally
  }
  const dict = resendDict((k) => window.localStorage.getItem(k));
  if (!dict) { return; }
  Pebble.sendAppMessage(dict, () => { console.log('config resent'); },
    () => { console.log('config resend failed'); });
});

Pebble.addEventListener('showConfiguration', () => {
  Pebble.sendAppMessage({ CfgOpen: 1 });   // pause the watch's idle auto-exit while the config page is open
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', (e: any) => {
  Pebble.sendAppMessage({ CfgOpen: 0 });   // config closed -> resume the watch's idle auto-exit (also on cancel)
  if (!e || !e.response) { console.log('No settings changed'); return; }
  const raw = clay.getSettings(e.response, false);
  const s: Record<string, any> = {};
  Object.keys(raw).forEach((k) => {
    const v = raw[k];
    s[k] = (v && typeof v === 'object' && 'value' in v) ? v.value : v;
  });
  const dict: Record<string, any> = {};
  dict.TimerConfig = timerListToString(s.TimerList);
  dict.SortOrder = parseInt(s.SortOrder, 10) || 0;
  dict.AutoReturn = s.AutoReturn ? 1 : 0;
  dict.RunningFirst = s.RunningFirst ? 1 : 0;
  dict.IdleExitSec = parseInt(s.IdleExitSec, 10) || 0;
  dict.LaunchSync = s.LaunchSync ? 1 : 0;
  // persist so we can re-send when the watchapp later launches and asks (above)
  window.localStorage.setItem('timer_config', dict.TimerConfig);
  window.localStorage.setItem('sort_order', String(dict.SortOrder));
  window.localStorage.setItem('auto_return', String(dict.AutoReturn));
  window.localStorage.setItem('running_first', String(dict.RunningFirst));
  window.localStorage.setItem('idle_exit', String(dict.IdleExitSec));
  window.localStorage.setItem('launch_sync', String(dict.LaunchSync));
  console.log('Sending TimerConfig: ' + JSON.stringify(dict.TimerConfig) + ' sort=' + dict.SortOrder + ' autoReturn=' + dict.AutoReturn + ' runningFirst=' + dict.RunningFirst + ' idleExit=' + dict.IdleExitSec + ' launchSync=' + dict.LaunchSync);
  Pebble.sendAppMessage(dict, () => { console.log('config sent'); },
    () => { console.log('config send failed'); });
});
