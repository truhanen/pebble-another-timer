// SPDX-License-Identifier: GPL-3.0-only
const test = require('node:test');
const assert = require('node:assert');
const { updateTimer } = require('../src/pkjs/update_timer');

function fakeStore(init) {
  const m = new Map(Object.entries(init || {}));
  return {
    get: (k) => (m.has(k) ? m.get(k) : null),
    set: (k, v) => m.set(k, v),
  };
}
const RS = '\x1e', US = '\x1f';

test('overwrites timer duration at index', () => {
  const s = fakeStore({ timer_config: 'Egg' + US + '300' + RS + 'Tea' + US + '120' });
  const str = updateTimer(s.get, s.set, 1, 90);
  assert.strictEqual(str, 'Egg' + US + '300' + RS + 'Tea' + US + '90');
  assert.strictEqual(s.get('timer_config'), str);
});

test('mirrors overwrite into the clay-settings TimerList', () => {
  const s = fakeStore({
    timer_config: 'Egg' + US + '300',
    'clay-settings': JSON.stringify({ SortOrder: '0' }),
  });
  updateTimer(s.get, s.set, 0, 180);
  const cs = JSON.parse(s.get('clay-settings'));
  assert.deepStrictEqual(cs.TimerList, [{ name: 'Egg', seconds: 180 }]);
  assert.strictEqual(cs.SortOrder, '0');
});

test('invalid index or seconds returns null and leaves storage untouched', () => {
  const s = fakeStore({ timer_config: 'Egg' + US + '300' });
  assert.strictEqual(updateTimer(s.get, s.set, 2, 100), null);
  assert.strictEqual(updateTimer(s.get, s.set, 0, 0), null);
  assert.strictEqual(s.get('timer_config'), 'Egg' + US + '300');
});
