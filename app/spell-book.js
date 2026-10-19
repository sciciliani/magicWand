// spell-book.js — the spells a wand can learn: names, how to do them, and
// their pictures (drawn here as SVG: the path of the tip plus an arrow).
//
// Every move is built from what the wand measures the same in any grip (tip
// right/left, tip up/down, twist, poke), so the moves below are easy to do and
// easy for the wand to tell apart. The first four are taught by the setup.
//
// Own art: drop app/spells/<id>.png (square, transparent) and it replaces the
// drawing everywhere.
import { t } from './i18n.js?v=5';

const esc = (s) => String(s ?? '').replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);

// wire: the name stored on the wand (A-Z a-z 0-9 - only). move: the Training
// checker that judges it (training-moves.js), or null (the wand alone decides).
// path: the tip's path in a 100x100 box. extra: decorations (rings, key).
export const SPELLS = [
  { id: 'zigzag', wire: 'ZigZag', move: 'zigzag', setup: true, name: 'ZigZag',
    how: 'Side to side, fast: right, left, right. Keep the tip level.',
    path: 'M14 28 L86 38 L14 56 L86 70' },
  { id: 'stroke', wire: 'Stroke', move: 'stroke', setup: true, name: 'Stroke',
    how: 'Start with the tip up high, then ONE firm stroke straight down, and stop.',
    path: 'M50 12 L50 86' },
  { id: 'tipup', wire: 'TipUp', move: 'tipup', setup: true, name: 'Tip up',
    how: 'Start with the tip low, then lift it straight up in ONE quick move, and stop.',
    path: 'M50 88 L50 14' },
  { id: 'circle', wire: 'Circle', move: 'circle', setup: true, name: 'Circle',
    how: 'Draw ONE big circle in the air with the tip of the wand, nice and round, in about one second.',
    path: 'M50 14 A36 36 0 1 1 36 17' },
  { id: 'swishr', wire: 'SwishRight', move: null, name: 'Swish right',
    how: 'One smooth sweep of the tip to your right, like turning a page.',
    path: 'M12 64 Q50 26 88 64' },
  { id: 'swishl', wire: 'SwishLeft', move: null, name: 'Swish left',
    how: 'One smooth sweep of the tip to your left.',
    path: 'M88 64 Q50 26 12 64' },
  { id: 'keyturn', wire: 'KeyTurn', move: null, name: 'Key turn',
    how: 'Keep the tip still and turn your wrist like a key in a lock, a quarter turn.',
    path: 'M72 30 A30 30 0 1 1 28 30', extra: 'key' },
  { id: 'poke', wire: 'Poke', move: null, name: 'Poke',
    how: 'Stab straight forward toward the device and back, like a poke.',
    path: 'M18 82 L62 38', extra: 'rings' },
  { id: 'flick', wire: 'Flick', move: 'hit', name: 'Flick',
    how: 'ONE quick snap: tip up, and straight back down. Fast!',
    path: 'M40 86 L50 16 L60 86' },
  { id: 'swishflick', wire: 'SwishFlick', move: null, name: 'Swish and flick',
    how: 'Sweep the tip to the right, then flick it down at the end.',
    path: 'M10 46 Q38 18 68 34 L80 86' },
  { id: 'counter', wire: 'CircleBack', move: null, name: 'Backward circle',
    how: 'A big circle the other way round (counter-clockwise).',
    path: 'M50 14 A36 36 0 1 0 64 17' },
];
export const SETUP_SPELLS = SPELLS.filter((s) => s.setup);
export const byId = (id) => SPELLS.find((s) => s.id === id);

/** The catalog spell a gesture name on the wand stands for (or null = custom). */
export function spellOfName(wire) {
  const n = String(wire || '').replace(/[_-]/g, '').toLowerCase();
  return SPELLS.find((s) => s.wire.toLowerCase() === n) || null;
}
/** Display name of a wand gesture: the catalog name in the current language, or the user's own. */
export function displayName(wire) {
  const s = spellOfName(wire);
  return s ? t(s.name) : String(wire || '').replace(/_/g, ' ');
}

// ------------------------------------------------------------------ pictures
const art = new Set();   // ids that have a PNG in app/spells/
const artListeners = [];
export const onArt = (fn) => artListeners.push(fn);
if (typeof Image !== 'undefined') for (const s of SPELLS) {
  const img = new Image();
  img.onload = () => { art.add(s.id); artListeners.forEach((fn) => fn()); };
  img.src = `app/spells/${s.id}.png`;
}

let uid = 0;
/**
 * Picture of a spell. `spell`: catalog entry or null (custom: a star with its
 * initial). opts: { size: px, label: text for screen readers, unsure: adds a "?" badge,
 * animate: the spark runs along the path once (call playIcon(el) to replay) }.
 */
export function spellIcon(spell, { size = 64, label = '', unsure = false, animate = false, initial = '' } = {}) {
  const name = label || (spell ? t(spell.name) : initial);
  const badge = unsure ? '<span class="unsure-badge" aria-hidden="true">?</span>' : '';
  if (spell && art.has(spell.id))
    return `<span class="spell-icon" style="--s:${size}px"><img src="app/spells/${spell.id}.png" alt="${esc(name)}" />${badge}</span>`;
  const k = ++uid;
  const grad = `sg${k}`, arrow = `sa${k}`, glow = `sf${k}`;
  let body;
  if (!spell) {
    body = `<path d="M50 14 L59 40 L86 40 L64 56 L72 83 L50 67 L28 83 L36 56 L14 40 L41 40 Z" class="trail" fill="none" stroke="url(#${grad})"/>
      <text x="50" y="58" text-anchor="middle" class="initial">${esc((initial || '?').slice(0, 1).toUpperCase())}</text>`;
  } else {
    const start = spell.path.match(/M\s*([\d.]+)[ ,]([\d.]+)/);
    const extra = spell.extra === 'rings'
      ? '<circle cx="70" cy="30" r="14" class="ring"/><circle cx="70" cy="30" r="6" class="ring"/>'
      : spell.extra === 'key' ? '<circle cx="50" cy="56" r="8" class="ring"/><path d="M50 64 L50 80 M50 74 L57 74" class="ring"/>' : '';
    body = `${extra}<path d="${spell.path}" class="trail" stroke="url(#${grad})" marker-end="url(#${arrow})" filter="url(#${glow})"/>
      ${start ? `<circle cx="${start[1]}" cy="${start[2]}" r="4.5" class="start"/>` : ''}
      ${animate ? `<circle r="5" class="spark"><animateMotion dur="1.4s" begin="indefinite" fill="freeze" path="${spell.path}"/></circle>` : ''}`;
  }
  return `<span class="spell-icon" style="--s:${size}px"><svg viewBox="0 0 100 100" role="img" aria-label="${esc(name)}">
    <defs>
      <linearGradient id="${grad}" gradientUnits="userSpaceOnUse" x1="10" y1="10" x2="90" y2="90"><stop offset="0" stop-color="#fff3c4"/><stop offset="1" stop-color="#c9a24a"/></linearGradient>
      <marker id="${arrow}" viewBox="0 0 10 10" refX="5" refY="5" markerWidth="4.2" markerHeight="4.2" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 Z" class="head"/></marker>
      <filter id="${glow}" filterUnits="userSpaceOnUse" x="0" y="0" width="100" height="100"><feGaussianBlur stdDeviation="1.6" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
    </defs>
    <circle cx="50" cy="50" r="48" class="disc"/>${body}</svg>${badge}</span>`;
}
/** Replay the spark of an icon made with { animate: true }. */
export function playIcon(root) {
  const anim = root?.querySelector?.('animateMotion');
  if (!anim) return;
  root.querySelector('.spell-icon')?.classList.add('playing');
  try { anim.beginElement(); } catch { /* static picture only */ }
}
