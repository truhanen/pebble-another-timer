// SPDX-License-Identifier: GPL-3.0-only
const test = require('node:test');
const assert = require('node:assert');
const { resendDict } = require('../src/pkjs/config_sync');

function store(obj) { return (k) => (k in obj ? obj[k] : null); }

test('resendDict: never saved (timer_config absent) -> null (do not clobber watch)', () => {
  assert.strictEqual(resendDict(store({})), null);
  assert.strictEqual(resendDict(store({ sort_order: '2' })), null);
});

test('resendDict: saved config -> dict with parsed SortOrder + AutoReturn + RunningFirst + IdleExitSec + LaunchSync + DefaultFinishAction + RunOnCreate + KeyboardOnNewTimer + KeyboardOnMainTouch', () => {
  assert.deepStrictEqual(
    resendDict(store({ timer_config: 'Egg\x1f300\x1eTea\x1f120', sort_order: '1', auto_return: '1', running_first: '1', idle_exit: '30', launch_sync: '1', default_finish_action: '0', run_on_create: '0', keyboard_on_new_timer: '0', keyboard_on_main_touch: '1' })),
    { TimerConfig: 'Egg\x1f300\x1eTea\x1f120', SortOrder: 1, AutoReturn: 1, RunningFirst: 1, IdleExitSec: 30, LaunchSync: 1, DefaultFinishAction: 0, RunOnCreate: 0, KeyboardOnNewTimer: 0, KeyboardOnMainTouch: 1 });
});

test('resendDict: explicitly-saved empty list ("") IS sent (user cleared all timers)', () => {
  assert.deepStrictEqual(
    resendDict(store({ timer_config: '', sort_order: '0' })),
    { TimerConfig: '', SortOrder: 0, AutoReturn: 0, RunningFirst: 1, IdleExitSec: 15, LaunchSync: 0, DefaultFinishAction: 1, RunOnCreate: 1, KeyboardOnNewTimer: 1, KeyboardOnMainTouch: 0 });
});

test('resendDict: missing default_finish_action defaults to 1 (Delete) for pre-feature saves', () => {
  assert.strictEqual(resendDict(store({ timer_config: 'a\x1f60' })).DefaultFinishAction, 1);
});

test('resendDict: saved default_finish_action "0" round-trips to 0', () => {
  assert.strictEqual(resendDict(store({ timer_config: 'a\x1f60', default_finish_action: '0' })).DefaultFinishAction, 0);
});

test('resendDict: missing/garbage sort_order defaults to 0', () => {
  assert.strictEqual(resendDict(store({ timer_config: 'a\x1f60' })).SortOrder, 0);
  assert.strictEqual(resendDict(store({ timer_config: 'a\x1f60', sort_order: 'x' })).SortOrder, 0);
});

test('resendDict: missing auto_return defaults to 0', () => {
  assert.strictEqual(resendDict(store({ timer_config: 'a\x1f60' })).AutoReturn, 0);
});

test('resendDict: saved auto_return "0" round-trips to 0', () => {
  assert.strictEqual(resendDict(store({ timer_config: 'a\x1f60', auto_return: '0' })).AutoReturn, 0);
});

test('resendDict: missing running_first defaults to 1 (ON) for pre-feature saves', () => {
  assert.strictEqual(resendDict(store({ timer_config: 'a\x1f60' })).RunningFirst, 1);
});

test('resendDict: saved running_first "0" round-trips to 0', () => {
  assert.strictEqual(resendDict(store({ timer_config: 'a\x1f60', running_first: '0' })).RunningFirst, 0);
});

test('resendDict: missing idle_exit defaults to 15 (ON) for pre-feature saves', () => {
  assert.strictEqual(resendDict(store({ timer_config: 'a\x1f60' })).IdleExitSec, 15);
});

test('resendDict: saved idle_exit "0" (Off) round-trips to 0', () => {
  assert.strictEqual(resendDict(store({ timer_config: 'a\x1f60', idle_exit: '0' })).IdleExitSec, 0);
});

test('resendDict: saved idle_exit "60" round-trips to 60', () => {
  assert.strictEqual(resendDict(store({ timer_config: 'a\x1f60', idle_exit: '60' })).IdleExitSec, 60);
});

test('resendDict: missing launch_sync defaults to 0 (OFF)', () => {
  assert.strictEqual(resendDict(store({ timer_config: 'a\x1f60' })).LaunchSync, 0);
});

test('resendDict: saved launch_sync "1" round-trips to 1', () => {
  assert.strictEqual(resendDict(store({ timer_config: 'a\x1f60', launch_sync: '1' })).LaunchSync, 1);
});

test('resendDict: missing run_on_create defaults to 1 (ON) for pre-feature saves', () => {
  assert.strictEqual(resendDict(store({ timer_config: 'a\x1f60' })).RunOnCreate, 1);
});

test('resendDict: saved run_on_create "0" round-trips to 0', () => {
  assert.strictEqual(resendDict(store({ timer_config: 'a\x1f60', run_on_create: '0' })).RunOnCreate, 0);
});

test('resendDict: missing keyboard_on_new_timer defaults to 1 (ON) for pre-feature saves', () => {
  assert.strictEqual(resendDict(store({ timer_config: 'a\x1f60' })).KeyboardOnNewTimer, 1);
});

test('resendDict: saved keyboard_on_new_timer "0" round-trips to 0', () => {
  assert.strictEqual(resendDict(store({ timer_config: 'a\x1f60', keyboard_on_new_timer: '0' })).KeyboardOnNewTimer, 0);
});

test('resendDict: missing keyboard_on_main_touch defaults to 0 (OFF) for pre-feature saves', () => {
  assert.strictEqual(resendDict(store({ timer_config: 'a\x1f60' })).KeyboardOnMainTouch, 0);
});

test('resendDict: saved keyboard_on_main_touch "1" round-trips to 1', () => {
  assert.strictEqual(resendDict(store({ timer_config: 'a\x1f60', keyboard_on_main_touch: '1' })).KeyboardOnMainTouch, 1);
});
