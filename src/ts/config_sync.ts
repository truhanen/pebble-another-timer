// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Tuomas Airaksinen

// The watch is a watchapp (not an always-running watchface), so the AppMessage
// sent on `webviewclosed` only lands if the app happened to be open at Save
// time. To make config delivery reliable, the watch asks for its config on
// launch (an inbound AppMessage) and the phone replies with the values it
// persisted on the last Save. This module builds that reply.

// Build the dict to re-send to the watch from persisted values, or null when
// nothing was ever saved on this phone — in that case we stay silent so we do
// NOT clobber whatever the watch already has persisted. `get` reads a stored
// string by key (e.g. window.localStorage.getItem), returning null if absent.
// Returns a Record (not a fixed shape) so it's directly assignable to
// Pebble.sendAppMessage's parameter type.
export function resendDict(get: (k: string) => string | null): Record<string, any> | null {
  const tc = get('timer_config');
  if (tc === null) { return null; }
  const rf = get('running_first');
  const ie = get('idle_exit');
  const ls = get('launch_sync');
  const dfa = get('default_finish_action');
  return {
    TimerConfig: tc,
    SortOrder: parseInt(get('sort_order') || '0', 10) || 0,
    AutoReturn: parseInt(get('auto_return') || '0', 10) || 0,
    RunningFirst: rf === null ? 1 : (parseInt(rf, 10) ? 1 : 0),
    IdleExitSec: ie === null ? 15 : (parseInt(ie, 10) || 0),
    LaunchSync: ls === null ? 0 : (parseInt(ls, 10) ? 1 : 0),
    DefaultFinishAction: dfa === null ? 1 : (parseInt(dfa, 10) ? 1 : 0),
  };
}
