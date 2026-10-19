// devices.js — the Devices tab: your TVs, air conditioners, lights… and which
// spell presses which button.
//
// A device is kept in this browser (profile.devices) with its buttons. A
// button's code is either from the library (brand + button) or learned from
// your own remote (raw timings). The wand only stores the buttons that a spell
// casts: mapping a spell uploads that one code to a free IR slot and binds it.
// Test sends a code once without storing it (TRY).
import { LIBRARY, identify } from './ir-presets.js?v=13';
import { spellIcon, spellOfName, displayName } from './spell-book.js?v=2';
import { t } from './i18n.js?v=4';

const $ = (s) => document.querySelector(s);
const esc = (s) => String(s ?? '').replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);
const wire = (s) => String(s || '').replace(/[^A-Za-z0-9-]+/g, '_').slice(0, 15);
const uid = () => Math.random().toString(36).slice(2, 9);

// Device types: library key, icon, buttons offered by default.
export const TYPES = {
  TV: { icon: '📺', name: 'TV', keys: ['Power', 'Volume up', 'Volume down', 'Channel up', 'Channel down'] },
  'Air conditioner': { icon: '❄️', name: 'Air conditioner', keys: ['On · 24 °C cold', 'Off', 'On · 30 °C hot'] },
  'LED strip': { icon: '💡', name: 'LED lights', keys: ['On', 'Off', 'Brighter', 'Dimmer'] },
  'Other device': { icon: '🔌', name: 'Other', keys: [] },
};
// How to set the remote before learning an air conditioner button.
const AC_HOWTO = {
  'On · 24 °C cold': 'Set your remote to COOL, 24 °C, then press its power button near the wand.',
  'On · 30 °C hot': 'Set your remote to HEAT, 30 °C, then press its power button near the wand.',
  Off: 'Press the remote’s power button to turn the air conditioner off, near the wand.',
};
const SHORT = {
  Power: 'Pwr', 'Volume up': 'VolUp', 'Volume down': 'VolDn', 'Channel up': 'ChUp', 'Channel down': 'ChDn',
  'On · 24 °C cold': 'Cool24', 'On · 30 °C hot': 'Heat30', Off: 'Off', On: 'On', Brighter: 'Bright', Dimmer: 'Dim',
};
const LEARNED = 'learned';   // brand of a device whose buttons all come from its remote

/**
 * app: { send, info(), connected(), profile(), saveProfile(), toast(text, err),
 *        capture(name) -> Promise<{ ok, key, khz, d, why }>, freeSlot(), slotsInUse(), backup(reason) }
 */
export function initDevices(app) {
  const dlg = $('#devDlg');
  let wiz = null;   // add-device wizard: { step, type, brand, keys, detect }

  const devices = () => app.profile()?.devices || [];
  const brandOf = (d) => (d.brand === LEARNED ? null : LIBRARY[d.type]?.[d.brand]);
  const libActions = (type, brand) => (LIBRARY[type]?.[brand]?.actions || []);
  const libCode = (type, brand, k) => libActions(type, brand)[k]?.make?.();
  const codeOf = (d, key) => key.raw || (key.lib != null ? libCode(d.type, d.brand, key.lib) : null);
  const keyLabel = (key) => t(key.label, key.vars);

  // Default buttons of a new device: the library's button with the same label, else "learn it".
  function defaultKeys(type, brand) {
    const acts = brand && brand !== LEARNED ? libActions(type, brand) : [];
    const want = TYPES[type].keys;
    const keys = want.map((label) => {
      const k = acts.findIndex((a) => a.label === label && a.make);
      return { id: uid(), label, short: SHORT[label] || wire(label), lib: k >= 0 ? k : null, raw: null, slot: -1 };
    });
    // LED strips: their remotes differ, so use the brand's own first buttons.
    if (type === 'LED strip' && acts.length) return acts.slice(0, 4).map((a, k) => ({ id: uid(), label: a.label, short: a.short, lib: k, raw: null, slot: -1 }));
    return keys;
  }

  // ---------------------------------------------------------------- the wand's side
  const gestures = () => app.info()?.g || [];
  const spellOn = (slot) => (slot >= 0 ? gestures().findIndex((g) => g.c && g.b === slot) : -1);

  async function upload(d, key) {
    const code = codeOf(d, key);
    if (!code) return false;
    let s = key.slot;
    if (s < 0 || !app.info()?.c?.[s]?.k) s = app.freeSlot();
    if (s < 0) { app.toast(t('All 12 IR slots are used. Clear one in the IR codes tab'), true); return false; }
    app.send(`CODE ${s} ${code.khz} ${wire(`${d.short || 'Dev'}_${key.short}`)} ${code.d.join(',')}`);
    key.slot = s;
    return true;
  }

  // Free the wand's IR slot of a button nobody casts any more.
  function release(key) {
    if (key.slot >= 0 && spellOn(key.slot) < 0) { app.send(`CCLR ${key.slot}`); key.slot = -1; }
  }

  async function mapSpell(d, key, g) {
    const before = spellOn(key.slot);
    if (before >= 0 && before !== g) app.send(`BIND ${before} -`);
    if (g < 0) {
      if (key.slot >= 0) app.send(`CCLR ${key.slot}`);
      key.slot = -1;
    } else {
      // The spell may cast another button now: that one is released.
      const old = gestures()[g]?.b;
      if (!(await upload(d, key))) return;
      app.send(`BIND ${g} ${key.slot}`);
      for (const od of devices()) for (const ok of od.keys) if (ok !== key && ok.slot === old && old >= 0) { app.send(`CCLR ${old}`); ok.slot = -1; }
      app.toast(t('{spell} now casts {what} ✓', { spell: displayName(gestures()[g].n), what: `${d.name} · ${keyLabel(key)}` }));
    }
    app.saveProfile();
    app.send('HELLO');
    app.backup('device');
  }

  function test(d, key) {
    const code = codeOf(d, key);
    if (!code) return app.toast(t('Learn this button from your remote first'), true);
    app.send(`TRY ${code.khz} ${code.d.join(',')}`);
  }

  async function learnKey(d, key, where) {
    const howto = AC_HOWTO[key.label] && d.type === 'Air conditioner' ? t(AC_HOWTO[key.label]) : t('Aim your remote at the wand tip and press “{key}” when it buzzes.', { key: keyLabel(key) });
    where(`⏳ ${howto}`);
    const r = await app.capture(wire(`${d.short || 'Dev'}_${key.short}`));
    if (!r.ok) return where(`✗ ${r.why}`), false;
    key.raw = { khz: r.khz, d: r.d };
    key.lib = null;
    where(t('✓ Learned. Press Test to try it.'));
    return true;
  }

  // ---------------------------------------------------------------- tab
  function spellOptions(slot) {
    const cur = spellOn(slot);
    const gs = gestures();
    return `<option value="-1">${t('— no spell —')}</option>` + gs.map((g, i) => (g.c ? `<option value="${i}" ${i === cur ? 'selected' : ''}>${esc(displayName(g.n))}${g.b >= 0 && i !== cur ? ' · ' + t('(in use)') : ''}</option>` : '')).join('');
  }

  function render() {
    const on = app.connected();
    const list = devices();
    $('#deviceList').innerHTML = list.length ? list.map((d) => {
      const ty = TYPES[d.type] || TYPES['Other device'];
      const extra = brandOf(d) ? libActions(d.type, d.brand).map((a, k) => ({ a, k })).filter(({ a, k }) => a.make && !d.keys.some((x) => x.lib === k)) : [];
      return `<div class="card device" data-dev="${d.id}">
        <div class="device-head"><span class="dicon">${ty.icon}</span>
          <div><input class="dev-name" value="${esc(d.name)}" data-act="rename" maxlength="30" />
          <div class="muted small">${d.brand === LEARNED ? t('Learned from your remote') : esc(t(d.brand))}${d.model ? ' · ' + esc(d.model) : ''}</div></div>
          <button class="small linkish" data-act="remove">${t('Remove')}</button></div>
        <div class="keys">${d.keys.map((key) => {
          const g = spellOn(key.slot), sp = g >= 0 ? spellOfName(gestures()[g].n) : null;
          const known = !!codeOf(d, key);
          return `<div class="key-row" data-key="${key.id}">
            <button class="small" data-act="test" ${on && known ? '' : 'disabled'} title="${t('Point the wand at the device and press Test. Did it react?')}">▶ ${t('Test')}</button>
            <span class="key-label">${esc(keyLabel(key))}${known ? '' : ` <span class="muted small">· ${t('not learned yet')}</span>`}</span>
            ${g >= 0 ? spellIcon(sp, { size: 28, initial: gestures()[g].n }) : '<span class="icon-gap"></span>'}
            <select data-act="spell" ${on && known ? '' : 'disabled'} aria-label="${t('Cast with')}">${spellOptions(key.slot)}</select>
            <button class="small" data-act="learn" ${on && app.info()?.irrx ? '' : 'disabled'}>${known ? t('Re-learn') : t('Learn')}</button>
            <button class="small linkish" data-act="drop" title="${t('Remove')}">✕</button>
            <div class="key-state muted small"></div>
          </div>`;
        }).join('')}</div>
        <div class="row add-key">
          ${extra.length ? `<select data-act="addlib"><option value="">${t('+ Add a button from the library…')}</option>${extra.map(({ a, k }) => `<option value="${k}">${esc(t(a.label, a.vars))}</option>`).join('')}</select>` : ''}
          <button class="small" data-act="addlearn" ${on && app.info()?.irrx ? '' : 'disabled'}>${t('+ Learn another button (e.g. Netflix)')}</button>
        </div></div>`;
    }).join('') : `<div class="card empty"><p class="muted">${t('No devices yet. Add your TV, air conditioner or lights, then choose which spell controls each button.')}</p></div>`;
    $('#addDeviceBtn').disabled = !on;
  }

  const findDev = (el) => devices().find((d) => d.id === el.closest('.device')?.dataset.dev);
  const findKey = (d, el) => d?.keys.find((k) => k.id === el.closest('.key-row')?.dataset.key);

  $('#deviceList').addEventListener('click', async (e) => {
    const b = e.target.closest('button');
    if (!b) return;
    const d = findDev(b);
    if (!d) return;
    const key = findKey(d, b);
    const state = (txt) => { const el = b.closest('.key-row')?.querySelector('.key-state'); if (el) el.textContent = txt; };
    const act = b.dataset.act;
    if (act === 'test') test(d, key);
    else if (act === 'learn') { if (await learnKey(d, key, state)) { if (key.slot >= 0) await upload(d, key); app.saveProfile(); render(); } }
    else if (act === 'drop') {
      if (!confirm(t('Remove {key}?', { key: keyLabel(key) }))) return;
      if (spellOn(key.slot) >= 0) app.send(`BIND ${spellOn(key.slot)} -`);
      if (key.slot >= 0) app.send(`CCLR ${key.slot}`);
      d.keys = d.keys.filter((k) => k !== key);
      app.saveProfile(); render();
    } else if (act === 'remove') {
      if (!confirm(t('Remove {name} and its buttons?', { name: d.name }))) return;
      for (const k of d.keys) { if (spellOn(k.slot) >= 0) app.send(`BIND ${spellOn(k.slot)} -`); if (k.slot >= 0) app.send(`CCLR ${k.slot}`); }
      const p = app.profile();
      p.devices = p.devices.filter((x) => x !== d);
      app.saveProfile(); render();
    } else if (act === 'addlearn') {
      const label = prompt(t('Name of the button (e.g. Netflix):'));
      if (!label) return;
      const nk = { id: uid(), label: label.trim().slice(0, 24), short: wire(label).slice(0, 8), lib: null, raw: null, slot: -1 };
      d.keys.push(nk);
      render();
      const row = document.querySelector(`.key-row[data-key="${nk.id}"] .key-state`);
      await learnKey(d, nk, (txt) => { if (row) row.textContent = txt; });
      app.saveProfile(); render();
    }
  });
  $('#deviceList').addEventListener('change', (e) => {
    const d = findDev(e.target);
    if (!d) return;
    const act = e.target.dataset.act;
    if (act === 'spell') mapSpell(d, findKey(d, e.target), +e.target.value);
    else if (act === 'rename') { d.name = e.target.value.trim() || d.name; app.saveProfile(); }
    else if (act === 'addlib' && e.target.value !== '') {
      const k = +e.target.value, a = libActions(d.type, d.brand)[k];
      d.keys.push({ id: uid(), label: a.label, vars: a.vars, short: a.short, lib: k, raw: null, slot: -1 });
      app.saveProfile(); render();
    }
  });
  $('#addDeviceBtn').addEventListener('click', () => openWizard());

  // ---------------------------------------------------------------- add-device wizard
  function openWizard() {
    if (!app.connected()) return;
    wiz = { step: 'type', type: null, brand: null, keys: [], msg: '' };
    renderWiz();
    if (!dlg.open) dlg.showModal();
  }

  function renderWiz() {
    const w = wiz;
    let html = '';
    if (w.step === 'type') {
      html = `<h2>${t('What do you want to control?')}</h2><div class="choice-grid">${Object.entries(TYPES).map(([k, ty]) =>
        `<button class="choice" data-type="${esc(k)}"><span class="dicon">${ty.icon}</span>${esc(t(ty.name))}</button>`).join('')}</div>`;
    } else if (w.step === 'brand') {
      const brands = Object.entries(LIBRARY[w.type] || {}).filter(([, b]) => b.actions.some((a) => a.make));
      html = `<h2>${t('Which brand?')}</h2>
        <div class="brand-list">${brands.map(([name, b]) => `<button class="choice small-choice" data-brand="${esc(name)}"><b>${esc(t(name))}</b><span class="muted small">${esc(t(b.note || ''))}</span></button>`).join('')}</div>
        <div class="row">
          ${app.info()?.irrx ? `<button class="primary" data-act="detect">🔍 ${t('Detect it with my remote')}</button>` : ''}
          <button data-brand="${LEARNED}">${t('My brand isn’t here: learn the buttons')}</button></div>`;
    } else if (w.step === 'detect') {
      html = `<h2>${t('Detect your {device}', { device: t(TYPES[w.type].name) })}</h2>
        <p>${t('Point your remote at the wand’s tip (about 10 cm away). Press Start, wait for the buzz, then press the remote’s power button.')}</p>
        <p class="wiz-msg">${esc(w.msg)}</p>
        <menu><button data-act="back">${t('Back')}</button><button class="primary" data-act="capture" ${w.busy ? 'disabled' : ''}>${w.busy ? t('Listening…') : t('Start')}</button></menu>`;
    } else if (w.step === 'keys') {
      html = `<h2>${esc(w.name)}</h2>
        <p class="muted small">${w.brand === LEARNED ? t('Learn each button from your remote, then test it with the wand.') : t('Point the wand at the device and press Test. Did it react?')}</p>
        <div class="keys">${w.keys.map((k) => {
          const known = !!(k.raw || k.lib != null);
          return `<div class="key-row" data-key="${k.id}">
            <button class="small" data-act="wtest" ${known ? '' : 'disabled'}>▶ ${t('Test')}</button>
            <span class="key-label">${esc(keyLabel(k))}${known ? ' ✓' : ` <span class="muted small">· ${t('not learned yet')}</span>`}</span>
            <button class="small" data-act="wlearn" ${app.info()?.irrx ? '' : 'disabled'}>${known ? t('Re-learn') : t('Learn')}</button>
            <div class="key-state muted small"></div></div>`;
        }).join('')}</div>
        <p class="hint">${t('You can choose the spell for each button after saving.')}</p>
        <menu><button data-act="back">${t('Back')}</button><button class="primary" data-act="save">${t('Save device')}</button></menu>`;
    }
    $('#devDlgBody').innerHTML = html + (w.step === 'type' || w.step === 'brand' ? `<menu><button data-act="${w.step === 'type' ? 'cancel' : 'back'}">${w.step === 'type' ? t('Cancel') : t('Back')}</button></menu>` : '');
  }

  function chooseBrand(brand, extra = {}) {
    const ty = TYPES[wiz.type];
    wiz.brand = brand;
    wiz.short = brand === LEARNED ? wire(t(ty.name)).slice(0, 5) : (LIBRARY[wiz.type][brand].short || wire(brand).slice(0, 5));
    wiz.name = brand === LEARNED ? t(ty.name) : `${t(brand)} ${t(ty.name)}`;
    wiz.keys = defaultKeys(wiz.type, brand);
    if (extra.learnedPower) {
      const k = wiz.keys.find((x) => /^(Power|On · 24)/.test(x.label)) || wiz.keys[0];
      if (k) { k.raw = extra.learnedPower; k.lib = null; }
    }
    wiz.step = 'keys';
    renderWiz();
  }

  dlg.addEventListener('click', async (e) => {
    const b = e.target.closest('button');
    if (!b || !wiz) return;
    if (b.dataset.type) {
      wiz.type = b.dataset.type;
      if (wiz.type === 'Other device') return chooseBrand(LEARNED);
      wiz.step = 'brand';
      return renderWiz();
    }
    if (b.dataset.brand) return chooseBrand(b.dataset.brand);
    const act = b.dataset.act;
    const fakeDev = { type: wiz.type, brand: wiz.brand, short: wiz.short, name: wiz.name };
    const key = wiz.keys.find((k) => k.id === b.closest('.key-row')?.dataset.key);
    const state = (txt) => { const el = b.closest('.key-row')?.querySelector('.key-state'); if (el) el.textContent = txt; };
    if (act === 'cancel') { dlg.close(); wiz = null; }
    else if (act === 'back') { wiz.step = wiz.step === 'keys' && wiz.type !== 'Other device' ? 'brand' : 'type'; renderWiz(); }
    else if (act === 'detect') { wiz.step = 'detect'; wiz.msg = ''; renderWiz(); }
    else if (act === 'capture') {
      wiz.busy = true; wiz.msg = t('Get ready… press the power button when the wand buzzes.'); renderWiz();
      const r = await app.capture('detect');
      wiz.busy = false;
      if (!r.ok) { wiz.msg = `✗ ${r.why}`; return renderWiz(); }
      const hits = identify(r.key, wiz.type);
      if (hits.length) {
        const h = hits[0];
        app.toast(t('It’s a {brand} {device}! ✓', { brand: t(h.brand), device: t(TYPES[wiz.type].name) }));
        return chooseBrand(h.brand);
      }
      app.toast(t('I don’t know this remote yet, so I’ll learn each button from it.'));
      chooseBrand(LEARNED, { learnedPower: { khz: r.khz, d: r.d } });
    }
    else if (act === 'wtest') test(fakeDev, key);
    else if (act === 'wlearn') { b.disabled = true; await learnKey(fakeDev, key, state); renderWiz(); }
    else if (act === 'save') {
      const p = app.profile();
      p.devices = p.devices || [];
      p.devices.push({ id: uid(), type: wiz.type, brand: wiz.brand, short: wiz.short, name: wiz.name, keys: wiz.keys });
      app.saveProfile();
      dlg.close();
      wiz = null;
      render();
      app.toast(t('Saved. Now choose a spell for each button.'));
    }
  });

  return { render, openWizard, busy: () => dlg.open };
}
