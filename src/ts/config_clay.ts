// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Tuomas Airaksinen

// Clay config for the Countdown timer app. The single `timerList` custom
// component (messageKey 'TimerList') is a Clay-only key (NOT in package.json
// messageKeys) — index.ts serializes it to the real CString key 'TimerConfig'
// on save, exactly as TimeStyle maps WidgetList -> SettingWidgetList.
// `KeyboardOptions` and `AutoReturnOptions` (checkboxgroups) are the same
// trick, for a different reason: autoHandleEvents is off, so we build the
// AppMessage dict ourselves instead of letting Clay's own
// `prepareSettingsForAppMessage` split an array-syntax messageKey into
// consecutive int keys — a raw array value assigned to one key on a
// manually-built dict is sent as a single array-typed AppMessage tuple, not
// split, so it must NOT be declared as an array-syntax key in package.json.
// index.ts decomposes each boolean array into two real scalar keys instead:
// KeyboardOnNewTimer / KeyboardOnMainTouch, and AutoReturnStart / AutoReturnStop.
const config = [
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Saved timers' },
      { type: 'text', defaultValue:
        'Add & label saved timers below.<br><br>' +
        'Saved timers are not deleted after they finish, and can be reused.<br><br>' +
        'Can be configured also on the watch.'
      },
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
      { type: 'checkboxgroup', messageKey: 'KeyboardOptions',
        label: 'Show label keyboard',
        description: 'Prompt for label after picking duration for a new timer',
        defaultValue: [true, false],
        options: ['"+ New timer"', 'Main view touch dial'] },
    ],
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Timer behavior' },
      { type: 'toggle', messageKey: 'LaunchSync',
        label: 'Launch-sync timer starts',
        description: 'When on, starting a timer subtracts the elapsed time since app launch from the timer.',
        defaultValue: false },
      { type: 'toggle', messageKey: 'RunOnCreate',
        label: 'Run timer when created',
        description: 'When off, a newly created timer starts out stopped instead of running immediately.',
        defaultValue: true },
      // radiogroup values MUST be strings (Clay gotcha); index.ts parseInts on save.
      { type: 'radiogroup', messageKey: 'DefaultFinishAction', label: 'Default action after timer finishes',
        description: 'Default for newly created timers. Can be changed per timer from the watch\'s ' +
          'timer edit menu.',
        defaultValue: '1', options: [
          { label: 'Save timer', value: '0' },
          { label: 'Delete timer', value: '1' },
        ] },
    ],
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'App behavior' },
      { type: 'checkboxgroup', messageKey: 'AutoReturnOptions',
        label: 'Return to watchface',
        description: 'When on, the app closes back to the watchface once you start or stop a timer.',
        defaultValue: [true, true],
        options: ['After starting a timer', 'After stopping a timer'] },
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
  { type: 'submit', defaultValue: 'Save' },
];

export = config;
