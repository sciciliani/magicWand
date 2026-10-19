// spells.js — the Spells tab: the spells on the wand, the ones it can still
// learn, your own spells, and the trainer (1, 2, 3… GO, three samples,
// sensitivity, a warning when a new spell looks like an existing one).
import { SPELLS, spellIcon, playIcon, spellOfName, displayName, onArt } from './spell-book.js?v=2';
import { Recorder } from './recorder.js?v=3';
import { t } from './i18n.js?v=4';

const $ = (s) => document.querySelector(s);
const esc = (s) => String(s ?? '').replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);
const NEEDED = 3;
const BEATS = ['Ready…', 'one {name}…', 'GO!'];  // the count before every attempt (texts as in the setup)
// Sensitivity 1..10 <-> the spell's acceptance distance on the wand (GTHR).
const toThr = (level) => +(0.3 + level * 0.12).toFixed(2);
const toLevel = (thr) => Math.max(1, Math.min(10, Math.round((thr - 0.3) / 0.12)));

/** app: { send, info(), connected(), log, perform | null, slotFor(wire), codeLabel(c), goto(tab), backup(reason), toast } */
export function initSpells(app) {
  const dlg = $('#spellDlg');
  const tr = { slot: -1, spell: null, wire: '', custom: false, count: 0, msg: '', warn: null, testing: false, done: false };
  const rec = new Recorder({ send: (l) => app.send(l), log: (x) => app.log(x), changed: () => renderDlg() });
  onArt(() => render());

  const gestures = () => app.info()?.g || [];

  // ---------------------------------------------------------------- tab
  function render() {
    const gs = gestures();
    const on = app.connected();
    const mine = gs.map((g, i) => ({ g, i })).filter(({ g }) => g.c > 0);
    const known = new Set(mine.map(({ g }) => spellOfName(g.n)?.id).filter(Boolean));
    $('#mySpells').innerHTML = mine.length ? mine.map(({ g, i }) => {
      const sp = spellOfName(g.n);
      const does = g.b >= 0 ? t('sends “{code}”', { code: esc(app.codeLabel(g.b)) }) : t('no action yet');
      return `<div class="spell-card" data-g="${i}">
        ${spellIcon(sp, { size: 72, initial: g.n })}
        <div class="spell-body"><b>${esc(displayName(g.n))}</b>
          <div class="dots" title="${t('{n} of 4 samples', { n: g.c })}">${[0, 1, 2, 3].map((k) => `<i class="${k < g.c ? 'on' : ''}"></i>`).join('')}</div>
          <div class="muted small">${does}</div></div>
        <div class="spell-actions">
          <button class="small primary" data-act="train" ${on ? '' : 'disabled'}>${t('Train')}</button>
          <button class="small" data-act="forget" ${on ? '' : 'disabled'}>${t('Forget')}</button>
        </div></div>`;
    }).join('') : `<p class="muted">${t('Your wand knows no spells yet. Learn one below.')}</p>`;

    const free = gs.filter((g) => !g.c).length;
    $('#moreSpells').innerHTML = SPELLS.filter((s) => !known.has(s.id)).map((s) => `
      <div class="spell-tile" data-id="${s.id}">
        ${spellIcon(s, { size: 64 })}
        <div><b>${esc(t(s.name))}</b><p class="muted small">${esc(t(s.how))}</p></div>
        <button class="small" data-act="learn" ${on && free ? '' : 'disabled'}>${t('Learn')}</button>
      </div>`).join('');
    $('#customHint').textContent = free ? t('{n} free spell slots on the wand.', { n: free }) : t('All 8 slots are used. Forget a spell first.');
    $('#customBtn').disabled = !on || !free;
  }

  $('#mySpells').addEventListener('click', (e) => {
    const b = e.target.closest('button');
    const card = e.target.closest('.spell-card');
    if (!b || !card) return;
    const g = +card.dataset.g;
    const name = displayName(gestures()[g]?.n);
    if (b.dataset.act === 'train') open({ slot: g });
    else if (b.dataset.act === 'forget' && confirm(t('Forget {name}? The wand will not recognise it any more.', { name }))) {
      app.send(`BIND ${g} -`);
      app.send(`GCLR ${g}`);
    }
  });
  $('#moreSpells').addEventListener('click', (e) => {
    const b = e.target.closest('button[data-act=learn]');
    if (b) open({ id: b.closest('.spell-tile').dataset.id });
  });
  $('#customBtn').addEventListener('click', () => {
    const name = $('#customName').value.trim().replace(/[^A-Za-z0-9-]+/g, '_').slice(0, 15);
    if (!name) return app.toast(t('Give your spell a name first'), true);
    if (gestures().some((g) => g.c && g.n.toLowerCase() === name.toLowerCase())) return app.toast(t('A spell with that name already exists'), true);
    $('#customName').value = '';
    open({ custom: name });
  });

  // ---------------------------------------------------------------- trainer
  /** open({ slot }) an existing spell, ({ id }) a catalog spell, ({ custom: name }) a new own spell. */
  function open({ slot = -1, id = null, custom = null } = {}) {
    if (!app.connected()) return;
    const gs = gestures();
    let spell = null, wire, isNew = false;
    if (slot >= 0) {
      wire = gs[slot].n;
      spell = spellOfName(wire);
    } else {
      spell = id ? SPELLS.find((s) => s.id === id) : null;
      wire = spell ? spell.wire : custom;
      slot = app.slotFor(wire);
      isNew = true;
    }
    if (slot < 0) return app.toast(t('All 8 slots are used. Forget a spell first.'), true);
    Object.assign(tr, { slot, spell, wire, custom: !spell, count: isNew ? 0 : gs[slot].c, warn: null, testing: false, done: false,
      msg: t('Hold the wand still and centred. Press Record, wait for GO, then do the spell once.') });
    if (isNew) app.send(`GCLR ${slot}`);
    renderDlg();
    if (!dlg.open) dlg.showModal();
    setTimeout(() => playIcon($('#spellDlg .sd-icon')), 300);
    app.practice(true);
  }

  function attempt() {
    tr.msg = '';
    tr.warn = null;
    rec.attempt({ slot: tr.slot, wire: tr.wire, move: tr.spell?.move || null }, (r) => {
      if (!dlg.open) return;
      if (r.kind === 'early') { tr.msg = t('Wait for GO! Hold the wand still and centred.'); return again(); }
      if (r.kind === 'nothing') { tr.msg = t('No move detected. Try again with a brisker motion.'); return renderDlg(); }
      if (r.kind === 'cancel') return renderDlg();
      tr.count = r.count;
      if (r.kind === 'bad') { tr.msg = t(r.verdict.say); return again(); }
      // A spell too close to another one: the wand may confuse them.
      if (r.near?.similar && tr.count <= 1) {
        tr.warn = { other: displayName(gestures()[r.near.g]?.n) };
        tr.msg = '';
        return renderDlg();
      }
      tr.msg = tr.count >= NEEDED ? t('Learned! Try it with Test, or adjust the sensitivity.') : t('Saved sample {n} of 3', { n: tr.count });
      if (tr.count >= NEEDED) { tr.done = true; app.backup('spell'); return renderDlg(); }
      again();
    });
    renderDlg();
  }
  const again = () => { renderDlg(); setTimeout(() => { if (dlg.open && !tr.warn && tr.count < NEEDED) attempt(); }, 900); };

  function renderDlg() {
    if (!tr.wire) return;
    const g = gestures()[tr.slot];
    const name = tr.spell ? t(tr.spell.name) : tr.wire.replace(/_/g, ' ');
    const p = rec.phase;
    const beats = BEATS.map((b, i) => {
      const n = i + 1, cls = p === 'go' && n === 3 ? 'now go' : p !== 'idle' && rec.beat === n ? 'now' : rec.beat > n ? 'past' : '';
      return `<li class="${cls}">${esc(t(b, { name }))}</li>`;
    }).join('');
    const level = toLevel(g?.t || 0.7);
    $('#spellDlgBody').innerHTML = `
      <div class="sd-head"><div class="sd-icon">${spellIcon(tr.spell, { size: 96, animate: true, initial: tr.wire })}</div>
        <div><h2>${esc(name)}</h2><p class="muted small">${esc(tr.spell ? t(tr.spell.how) : t('Your own spell: do it the same way every time.'))}</p>
        <div class="dots big">${[0, 1, 2].map((k) => `<i class="${k < tr.count ? 'on' : ''}"></i>`).join('')}</div></div></div>
      <div class="counter" data-phase="${p}"><ol>${beats}</ol></div>
      ${tr.warn ? `<div class="warn-card">⚠️ ${t('This looks a lot like {other}. Your wand may confuse them. Try a move that is more different, or keep it anyway.', { other: esc(tr.warn.other) })}
        <div class="row"><button class="small" data-act="restart">${t('Start over')}</button><button class="small primary" data-act="keep">${t('Keep it')}</button></div></div>` : ''}
      <p class="sd-msg">${esc(tr.msg)}</p>
      ${app.perform && tr.spell?.move ? `<div class="pretend"><span class="muted small">${t('Pretend wand:')}</span>
        <button class="small" data-pretend="1">${t('do it right')}</button><button class="small" data-pretend="wrong">${t('wrong move')}</button></div>` :
        app.perform ? `<div class="pretend"><span class="muted small">${t('Pretend wand:')}</span><button class="small" data-pretend="other">${t('do a move')}</button></div>` : ''}
      ${tr.count >= NEEDED ? `<label class="sens"><span>${t('Sensitivity')} <output>${level}</output></span>
        <input type="range" min="1" max="10" step="1" value="${level}" data-act="sens" />
        <span class="hint">${t('Higher = recognised more easily, but it may also catch other moves.')}</span></label>` : ''}
      ${tr.testing ? `<p class="test-msg">${esc(tr.testMsg || t('Cast the spell now…'))}</p>` : ''}
      <menu>
        <button type="button" data-act="close">${tr.count >= NEEDED ? t('Done') : t('Cancel')}</button>
        ${tr.count >= NEEDED ? `<button type="button" data-act="test">${t('Test')}</button><button type="button" data-act="more">${t('Record another')}</button>` :
          `<button type="button" class="primary" data-act="record" ${rec.busy || tr.warn ? 'disabled' : ''}>${rec.busy ? t('Recording…') : t('Record')}</button>`}
      </menu>`;
  }

  dlg.addEventListener('click', (e) => {
    const b = e.target.closest('button');
    if (!b) return;
    const act = b.dataset.act;
    if (b.dataset.pretend) {
      const mv = tr.spell?.move;
      const other = { zigzag: 'stroke', stroke: 'tipup', tipup: 'stroke', circle: 'zigzag', hit: 'zigzag' }[mv] || 'swish';
      return app.perform(b.dataset.pretend === 'wrong' ? other : mv || 'other', 1);
    }
    if (act === 'record') attempt();
    else if (act === 'more') { tr.done = false; tr.count = Math.min(tr.count, NEEDED - 1); attempt(); }
    else if (act === 'keep') { tr.warn = null; tr.msg = t('Saved sample {n} of 3', { n: tr.count }); again(); }
    else if (act === 'restart') { app.send(`GCLR ${tr.slot}`); tr.count = 0; tr.warn = null; tr.msg = t('Try a move that looks different.'); renderDlg(); }
    else if (act === 'test') { tr.testing = true; tr.testMsg = ''; renderDlg(); }
    else if (act === 'close') close();
  });
  dlg.addEventListener('change', (e) => {
    if (e.target.dataset.act === 'sens') app.send(`GTHR ${tr.slot} ${toThr(+e.target.value)}`);
  });
  dlg.addEventListener('input', (e) => {
    if (e.target.dataset.act === 'sens') e.target.parentElement.querySelector('output').textContent = e.target.value;
  });
  dlg.addEventListener('cancel', () => close());
  function close() {
    rec.cancel(true);
    tr.testing = false;
    tr.wire = '';
    app.practice(false);
    if (dlg.open) dlg.close();
    render();
  }

  return {
    render,
    open,
    onLine(tag, p) {
      rec.feed(tag, p);
      // Test: the wand reports what it recognised (practice mode: nothing is sent).
      if (tag === 'MATCH' && tr.testing && dlg.open) {
        const g = +p[0], ok = p[3] === '1';
        tr.testMsg = g === tr.slot && ok ? t('✓ Recognised! (score {score})', { score: (+p[1]).toFixed(2) })
          : g >= 0 ? t('✗ Not recognised. Closest: {name}. Try again, or raise the sensitivity.', { name: displayName(gestures()[g]?.n) })
          : t('✗ Not recognised. Try again.');
        renderDlg();
      }
      if (tag === 'INFO' && dlg.open && !rec.busy) renderDlg();
    },
    busy: () => dlg.open,
  };
}
