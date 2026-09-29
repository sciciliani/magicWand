// test_ir.mjs — checks the TV preset encoders against the codes as they are
// usually published (32-bit hex, bits in transmission order).
// Run: node test/test_ir.mjs   (or make test)
import assert from 'node:assert/strict';
import { nec, samsung32, sony, fromHex, PRESETS, LIBRARY } from '../app/ir-presets.js';

// Read a pulse-distance frame back into a hex string, in transmission order.
function decodePulseDistance(d, headerLen = 2) {
  let v = 0n;
  for (let i = headerLen + 1; i < d.length; i += 2) v = (v << 1n) | (d[i] > 1000 ? 1n : 0n);
  return '0x' + v.toString(16).toUpperCase();
}

const cases = [
  ['Samsung power', samsung32(0x07, 0x02), '0xE0E040BF'],
  ['Samsung vol+', samsung32(0x07, 0x07), '0xE0E0E01F'],
  ['Samsung mute', samsung32(0x07, 0x0f), '0xE0E0F00F'],
  ['LG power', nec(0x04, 0x08), '0x20DF10EF'],
  ['LG vol-', nec(0x04, 0x03), '0x20DFC03F'],
  ['LG power on', nec(0x04, 0xc4), '0x20DF23DC'],
  ['LG power off', nec(0x04, 0xc5), '0x20DFA35C'],
  ['Samsung off', samsung32(0x07, 0x98), '0xE0E019E6'],
  ['Toshiba power', nec(0x40, 0x12), '0x2FD48B7'],
  ['LED24 on', fromHex('nec', '00F7C03F'), '0xF7C03F'],
  ['Panasonic pwr', fromHex('kaseikyo', '40040100BCBD'), '0x40040100BCBD'],
];
for (const [name, code, hex] of cases) {
  assert.equal(code.d.length % 2, 1, `${name}: must end with a mark`);
  assert.equal(decodePulseDistance(code.d), hex, name);
  console.log(`ok  ${name.padEnd(14)} ${hex}`);
}

// Sony power: cmd 21, addr 1 -> 12 bits LSB first, 3 frames, 45 ms period.
const s = sony(21, 1);
assert.equal(s.khz, 40);
const frame = s.d.slice(0, 25);
const bits = [];
for (let i = 2; i < frame.length; i += 2) bits.push(frame[i] > 900 ? 1 : 0);
assert.deepEqual(bits, [1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 0, 0]);
const period = s.d.slice(0, 26).reduce((a, b) => a + b, 0);
assert.equal(period, 45000);
console.log('ok  Sony power     3 frames, 45 ms period');

// Every preset must fit the wand (<= 600 durations, all 16-bit).
for (const [brand, buttons] of Object.entries(PRESETS))
  for (const [btn, make] of Object.entries(buttons)) {
    const c = make();
    assert.ok(c.d.length <= 600 && c.d.every((x) => x > 0 && x < 65536), `${brand} ${btn}`);
  }
console.log('ok  all presets fit the firmware limits');

// Coolix: frame sent twice, 5244 us gap, identical halves
const cx = fromHex('coolix', 'B24D7B84E01F');
assert.equal(cx.d.length, 199);
assert.equal(cx.d[99], 5244);
assert.equal(decodePulseDistance(cx.d.slice(0, 99)), '0xB24D7B84E01F');
console.log('ok  Coolix off     0xB24D7B84E01F x2');

// Library: every entry has a label + short name, and codes fit the wand
let nLib = 0;
for (const [dev, brands] of Object.entries(LIBRARY))
  for (const [brand, b] of Object.entries(brands)) {
    assert.ok(b.short && b.actions.length, `${dev}/${brand}`);
    for (const a of b.actions) {
      assert.ok(a.label && a.short && (a.learn || a.make), `${dev}/${brand}/${a.label}`);
      assert.ok(`${b.short}_${a.short}`.length <= 15, `name too long: ${b.short}_${a.short}`);
      if (a.make) { const c = a.make(); assert.ok(c.d.length <= 600 && c.d.length % 2 === 1, `${brand} ${a.label}`); nLib++; }
    }
  }
console.log(`ok  remote library: ${nLib} codes, names fit`);
console.log('\nPASS');
