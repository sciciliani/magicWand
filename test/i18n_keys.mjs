// Every English text the app shows (keys for app/lang/*.js). Used by test/test_i18n.mjs.
import { readFileSync } from 'node:fs';
const root = new URL('..', import.meta.url).pathname;
const read = (f) => readFileSync(root + f, 'utf8');
const IGNORE = new Set(['NO (', 'English text', 'err']);
const keys = new Set();
const add = (s) => { const k = (s || '').replace(/\s+/g, ' ').trim(); if (/[A-Za-z]/.test(k) && !IGNORE.has(k)) keys.add(k); };
const unq = (s) => s.slice(1, -1).replace(/\\'/g, "'").replace(/\\"/g, '"');
const STR = `('(?:[^'\\\\]|\\\\.)*'|"(?:[^"\\\\]|\\\\.)*")`;

// HTML
const html = read('index.html');
for (const m of html.matchAll(/<(\w+)\b[^>]*?\sdata-i18n(?=[\s>])[^>]*>([\s\S]*?)<\/\1>/g)) add(m[2]);
for (const m of html.matchAll(/data-i18n-(?:placeholder|title|alt)="([^"]+)"/g)) add(m[1]);
// t('...') everywhere, say('...') / say(x ? '...' : '...') in training.js
for (const f of ['app/app.js', 'app/wand-link.js', 'app/training.js']) {
  const src = read(f);
  for (const m of src.matchAll(new RegExp(`\\bt\\(\\s*${STR}`, 'g'))) add(unq(m[1]));
  for (const m of src.matchAll(new RegExp(`\\bsay\\(\\s*${STR}`, 'g'))) add(unq(m[1]));
  if (f !== 'app/wand-link.js') for (const m of src.matchAll(new RegExp(`\\?\\s*${STR}\\s*:\\s*${STR}`, 'g')))
    for (const x of [unq(m[1]), unq(m[2])]) if ((/\s/.test(x) || /^[A-Z][a-z]/.test(x)) && !IGNORE.has(x)) add(x);
}
// training.js extras: the count and retry lines
const tr = read('app/training.js');
for (const m of tr.matchAll(new RegExp(`(?:BEATS = \\[|startCycle\\(false, )([^\\]\\n]*)`, 'g')))
  for (const s of m[1].matchAll(new RegExp(STR, 'g'))) add(unq(s[1]));
for (const m of tr.matchAll(/\? '([A-Z][^']*[.!?…])' : null/g)) add(m[1]);
// app.js suggested moves
for (const m of read('app/app.js').matchAll(/(?:move|use): '([^']+)'/g)) add(m[1]);
// training-moves.js: names, how, verdicts
const tm = read('app/training-moves.js');
for (const m of tm.matchAll(/(?:name|how): '([^']+)'/g)) add(m[1]);
for (const m of tm.matchAll(new RegExp(`(?:no\\('\\w+',\\s*|say: |\\? |: |wrongKind = |)${STR}`, 'g'))) {
  const s = unq(m[1]);
  if (/[a-z] [a-z]/i.test(s) && /[.!]$/.test(s)) add(s);
}
// remote library
const { LIBRARY } = await import(root + 'app/ir-presets.js');
for (const [dev, brands] of Object.entries(LIBRARY)) {
  add(dev);
  for (const [brand, b] of Object.entries(brands)) {
    if (/[a-z]{3}/.test(brand) && !/^(Samsung|Sony|Toshiba|Panasonic|Vizio|LG|RCA|Midea \/ Coolix)$/.test(brand)) add(brand);
    add(b.note);
    for (const a of b.actions) add(a.label);
  }
}
export default [...keys];
if (process.argv[1].endsWith('i18n_keys.mjs')) console.log(JSON.stringify([...keys], null, 1));
