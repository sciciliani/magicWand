// Training tab move checker vs simulated wand data (build/training_motions.json,
// made by test/gen_training_motions.cpp from the gesture test's hand simulator).
import { readFileSync } from 'node:fs';
import { MoveCatcher, measure, judge, describe } from '../app/training-moves.js';

const file = process.argv.slice(2).find((a) => !a.startsWith('-')) || 'build/training_motions.json';
const data = JSON.parse(readFileSync(file, 'utf8'));
const verbose = process.argv.includes('-v');
let fails = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'}  ${what}`); if (!ok) fails++; };

function catchMove(rows) {
  const c = new MoveCatcher();
  let best = null;
  for (const r of [...rows, ...Array(10).fill([0, 0, 0, 0, 10])]) {
    const seg = c.push(r);
    if (seg && (!best || seg.length > best.length)) best = seg;
  }
  return best;
}

// A real hard snap doesn't stop dead: the wand wobbles for half a second.
// Same hits with that wobble appended must still be accepted.
for (const d of data.filter((x) => x.move === 'hit' && x.kind === 'ok')) {
  const rows = d.f.map((r) => r.slice());
  let last = rows.length - 1;
  while (last > 0 && rows[last][4] < 40) last--;
  const peak = Math.max(...rows.map((r) => Math.abs(r[1])));
  const wob = [];
  for (let i = 0; i < 14; i++) {
    const a = peak * 0.35 * Math.exp(-i / 5) * (i % 2 ? -1 : 1);
    wob.push([Math.round(a * 0.3), Math.round(a), 0, 0, Math.round(Math.abs(a) * 1.5)]);
  }
  rows.splice(last + 1, 0, ...wob);
  data.push({ move: 'hit', kind: 'wobble', f: rows });
}

const stats = {};
const bump = (k, ok) => { stats[k] ??= [0, 0]; stats[k][0] += ok ? 1 : 0; stats[k][1]++; };
for (const d of data) {
  const seg = catchMove(d.f);
  for (const target of ['circle', 'hit', 'zigzag']) {
    if (d.kind === 'ok' && d.move === target) {
      const v = seg && judge(target, measure(seg));
      bump(`${target}: done right -> yes`, v?.ok);
      if (verbose && !v?.ok) console.log('   ', target, v?.say, seg && describe(measure(seg)));
    } else if (d.kind === 'wobble' && d.move === target) {
      const v = seg && judge(target, measure(seg));
      bump(`${target}: hard snap + wobble -> yes`, v?.ok);
      if (verbose && !v?.ok) console.log('   wobble', v?.say, seg && describe(measure(seg)));
    } else if (d.kind === 'slow' && d.move === target) {
      const v = seg && judge(target, measure(seg));
      bump(`${target}: done 3x slower -> "faster"`, v?.kind === 'faster');
      if (verbose && v?.kind !== 'faster') console.log('   slow', target, v?.say, seg ? describe(measure(seg)) : 'not caught');
    } else if (d.kind === 'fast' && d.move === target) {
      const v = seg && judge(target, measure(seg));
      bump(`${target}: rushed -> "slower"`, v?.kind === 'slower');
      if (verbose && v?.kind !== 'slower') console.log('   fast', target, v?.say, seg && describe(measure(seg)));
    } else if (d.move !== target) {
      const v = seg && judge(target, measure(seg));
      bump(`${target}: other moves rejected`, !v?.ok);
      if (verbose && v?.ok) console.log('   accepted', d.move, 'as', target, describe(measure(seg)));
    }
  }
}
const need = { 'wobble -> yes': 0.9, 'done right': 0.9, 'slower"': 0.85, 'faster"': 0.85, rejected: 0.95 };
for (const [k, [ok, n]] of Object.entries(stats)) {
  const min = Object.entries(need).find(([key]) => k.includes(key))[1];
  check(ok / n >= min, `${k}: ${ok}/${n} (need ${Math.round(min * 100)}%)`);
}
console.log(fails ? 'FAIL' : 'PASS');
process.exit(fails ? 1 : 0);
