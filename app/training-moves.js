// training-moves.js — the Training tab's move checker. No DOM: tested by
// test/test_training.mjs with simulated wand data.
//
// Input is the wand's live stream, "F right up twist thrust energy" at 25 Hz
// (STREAM 1). right/up = how fast the tip moves sideways/up (x100, 100 = 150
// deg/s), the same whichever way the wand is held. energy ≈ deg/s.
//
// Three fixed moves:
//   circle  a full loop with the tip, about 1 s
//   hit     a quick snap up and back down
//   zigzag  side to side fast: right, left, right

export const MOVES = {
  circle: {
    name: 'Circle',
    how: 'Draw ONE big circle in the air with the tip of the wand, nice and round, in about one second.',
    path: 'M50 14 A36 36 0 1 1 49.9 14',
  },
  hit: {
    name: 'Hit',
    how: 'ONE quick snap: tip up, and straight back down. Fast!',
    path: 'M42 86 L50 16 L58 86',
  },
  zigzag: {
    name: 'ZigZag',
    how: 'Side to side, fast: right, left, right. Keep the tip level.',
    path: 'M12 32 L88 42 L12 56 L88 68',
  },
};

// Tuning (all times in seconds, speeds in deg/s of the tip). Every attempt is
// shown in the Log tab with its numbers, to adjust these on a real wand.
export const TUNE = {
  startEnergy: 50,   // a move starts above this...
  stopEnergy: 35,    // ...and ends after stopHold samples below this
  stopHold: 10,      // 400 ms of calm (a slow zigzag pauses at each end)
  minPath: 15,       // shorter tip paths (deg) are just jitter
  maxDur: 4,         // longer = not one move
  // slow/fast compare the move's core (where the tip is above a third of
  // its top speed), not the wobble after a hard stop.
  circle: { minTurn: 270, almostTurn: 150, minRound: 0.3, slow: 1.5, minPeak: 150, fast: 0.45 },
  hit: { vsSide: 1.8, slow: 0.6, minPeak: 200, fast: 0.08 },
  zigzag: { vsUp: 1.5, minRev: 2, slow: 1.5, minPeak: 150, fast: 0.25 },
};

const DT = 1 / 25;
const K = 1.5; // feature x100 -> deg/s

// Cuts the stream into moves: push() every F row, returns a finished move
// (array of rows) or null.
export class MoveCatcher {
  constructor() { this.reset(); }
  reset() { this.rows = []; this.pre = []; this.active = false; this.quiet = 0; }
  push(row) {
    const e = row[4];
    if (!this.active) {
      this.pre.push(row);
      if (this.pre.length > 2) this.pre.shift();
      if (e > TUNE.startEnergy) { this.active = true; this.rows = [...this.pre]; this.quiet = 0; }
      return null;
    }
    this.rows.push(row);
    this.quiet = e < TUNE.stopEnergy ? this.quiet + 1 : 0;
    const tooLong = this.rows.length * DT > TUNE.maxDur;
    if (this.quiet < TUNE.stopHold && !tooLong) return null;
    const seg = this.rows.slice(0, this.rows.length - Math.max(0, this.quiet - 1));
    seg.tooLong = tooLong;
    this.active = false; this.pre = []; this.rows = []; this.quiet = 0;
    return measure(seg).path >= TUNE.minPath ? seg : null;
  }
}

// The wand's "right" signal can flip sign for a sample when the tip points
// almost straight up. A lone sample whose sign disagrees with both neighbours
// is replaced by their average (real peaks keep their sign and stay).
function despike(seg) {
  const fix = (p, v, n) => (Math.sign(p) === Math.sign(n) && Math.sign(v) === -Math.sign(p) && Math.abs(p) > 20
    ? (p + n) / 2 : v);
  return seg.map((row, i) => {
    if (i === 0 || i === seg.length - 1) return row;
    const [p, n] = [seg[i - 1], seg[i + 1]];
    return [fix(p[0], row[0], n[0]), fix(p[1], row[1], n[1]), row[2], row[3], row[4]];
  });
}

// Numbers that describe one move.
export function measure(raw) {
  const seg = despike(raw);
  let path = 0, side = 0, up = 0, twist = 0, thrust = 0, peak = 0, peakUp = 0, turn = 0;
  let x = 0, y = 0, minX = 0, maxX = 0, minY = 0, maxY = 0, lastA = null, rSign = 0, uSign = 0, rRev = 0, uRev = 0;
  const speeds = seg.map(([r, u]) => Math.hypot(r * K, u * K));
  for (const [r, u, t, th] of seg) {
    const vr = r * K, vu = u * K, s = Math.hypot(vr, vu);
    path += s * DT; side += Math.abs(vr) * DT; up += Math.abs(vu) * DT;
    twist += Math.abs(t * K) * DT; thrust += Math.abs(th) / 100 * DT;
    peak = Math.max(peak, s); peakUp = Math.max(peakUp, Math.abs(vu));
    x += vr * DT; y += vu * DT;
    minX = Math.min(minX, x); maxX = Math.max(maxX, x); minY = Math.min(minY, y); maxY = Math.max(maxY, y);
    if (s > 40) {  // direction of travel, only when really moving
      const a = Math.atan2(vu, vr);
      let d = lastA === null ? 0 : a - lastA;
      while (d > Math.PI) d -= 2 * Math.PI;
      while (d < -Math.PI) d += 2 * Math.PI;
      // A real move turns at most ~60° between samples (25 Hz). Bigger jumps
      // are the "right" signal flipping for a sample or two: skip them.
      if (Math.abs(d) < 1.75) { turn += d; lastA = a; } else if (lastA === null) lastA = a;
    } else lastA = null;
    if (Math.abs(vr) > 40) { const sg = Math.sign(vr); if (rSign && sg !== rSign) rRev++; rSign = sg; }
    if (Math.abs(vu) > 40) { const sg = Math.sign(vu); if (uSign && sg !== uSign) uRev++; uSign = sg; }
  }
  const w = maxX - minX, h = maxY - minY;
  const core = speeds.map((v, i) => (v >= peak * 0.35 ? i : -1)).filter((i) => i >= 0);
  const active = core.length ? (core[core.length - 1] - core[0] + 1) * DT : 0;
  return {
    dur: seg.length * DT, active, path, side, up, twist, thrust, peak, peakUp,
    turn: Math.abs(turn) * 180 / Math.PI, round: Math.min(w, h) / Math.max(w, h, 1e-6),
    rRev, uRev, tooLong: !!raw.tooLong,
  };
}

// Verdict for one attempt at `target`: { ok, kind: yes|faster|slower|shape, say }
export function judge(target, m) {
  const no = (kind, say) => ({ ok: false, kind, say });
  const T = TUNE[target];
  const wrongKind = m.twist > m.path * 0.8 ? 'That was a twist of the wrist. Move the whole tip instead.'
    : m.thrust * 150 > m.path ? 'That was a poke forward. Move the tip through the air instead.' : null;
  if (m.tooLong) return no('faster', 'Too slow, I lost track. One quick, clean move.');
  if (target === 'circle') {
    if (m.active > T.slow || m.peak < T.minPeak) return no('faster', 'Faster! A circle takes about one second.');
    if (wrongKind) return no('shape', wrongKind);
    if (m.turn < T.minTurn || m.round < T.minRound) {
      if (m.up > m.side * 2) return no('shape', 'That went up and down. Draw a round loop instead.');
      if (m.side > m.up * 2) return no('shape', 'That went side to side. Draw a round loop instead.');
      if (m.active < T.fast) return no('slower', 'Slower, and nice and round.');
      if (m.turn >= T.almostTurn) return no('shape', 'Almost! Close the circle all the way round.');
      return no('shape', 'Draw a round loop with the tip.');
    }
    if (m.active < T.fast) return no('slower', 'Slower! Draw it calmly.');
    return { ok: true, kind: 'yes', say: 'Yes! A fine circle.' };
  }
  if (target === 'hit') {
    if (m.active > T.slow || m.peak < T.minPeak) return no('faster', 'Faster! A quick snap, like cracking a whip.');
    if (wrongKind) return no('shape', wrongKind);
    if (m.side * T.vsSide > m.up) return no('shape', 'Up and down only, not sideways.');
    if (m.uRev < 1) return no('shape', 'Up, and straight back down.');
    if (m.turn >= TUNE.circle.minTurn) return no('shape', 'That was a loop. Just up and down.');
    if (m.active < T.fast) return no('slower', 'A little slower, a clear up and down.');
    return { ok: true, kind: 'yes', say: 'Yes! A clean hit.' };
  }
  if (target === 'zigzag') {
    if (m.active > T.slow || m.peak < T.minPeak) return no('faster', 'Faster! Snap side to side.');
    if (wrongKind) return no('shape', wrongKind);
    if (m.up * T.vsUp > m.side) return no('shape', 'Side to side, keep the tip level.');
    if (m.turn >= TUNE.circle.minTurn) return no('shape', 'That was a loop. Straight strokes: right, left, right.');
    if (m.rRev < T.minRev) return no('shape', 'More zigs: right, left, right.');
    if (m.active < T.fast) return no('slower', 'A bit slower, three clear strokes.');
    return { ok: true, kind: 'yes', say: 'Yes! A sharp zigzag.' };
  }
  return no('shape', 'Hmm.');
}

// One line for the Log tab.
export function describe(m) {
  return `dur ${m.dur.toFixed(2)}s core ${m.active.toFixed(2)}s peak ${m.peak | 0}°/s path ${m.path | 0}° side ${m.side | 0} up ${m.up | 0} ` +
    `turn ${m.turn | 0}° round ${m.round.toFixed(2)} revR ${m.rRev} revU ${m.uRev}`;
}
