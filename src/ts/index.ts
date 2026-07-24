// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Tuomas Airaksinen

import Clay from 'pebble-clay';
import clayConfig from './config_clay';
import timerListComponent from './config_timer_list';
import pinSaveButtonToBottom from './config_pin_save';
import { timerListToString } from './timer_config';
import { resendDict } from './config_sync';
import { appendCustomTimer } from './add_timer';
import { deleteTimer } from './delete_timer';
import { updateTimer } from './update_timer';

const clay = new Clay(clayConfig, pinSaveButtonToBottom, { autoHandleEvents: false });
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
      p.AddTimerName,
      p.AddTimerId);
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
  // checkboxgroup .get() returns an array of booleans for the Clay-only
  // 'AutoReturnOptions' key (see config_clay.ts); decompose into the two real
  // scalar AppMessage keys instead of sending the array itself.
  const autoReturnOptions: boolean[] = Array.isArray(s.AutoReturnOptions) ? s.AutoReturnOptions : [true, true];
  dict.AutoReturnStart = autoReturnOptions[0] ? 1 : 0;
  dict.AutoReturnStop = autoReturnOptions[1] ? 1 : 0;
  dict.RunningFirst = s.RunningFirst ? 1 : 0;
  dict.IdleExitSec = parseInt(s.IdleExitSec, 10) || 0;
  dict.LaunchSync = s.LaunchSync ? 1 : 0;
  dict.DefaultFinishAction = parseInt(s.DefaultFinishAction, 10) || 0;
  dict.RunOnCreate = s.RunOnCreate ? 1 : 0;
  // checkboxgroup .get() returns an array of booleans for the Clay-only
  // 'KeyboardOptions' key (see config_clay.ts); decompose into the two real
  // scalar AppMessage keys instead of sending the array itself.
  const keyboardOptions: boolean[] = Array.isArray(s.KeyboardOptions) ? s.KeyboardOptions : [true, false];
  dict.KeyboardOnNewTimer = keyboardOptions[0] ? 1 : 0;
  dict.KeyboardOnMainTouch = keyboardOptions[1] ? 1 : 0;
  // select values MUST be strings (Clay); parseInt on save.
  dict.VibePattern = parseInt(s.VibePattern, 10) || 0;
  dict.AudioVolume = parseInt(s.AudioVolume, 10) || 0;
  // checkboxgroup .get() returns an array of booleans for the Clay-only
  // 'NewTimerSoundOptions' key (see config_clay.ts); decompose into the two real
  // scalar AppMessage keys instead of sending the array itself.
  const newTimerSoundOptions: boolean[] = Array.isArray(s.NewTimerSoundOptions) ? s.NewTimerSoundOptions : [true, true];
  dict.DefaultVibrationEnabled = newTimerSoundOptions[0] ? 1 : 0;
  dict.DefaultSoundEnabled = newTimerSoundOptions[1] ? 1 : 0;
  // persist so we can re-send when the watchapp later launches and asks (above)
  window.localStorage.setItem('timer_config', dict.TimerConfig);
  window.localStorage.setItem('sort_order', String(dict.SortOrder));
  window.localStorage.setItem('auto_return_start', String(dict.AutoReturnStart));
  window.localStorage.setItem('auto_return_stop', String(dict.AutoReturnStop));
  window.localStorage.setItem('running_first', String(dict.RunningFirst));
  window.localStorage.setItem('idle_exit', String(dict.IdleExitSec));
  window.localStorage.setItem('launch_sync', String(dict.LaunchSync));
  window.localStorage.setItem('default_finish_action', String(dict.DefaultFinishAction));
  window.localStorage.setItem('run_on_create', String(dict.RunOnCreate));
  window.localStorage.setItem('keyboard_on_new_timer', String(dict.KeyboardOnNewTimer));
  window.localStorage.setItem('keyboard_on_main_touch', String(dict.KeyboardOnMainTouch));
  window.localStorage.setItem('vibe_pattern', String(dict.VibePattern));
  window.localStorage.setItem('audio_volume', String(dict.AudioVolume));
  window.localStorage.setItem('default_vibration_enabled', String(dict.DefaultVibrationEnabled));
  window.localStorage.setItem('default_sound_enabled', String(dict.DefaultSoundEnabled));
  console.log('Sending TimerConfig: ' + JSON.stringify(dict.TimerConfig) + ' sort=' + dict.SortOrder + ' autoReturnStart=' + dict.AutoReturnStart + ' autoReturnStop=' + dict.AutoReturnStop + ' runningFirst=' + dict.RunningFirst + ' idleExit=' + dict.IdleExitSec + ' launchSync=' + dict.LaunchSync + ' defaultFinishAction=' + dict.DefaultFinishAction + ' runOnCreate=' + dict.RunOnCreate + ' keyboardOnNewTimer=' + dict.KeyboardOnNewTimer + ' keyboardOnMainTouch=' + dict.KeyboardOnMainTouch + ' vibePattern=' + dict.VibePattern + ' audioVolume=' + dict.AudioVolume + ' defaultVibrationEnabled=' + dict.DefaultVibrationEnabled + ' defaultSoundEnabled=' + dict.DefaultSoundEnabled);
  Pebble.sendAppMessage(dict, () => { console.log('config sent'); },
    () => { console.log('config send failed'); });
});
