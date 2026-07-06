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
      { type: 'text', defaultValue:
        'Add timers below. On the watch: short Select on a stopped timer starts it; ' +
        'short Select on a running/paused timer opens quick controls; long Select opens the duration dial.' },
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
      { type: 'toggle', messageKey: 'LaunchSync',
        label: 'Launch-sync starts',
        description: 'When on, starts subtract elapsed time since app launch from the started timer.',
        defaultValue: false },
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
        'on a running or paused timer, opens quick controls.<br>' +
        '• <b>Select (long press / hold)</b> — opens the duration dial for <i>any</i> timer.<br>' +
        '• <b>Touch screen</b> — opens the touch dial; when you finish touch selection, ' +
        'the chosen duration starts immediately as an unsaved timer.' },
      { type: 'text', defaultValue:
        '<b>Quick controls menu</b> (short Select on running/paused timer)<br>' +
        '• Start/Pause, +1 min/-1 min, and Stop.' },
      { type: 'text', defaultValue:
        '<b>Duration dial</b> (opened first on long press and for New timer)<br>' +
        '• Three boxes: hours, minutes, and seconds.<br>' +
        '• Up/Down changes the selected field. Hours clamp at 0..100; minutes/seconds wrap 0..59. Hold repeats.<br>' +
        '• Select moves right; Select on seconds opens actions. Back moves left; Back on hours cancels editing.<br>' +
        '• Touch opens the touch dial for direct duration selection.<br>' +
        '• In touch dial, to enter second-precision mode: rotate anticlockwise past 6 o\'clock, then past 12 o\'clock while at 0h.<br>' +
        '• On existing timers, long press Select on the dial opens delete confirmation.' },
      { type: 'text', defaultValue:
        '<b>Edit/save actions</b> (after confirming duration)<br>' +
        '• <b>Start unsaved</b> — add and start a temporary timer (not saved, disappears after finishing).<br>' +
        '• <b>Start &amp; save</b> / <b>Only save</b> — then choose save type: ' +
        '<b>As new timer</b> or <b>Overwrite current</b>.<br>' +
        '• <b>Delete timer</b> — remove the selected timer (asks to confirm).' },
      { type: 'text', defaultValue:
          '<b>New timer menu</b><br>' +
          '• Same as timer edit/save menu except no Delete and no save-type submenu ' +
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
