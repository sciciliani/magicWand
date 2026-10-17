// knight.js — the Knight tab: a spell battle against the knight figure.
//
// The wand gets two codes of its own, made up at random: HIT (the knight falls
// apart) and HEAL (it stands up again). A new or factory-reset knight learns
// the first code it receives as "hit" and the second as "heal", so pairing is:
// point the wand at the knight and press Pair (the wand sends HIT, then HEAL).
//
// Every knight paired with this wand learns the same two codes, so a spell
// hits whichever knight you point at. More knights = pair each one.
import { battleCodes, codeKey, LIBRARY } from './ir-presets.js?v=12';
import { spellIcon, spellOfName, displayName } from './spell-book.js?v=1';
import { t } from './i18n.js?v=3';

const $ = (s) => document.querySelector(s);
const esc = (s) => String(s ?? '').replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);
const hex = (n, w) => n.toString(16).toUpperCase().padStart(w, '0');
const PAIR_GAP_MS = 1500;   // between HIT and HEAL while pairing

/** app: { send, info(), connected(), profile(), saveProfile(), toast, freeSlot(), backup(reason) } */
export function initKnight(app) {
  const k = () => app.profile()?.knight || null;
  const gestures = () => app.info()?.g || [];
  const codeName = { hit: 'Knight_hit', heal: 'Knight_heal' };
  let pairing = null;   // { step: 'ready'|'sent'|'name', msg }

  // Codes the new pair must not collide with: the library and every device button.
  function takenKeys() {
    const s = new Set();
    for (const brands of Object.values(LIBRARY)) for (const b of Object.values(brands)) for (const a of b.actions) if (a.make) s.add(codeKey(a.make().d));
    for (const d of app.profile()?.devices || []) for (const key of d.keys) if (key.raw) s.add(codeKey(key.raw.d));
    return s;
  }

  // Both codes on the wand (slots named Knight_hit / Knight_heal).
  function slotOf(which) {
    const cs = app.info()?.c || [];
    return cs.findIndex((c) => c.k && c.n === codeName[which]);
  }
  function ensureOnWand() {
    const kn = k();
    if (!kn) return false;
    for (const which of ['hit', 'heal']) {
      if (slotOf(which) >= 0) continue;
      const s = app.freeSlot(which === 'heal' ? 1 : 0);
      if (s < 0) { app.toast(t('All 12 IR slots are used. Clear one in the IR codes tab'), true); return false; }
      app.send(`CODE ${s} ${kn[which].khz} ${codeName[which]} ${kn[which].d.join(',')}`);
    }
    return true;
  }

  function create() {
    const bc = battleCodes(takenKeys());
    const p = app.profile();
    p.knight = { addr: bc.addr, hit: { cmd: bc.hit.cmd, khz: bc.hit.khz, d: bc.hit.d }, heal: { cmd: bc.heal.cmd, khz: bc.heal.khz, d: bc.heal.d }, knights: [], autoBind: true };
    app.saveProfile();
    ensureOnWand();
    app.send('HELLO');
    app.backup('knight');
    render();
  }

  function bind(which, g) {
    const s = slotOf(which);
    if (s < 0) return ensureOnWand();
    // Unbind whatever spell cast this code before.
    gestures().forEach((x, i) => { if (x.b === s && i !== g) app.send(`BIND ${i} -`); });
    if (g >= 0) app.send(`BIND ${g} ${s}`);
    app.backup('knight');
  }

  function render() {
    const el = $('#knightArea');
    const on = app.connected();
    const kn = k();
    if (!kn) {
      el.innerHTML = `<div class="card knight-intro">
        <h2>${t('Spell battle against a Knight')}</h2>
        <p class="muted">${t('Your wand gets two secret codes of its own: one spell makes the knight fall apart, another one puts it back together.')}</p>
        <button class="primary" data-act="create" ${on ? '' : 'disabled'}>${t('Create my battle codes')}</button></div>`;
      return;
    }
    const gs = gestures();
    const hitSlot = slotOf('hit'), healSlot = slotOf('heal');
    const spellFor = (slot) => (slot >= 0 ? gs.findIndex((g) => g.c && g.b === slot) : -1);
    const opts = (slot) => `<option value="-1">${t('— no spell —')}</option>` + gs.map((g, i) => (g.c ? `<option value="${i}" ${i === spellFor(slot) ? 'selected' : ''}>${esc(displayName(g.n))}</option>` : '')).join('');
    const row = (which, label, slot) => {
      const g = spellFor(slot);
      return `<div class="battle-row">${g >= 0 ? spellIcon(spellOfName(gs[g].n), { size: 44, initial: gs[g].n }) : '<span class="icon-gap big"></span>'}
        <div><b>${label}</b><div class="muted small">NEC ${hex(kn.addr, 4)} · ${hex(kn[which].cmd, 2)}${slot < 0 ? ' · ' + t('not on the wand') : ''}</div></div>
        <select data-bind="${which}" ${on && slot >= 0 ? '' : 'disabled'} aria-label="${t('Cast with')}">${opts(slot)}</select>
        <button class="small" data-test="${which}" ${on && slot >= 0 ? '' : 'disabled'}>▶ ${t('Test')}</button></div>`;
    };
    el.innerHTML = `
      <div class="card"><h2>${t('Your battle spells')}</h2>
        ${row('hit', `⚔️ ${t('Hit')}`, hitSlot)}
        ${row('heal', `✨ ${t('Heal')}`, healSlot)}
        ${hitSlot < 0 || healSlot < 0 ? `<button class="small" data-act="restore" ${on ? '' : 'disabled'}>${t('Put the codes back on the wand')}</button>` : ''}
        <p class="hint">${t('Point the wand at a knight before you cast: only the knight in front of the tip reacts.')}</p></div>
      <div class="card"><h2>${t('Your knights')}</h2>
        ${kn.knights.length ? `<ul class="knights">${kn.knights.map((x, i) => `<li>🛡️ <b>${esc(x.name)}</b> <span class="muted small">${t('paired {date}', { date: new Date(x.ts).toLocaleDateString() })}</span>
          <button class="small linkish" data-forget="${i}">${t('Remove')}</button></li>`).join('')}</ul>` : `<p class="muted">${t('No knight paired yet.')}</p>`}
        ${pairing ? pairingHtml() : `<button class="primary" data-act="pair" ${on && hitSlot >= 0 && healSlot >= 0 ? '' : 'disabled'}>${kn.knights.length ? t('Pair another knight') : t('Pair a knight')}</button>`}
      </div>`;
  }

  function pairingHtml() {
    if (pairing.step === 'ready') return `<ol class="steps">
        <li>${t('Switch the knight on. A new knight (or one after a factory reset) is ready to learn.')}</li>
        <li>${t('Point the wand at the knight, about one step away.')}</li>
        <li>${t('Press Pair: the wand sends HIT, then HEAL.')}</li></ol>
      <menu><button data-act="pair-cancel">${t('Cancel')}</button><button class="primary" data-act="pair-send">${t('Pair')}</button></menu>`;
    if (pairing.step === 'sent') return `<p>${esc(pairing.msg)}</p>`;
    return `<p>${t('Did the knight confirm (it moved or blinked twice)?')}</p>
      <label>${t('Name this knight')} <input id="knightName" maxlength="20" value="${esc(t('Knight {n}', { n: k().knights.length + 1 }))}" /></label>
      <menu><button data-act="pair-send">${t('Send again')}</button><button class="primary" data-act="pair-done">${t('Yes, save it')}</button></menu>`;
  }

  $('#knightArea').addEventListener('click', (e) => {
    const b = e.target.closest('button');
    if (!b) return;
    if (b.dataset.test) { const s = slotOf(b.dataset.test); if (s >= 0) app.send(`SEND ${s}`); return; }
    if (b.dataset.forget != null) {
      const kn = k();
      if (!confirm(t('Remove {name} from the list? (The knight keeps its codes until it is reset.)', { name: kn.knights[+b.dataset.forget].name }))) return;
      kn.knights.splice(+b.dataset.forget, 1);
      app.saveProfile(); return render();
    }
    const act = b.dataset.act;
    if (act === 'create') create();
    else if (act === 'restore') { ensureOnWand(); app.send('HELLO'); }
    else if (act === 'pair') { pairing = { step: 'ready' }; render(); }
    else if (act === 'pair-cancel') { pairing = null; render(); }
    else if (act === 'pair-send') {
      pairing = { step: 'sent', msg: t('Sending HIT…') };
      render();
      app.send(`SEND ${slotOf('hit')}`);
      setTimeout(() => {
        pairing.msg = t('Sending HEAL…');
        render();
        app.send(`SEND ${slotOf('heal')}`);
        setTimeout(() => { pairing = { step: 'name' }; render(); }, 800);
      }, PAIR_GAP_MS);
    } else if (act === 'pair-done') {
      const name = ($('#knightName')?.value || '').trim() || t('Knight {n}', { n: k().knights.length + 1 });
      k().knights.push({ name, ts: Date.now() });
      pairing = null;
      app.saveProfile();
      app.backup('knight');
      app.toast(t('{name} is ready for battle ⚔️', { name }));
      render();
    }
  });
  $('#knightArea').addEventListener('change', (e) => {
    const which = e.target.dataset.bind;
    if (which) bind(which, +e.target.value);
  });

  return {
    render,
    start() { if (!k()) create(); else render(); },
    onInfo() {
      // First time: Stroke strikes, Tip up heals (if the wand knows them and they are free).
      const kn = k();
      if (kn?.autoBind && slotOf('hit') >= 0 && slotOf('heal') >= 0) {
        kn.autoBind = false;
        app.saveProfile();
        const gs = gestures();
        const pick = (wire) => gs.findIndex((g) => g.c && g.b < 0 && spellOfName(g.n)?.wire === wire);
        const h = pick('Stroke'), r = pick('TipUp');
        if (h >= 0) app.send(`BIND ${h} ${slotOf('hit')}`);
        if (r >= 0) app.send(`BIND ${r} ${slotOf('heal')}`);
      }
      render();
    },
  };
}
