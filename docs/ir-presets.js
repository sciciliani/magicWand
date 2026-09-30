// ir-presets.js — turn well-known TV codes into the wand's raw format.
//
// Raw format: array of microsecond durations, alternating mark (LED blinking)
// and space (LED off), starting with a mark. Same as the firmware's IrCode.
// Presets are the widely published codes for each brand; if one doesn't work
// on your TV, use "Learn from remote" instead.

const bitsLSB = (value, count) => Array.from({ length: count }, (_, i) => (value >> i) & 1);

function pulseDistance(header, bits, { mark = 560, one = 1690, zero = 560 } = {}) {
  const d = [...header];
  for (const b of bits) d.push(mark, b ? one : zero);
  d.push(mark); // stop bit
  return d;
}

/** NEC: 8-bit address (or 16-bit extended), 8-bit command. 38 kHz. */
export function nec(addr, cmd) {
  const addrBits =
    addr > 0xff ? bitsLSB(addr, 16) : [...bitsLSB(addr, 8), ...bitsLSB(~addr & 0xff, 8)];
  const bits = [...addrBits, ...bitsLSB(cmd, 8), ...bitsLSB(~cmd & 0xff, 8)];
  return { khz: 38, d: pulseDistance([9000, 4500], bits) };
}

/** Samsung32: like NEC but 4.5 ms header and the address sent twice. 38 kHz. */
export function samsung32(addr, cmd) {
  const bits = [...bitsLSB(addr, 8), ...bitsLSB(addr, 8), ...bitsLSB(cmd, 8), ...bitsLSB(~cmd & 0xff, 8)];
  return { khz: 38, d: pulseDistance([4500, 4500], bits) };
}

/** Sony SIRC (12/15/20 bit), sent 3 times as Sony TVs expect. 40 kHz. */
export function sony(cmd, addr, nbits = 12) {
  const addrBits = nbits - 7;
  const bits = [...bitsLSB(cmd, 7), ...bitsLSB(addr, addrBits)];
  const frame = [2400];
  for (const b of bits) frame.push(600, b ? 1200 : 600); // space, mark
  const frameLen = frame.reduce((a, b) => a + b, 0);
  const d = [];
  for (let r = 0; r < 3; r++) {
    if (r > 0) d.push(45000 - frameLen); // gap: frames start every 45 ms
    d.push(...frame);
  }
  return { khz: 40, d };
}

/** Protocol timings, used to build a code from its hex value (bits in the
 *  order they are sent, exactly as tools/IrDump and the wand print them). */
export const PROTOCOLS = {
  nec: { khz: 38, hdr: [9000, 4500], mark: 560, one: 1690, zero: 560, gap: 40000, reps: 1 },
  samsung: { khz: 38, hdr: [4500, 4500], mark: 560, one: 1690, zero: 560, gap: 47000, reps: 1 },
  kaseikyo: { khz: 37, hdr: [3456, 1728], mark: 432, one: 1296, zero: 432, gap: 74000, reps: 1 }, // Panasonic
  coolix: { khz: 38, hdr: [4692, 4692], mark: 552, one: 1656, zero: 552, gap: 5244, reps: 2 },    // Midea & co.
};

/** Build a raw code from a hex value, e.g. fromHex('coolix', 'B24D7B84E01F'). */
export function fromHex(proto, hex) {
  const p = PROTOCOLS[proto];
  const bits = [];
  for (const ch of hex.replace(/^0x/i, '')) {
    const v = parseInt(ch, 16);
    for (let k = 3; k >= 0; k--) bits.push((v >> k) & 1);
  }
  const frame = pulseDistance(p.hdr, bits, p);
  const d = [];
  for (let r = 0; r < p.reps; r++) {
    if (r > 0) d.push(p.gap);
    d.push(...frame);
  }
  return { khz: p.khz, d };
}

const samsung = (cmd) => () => samsung32(0x07, cmd);
const lg = (cmd) => () => nec(0x04, cmd);
const sonyTv = (cmd) => () => sony(cmd, 1, 12);

export const PRESETS = {
  'Samsung TV': {
    Power: samsung(0x02), 'Volume up': samsung(0x07), 'Volume down': samsung(0x0b), Mute: samsung(0x0f),
    'Channel up': samsung(0x12), 'Channel down': samsung(0x10), Source: samsung(0x01),
  },
  'LG TV': {
    Power: lg(0x08), 'Volume up': lg(0x02), 'Volume down': lg(0x03), Mute: lg(0x09),
    'Channel up': lg(0x00), 'Channel down': lg(0x01), Input: lg(0x0b),
  },
  'Sony TV': {
    Power: sonyTv(21), 'Volume up': sonyTv(18), 'Volume down': sonyTv(19), Mute: sonyTv(20),
    'Channel up': sonyTv(16), 'Channel down': sonyTv(17), Input: sonyTv(37),
  },
};

/** Best-effort guess of what a learned code is, for display only. */
export function describe(d, khz) {
  if (!d || !d.length) return 'empty';
  const ms = (d.reduce((a, b) => a + b, 0) / 1000).toFixed(0);
  let kind = 'raw';
  if (Math.abs(d[0] - 9000) < 1500 && Math.abs(d[1] - 4500) < 900) kind = 'NEC-like';
  else if (Math.abs(d[0] - 4500) < 900 && Math.abs(d[1] - 4500) < 900) kind = 'Samsung-like';
  else if (Math.abs(d[0] - 2400) < 500) kind = 'Sony-like';
  else if (d.length > 150) kind = 'long (AC?)';
  return `${kind} · ${d.length} pulses · ${ms} ms · ${khz} kHz`;
}

// ------------------------------------------------------------------ remote library
// Device -> brand -> actions. Each action either has `make` (a known code the
// app can send for a test) or `learn: true` (copy it from your own remote:
// air conditioners send their whole state, so each setting is its own code).
// `short` becomes the code's name on the wand (max 15 characters).
const hex = (proto, h) => () => fromHex(proto, h);
const learn = { learn: true };

const tvActions = (m) => Object.entries(m).map(([label, [short, make]]) => ({ label, short, make }));

const SAMSUNG = tvActions({
  Power: ['Pwr', samsung(0x02)], 'Power off': ['Off', samsung(0x98)],
  'Volume up': ['VolUp', samsung(0x07)], 'Volume down': ['VolDn', samsung(0x0b)], Mute: ['Mute', samsung(0x0f)],
  'Channel up': ['ChUp', samsung(0x12)], 'Channel down': ['ChDn', samsung(0x10)], 'Input / source': ['Input', samsung(0x01)],
});
const LG = tvActions({
  Power: ['Pwr', lg(0x08)], 'Power on': ['On', lg(0xc4)], 'Power off': ['Off', lg(0xc5)],
  'Volume up': ['VolUp', lg(0x02)], 'Volume down': ['VolDn', lg(0x03)], Mute: ['Mute', lg(0x09)],
  'Channel up': ['ChUp', lg(0x00)], 'Channel down': ['ChDn', lg(0x01)], 'Input / source': ['Input', lg(0x0b)],
});
const SONY = tvActions({
  Power: ['Pwr', sonyTv(21)], 'Power on': ['On', sonyTv(46)], 'Power off': ['Off', sonyTv(47)],
  'Volume up': ['VolUp', sonyTv(18)], 'Volume down': ['VolDn', sonyTv(19)], Mute: ['Mute', sonyTv(20)],
  'Channel up': ['ChUp', sonyTv(16)], 'Channel down': ['ChDn', sonyTv(17)], 'Input / source': ['Input', sonyTv(37)],
});
const toshiba = (cmd) => () => nec(0x40, cmd);
const TOSHIBA = tvActions({
  Power: ['Pwr', toshiba(0x12)], 'Volume up': ['VolUp', toshiba(0x1a)], 'Volume down': ['VolDn', toshiba(0x1e)],
  Mute: ['Mute', toshiba(0x10)], 'Channel up': ['ChUp', toshiba(0x1b)], 'Channel down': ['ChDn', toshiba(0x1f)],
  'Input / source': ['Input', toshiba(0x0f)],
});
const pana = (h) => hex('kaseikyo', h);
const PANASONIC = tvActions({
  Power: ['Pwr', pana('40040100BCBD')], 'Volume up': ['VolUp', pana('400401000405')],
  'Volume down': ['VolDn', pana('400401008485')], Mute: ['Mute', pana('400401004C4D')],
  'Channel up': ['ChUp', pana('400401002C2D')], 'Channel down': ['ChDn', pana('40040100ACAD')],
  'Input / source': ['Input', pana('40040100A0A1')],
});
const TV_LEARN = [
  { label: 'Power', short: 'Pwr', ...learn }, { label: 'Volume up', short: 'VolUp', ...learn },
  { label: 'Volume down', short: 'VolDn', ...learn }, { label: 'Mute', short: 'Mute', ...learn },
  { label: 'Channel up', short: 'ChUp', ...learn }, { label: 'Channel down', short: 'ChDn', ...learn },
  { label: 'Input / source', short: 'Input', ...learn },
];
const AC_LEARN = [
  { label: 'On · cool (set the remote first)', short: 'Cool', ...learn },
  { label: 'On · heat (set the remote first)', short: 'Heat', ...learn },
  { label: 'On · fan only', short: 'Fan', ...learn },
  { label: 'Off', short: 'Off', ...learn },
  { label: 'Cooler temperature', short: 'Cold', ...learn },
  { label: 'Warmer temperature', short: 'Warm', ...learn },
];
const strip = (h) => hex('nec', h);

export const LIBRARY = {
  TV: {
    Samsung: { short: 'Sams', note: 'Most Samsung TVs', actions: SAMSUNG },
    LG: { short: 'LG', note: 'Most LG TVs', actions: LG },
    Vizio: { short: 'Vizio', note: 'Vizio uses the same codes as LG', actions: LG },
    Sony: { short: 'Sony', note: 'Bravia and older Sony TVs', actions: SONY },
    Toshiba: { short: 'Tosh', note: 'Older Toshiba TVs (NEC)', actions: TOSHIBA },
    Panasonic: { short: 'Pana', note: 'Viera TVs', actions: PANASONIC },
    'Other brand (learn)': { short: 'TV', note: 'Copy each button from your remote', actions: TV_LEARN },
  },
  'Air conditioner': {
    'Midea / Coolix': {
      short: 'AC',
      note: 'Midea and brands built on Midea units (Carrier, Electrolux, Beko, Comfee, Toshiba…). ' +
        'Off is a fixed code; every other setting is sent as a whole (mode + temperature + fan), so learn the ones you use.',
      actions: [{ label: 'Off', short: 'Off', make: hex('coolix', 'B24D7B84E01F') }, ...AC_LEARN.filter((a) => a.short !== 'Off')],
    },
    'Any brand (learn)': {
      short: 'AC',
      note: 'Set the remote to exactly what the spell should do (mode, temperature, fan), then learn it.',
      actions: AC_LEARN,
    },
  },
  'LED strip': {
    '24-key remote': {
      short: 'LED', note: 'The common small remote with 24 buttons (rows of colours)',
      actions: [
        { label: 'On', short: 'On', make: strip('00F7C03F') }, { label: 'Off', short: 'Off', make: strip('00F740BF') },
        { label: 'Brighter', short: 'Bright', make: strip('00F700FF') }, { label: 'Dimmer', short: 'Dim', make: strip('00F7807F') },
        { label: 'Red', short: 'Red', make: strip('00F720DF') }, { label: 'Green', short: 'Green', make: strip('00F7A05F') },
        { label: 'Blue', short: 'Blue', make: strip('00F7609F') }, { label: 'White', short: 'White', make: strip('00F7E01F') },
      ],
    },
    '44-key remote': {
      short: 'LED', note: 'The larger remote with 44 buttons',
      actions: [
        { label: 'Power', short: 'Pwr', make: strip('00FF02FD') },
        { label: 'Brighter', short: 'Bright', make: strip('00FF3AC5') }, { label: 'Dimmer', short: 'Dim', make: strip('00FFBA45') },
        { label: 'Red', short: 'Red', make: strip('00FF1AE5') }, { label: 'Green', short: 'Green', make: strip('00FF9A65') },
        { label: 'Blue', short: 'Blue', make: strip('00FFA25D') }, { label: 'White', short: 'White', make: strip('00FF22DD') },
      ],
    },
  },
  'Other device': {
    'Any remote (learn)': {
      short: 'Dev',
      note: 'Projectors, soundbars, fans, set-top boxes… copy any button from its remote.',
      actions: Array.from({ length: 6 }, (_, k) => ({ label: `Button ${k + 1}`, short: `Btn${k + 1}`, ...learn })),
    },
  },
};
