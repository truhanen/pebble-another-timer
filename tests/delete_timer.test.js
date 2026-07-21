// SPDX-License-Identifier: GPL-3.0-only
const test = require('node:test');
const assert = require('node:assert');
const { deleteTimer } = require('../src/pkjs/delete_timer');

function fakeStore(init) {
  const m = new Map(Object.entries(init || {}));
  return {
    get: (k) => (m.has(k) ? m.get(k) : null),
    set: (k, v) => m.set(k, v),
    dump: () => m,
  };
}
const RS = '\x1e', US = '\x1f';

test('removes the timer at the given index from timer_config', () => {
  const s = fakeStore({ timer_config: 'Egg' + US + '300' + RS + 'Tea' + US + '120' + RS + US + '90' });
  const str = deleteTimer(s.get, s.set, 1);   // drop "Tea"
  assert.strictEqual(str, 'Egg' + US + '300' + US + '0' + RS + US + '90' + US + '0');
  assert.strictEqual(s.get('timer_config'), str);
});

test('mirrors the deletion into the clay-settings TimerList', () => {
  const s = fakeStore({
    timer_config: 'Egg' + US + '300' + RS + 'Tea' + US + '120',
    'clay-settings': JSON.stringify({ SortOrder: '0' }),
  });
  deleteTimer(s.get, s.set, 0);   // drop "Egg"
  const cs = JSON.parse(s.get('clay-settings'));
  assert.deepStrictEqual(cs.TimerList, [{ name: 'Tea', seconds: 120, id: 0 }]);
  assert.strictEqual(cs.SortOrder, '0'); // other keys preserved
});

test('out-of-range index returns null and leaves storage untouched', () => {
  const s = fakeStore({ timer_config: 'Egg' + US + '300' });
  assert.strictEqual(deleteTimer(s.get, s.set, 5), null);
  assert.strictEqual(deleteTimer(s.get, s.set, -1), null);
  assert.strictEqual(s.get('timer_config'), 'Egg' + US + '300');
});
