// SPDX-License-Identifier: GPL-3.0-only
const test = require('node:test');
const assert = require('node:assert');
const config = require('../src/pkjs/config_clay');

// Top-level index of a `section` whose items contain a heading with the given text.
function sectionIndexByHeading(cfg, headingText) {
  return cfg.findIndex((item) =>
    item.type === 'section' &&
    Array.isArray(item.items) &&
    item.items.some((c) => c.type === 'heading' && c.defaultValue === headingText));
}

// Concatenate all `text` item defaultValues inside the section at `idx`.
function sectionText(cfg, idx) {
  return cfg[idx].items
    .filter((c) => c.type === 'text')
    .map((c) => c.defaultValue)
    .join('\n');
}

test('config has a "Using the timers on your watch" guide section', () => {
  const idx = sectionIndexByHeading(config, 'Using the timers on your watch');
  assert.notStrictEqual(idx, -1, 'guide section not found');
});

test('guide documents the non-intuitive long press and the menu actions', () => {
  const idx = sectionIndexByHeading(config, 'Using the timers on your watch');
  const text = sectionText(config, idx).toLowerCase();
  assert.match(text, /long press/, 'long-press interaction must be documented');
  assert.match(text, /start/, 'Start action must be documented');
  assert.match(text, /save/, 'Start & Save action must be documented');
  assert.match(text, /delete/, 'Delete action must be documented');
  assert.match(text, /snooze/, 'alarm snooze must be documented');
});

test('guide sits after Behavior and before the Save button', () => {
  const guideIdx = sectionIndexByHeading(config, 'Using the timers on your watch');
  const behaviorIdx = sectionIndexByHeading(config, 'Behavior');
  const submitIdx = config.findIndex((item) => item.type === 'submit');
  assert.ok(behaviorIdx !== -1 && submitIdx !== -1);
  assert.ok(guideIdx > behaviorIdx, 'guide should come after the Behavior section');
  assert.ok(guideIdx < submitIdx, 'guide should come before the Save button');
});
