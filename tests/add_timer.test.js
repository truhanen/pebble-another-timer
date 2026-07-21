// SPDX-License-Identifier: GPL-3.0-only
const test = require('node:test');
const assert = require('node:assert');
const { appendCustomTimer } = require('../src/pkjs/add_timer');

function fakeStore(init) {
  const m = new Map(Object.entries(init || {}));
  return {
    get: (k) => (m.has(k) ? m.get(k) : null),
    set: (k, v) => m.set(k, v),
    dump: () => m,
  };
}
const RS = '\x1e', US = '\x1f';

test('appends an unnamed timer to timer_config', () => {
  const s = fakeStore({ timer_config: 'Egg' + US + '300' });
  const str = appendCustomTimer(s.get, s.set, 120);
  assert.strictEqual(str, 'Egg' + US + '300' + US + '0' + RS + US + '120' + US + '0');
  assert.strictEqual(s.get('timer_config'), str);
});

test('stores the watch-assigned id (AddTimerId) instead of leaving it 0', () => {
  const s = fakeStore({ timer_config: '' });
  const str = appendCustomTimer(s.get, s.set, 120, 'Egg', 2147483690);
  assert.strictEqual(str, 'Egg' + US + '120' + US + '2147483690');
});

test('mirrors the new timer into the clay-settings TimerList', () => {
  const s = fakeStore({ timer_config: '', 'clay-settings': JSON.stringify({ SortOrder: '0' }) });
  appendCustomTimer(s.get, s.set, 90);
  const cs = JSON.parse(s.get('clay-settings'));
  assert.deepStrictEqual(cs.TimerList, [{ name: '', seconds: 90, id: 0 }]);
  assert.strictEqual(cs.SortOrder, '0'); // other keys preserved
});

test('rejects invalid or full', () => {
  const s = fakeStore({ timer_config: '' });
  assert.strictEqual(appendCustomTimer(s.get, s.set, 0), null);
  assert.strictEqual(appendCustomTimer(s.get, s.set, -5), null);
  // fill to MAX_TIMERS (16)
  const many = [];
  for (let i = 0; i < 16; i++) { many.push('t' + US + '60'); }
  const s2 = fakeStore({ timer_config: many.join(RS) });
  assert.strictEqual(appendCustomTimer(s2.get, s2.set, 60), null);
});
