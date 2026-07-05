// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Tuomas Airaksinen

// Clay config for the Countdown timer app. The single `timerList` custom
// component (messageKey 'TimerList') is a Clay-only key (NOT in package.json
// messageKeys) — index.ts serializes it to the real CString key 'TimerConfig'
// on save, exactly as TimeStyle maps WidgetList -> SettingWidgetList.
const config = [
  { type: 'heading', defaultValue: 'Countdown timer' },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Timers' },
      { type: 'text', defaultValue: 'Add timers below. On the watch, open a timer to Start/Pause/Stop it.' },
      { type: 'timerList', messageKey: 'TimerList', defaultValue: [{ name: '', seconds: 0 }] },
    ],
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Display' },
      // radiogroup values MUST be strings (Clay gotcha); index.ts parseInts on save.
      { type: 'radiogroup', messageKey: 'SortOrder', label: 'Sort timers on watch by',
        defaultValue: '0', options: [
          { label: 'Most recently used', value: '0' },
          { label: 'Shortest remaining first', value: '1' },
          { label: 'Longest remaining first', value: '2' },
        ] },
      { type: 'toggle', messageKey: 'RunningFirst',
        label: 'Show running timers at the top', defaultValue: true },
    ],
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Behavior' },
      { type: 'toggle', messageKey: 'AutoReturn',
        label: 'Return to watchface after starting or stopping a timer',
        description: 'When on, the app closes back to the watchface once you start or stop a timer.',
        defaultValue: true },
      // select values MUST be strings (Clay); index.ts parseInts on save.
      { type: 'select', messageKey: 'IdleExitSec',
        label: 'Return to watchface when idle',
        description: 'Close back to the watchface after this many seconds with no button press in the timer list or detail view. Off disables it.',
        defaultValue: '15', options: [
          { label: 'Off', value: '0' },
          { label: '10 seconds', value: '10' },
          { label: '15 seconds', value: '15' },
          { label: '30 seconds', value: '30' },
          { label: '60 seconds', value: '60' },
        ] },
    ],
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Using the timers on your watch' },
      { type: 'text', defaultValue:
        '<b>Timer list</b><br>' +
        '• <b>Up / Down</b> — move between timers.<br>' +
        '• <b>Select</b> (short press) — on a stopped timer, starts it right away; ' +
        'on a running or paused timer, opens its menu.<br>' +
        '• <b>Select (long press / hold)</b> — opens the menu for <i>any</i> timer.' },
      { type: 'text', defaultValue:
        '<b>Timer menu</b> (opened as above)<br>' +
        '• <b>+1 min / +10s</b> — short press adds 1 minute at 1:00 or above, and 10 seconds below 1:00.<br>' +
        '• <b>Hold on the same item</b> — subtract 1 minute above 1:00, and 10 seconds at/below 1:00 (down to 0:10).<br>' +
        '• <b>Start unsaved</b> — add and start a temporary timer (not saved to phone).<br>' +
        '• <b>Start &amp; save</b> / <b>Only save</b> — then choose save type: ' +
        '<b>As new timer</b> or <b>Overwrite current</b>.<br>' +
        '• <b>Delete timer</b> — remove the selected timer (asks to confirm).<br>' +
        '• <b>New timer menu</b> is the same except no Delete and no save-type submenu ' +
        '(save is always as new timer).' },
      { type: 'text', defaultValue:
        '<b>When a timer reaches zero</b><br>' +
        '• <b>Up</b> — +1 min (snooze). ' +
        '• <b>Down</b> — Stop. ' +
        '• <b>Back</b> — snooze.' },
    ],
  },
  { type: 'submit', defaultValue: 'Save' },
];

export = config;
