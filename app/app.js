// app.js — Wand Workshop: configure the wand over Web Bluetooth.
// Protocol reference: docs/PROTOCOL.md
//
// Tabs: Home (live motion, last spell), Spells, Devices, Knight, Wizard (the
// first-time setup), Settings (backups), IR codes (raw slots), Log.
import { BleWand, MockWand, bluetoothAvailable, knownWands } from './wand-link.js?v=13';
import { LIBRARY, learnedKey } from './ir-presets.js?v=13';
import { initTraining } from './training.js?v=6';
import { initSpells } from './spells.js?v=3';
import { initDevices } from './devices.js?v=2';
import { initKnight } from './knight.js?v=2';
import { spellIcon, spellOfName, displayName } from './spell-book.js?v=2';
import { loadProfile, saveProfile, wandId, wandIsEmpty, factoryNamed, listBackups, addBackup, lastWand } from './profile.js?v=2';
import { t, applyStatic, initPickers, onLangChange } from './i18n.js?v=4';

const $ = (s) => document.querySelector(s);
const esc = (s) => String(s ?? '').replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);
const toWire = (s) => (s || '').trim().replace(/[^A-Za-z0-9-]+/g, '_').slice(0, 15) || '-';
const fromWire = (s) => (s || '').replace(/_/g, ' ');
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const state = {
  link: null,
  info: null,
  id: null,          // wand ID (chip ID) -> profile, backups
  profile: null,
  checked: false,    // first INFO of this connection handled (setup / restore)
  recording: -1,
  learning: -1,
  captured: {},      // slot -> '0xF2A0D5 x3 · 56 kHz' (codes learned this session)
  lib: null,         // open library dialog: { slot, spell, learnSlot }
  lastDevice: null,  // BLE device to reconnect to
  feats: [],         // [right, up, twist, thrust]
  backupBusy: false,
};

// ------------------------------------------------------------------ waiting for replies
// expect(test, ms): resolves with the first line for which test(line) is true.
const waiters = [];
function expect(test, ms = 4000) {
  return new Promise((resolve) => {
    const w = { test, resolve, timer: setTimeout(() => { waiters.splice(waiters.indexOf(w), 1); resolve(null); }, ms) };
    waiters.push(w);
  });
}
function feedWaiters(line) {
  for (const w of [...waiters]) if (w.test(line)) { clearTimeout(w.timer); waiters.splice(waiters.indexOf(w), 1); w.resolve(line); }
}

// ------------------------------------------------------------------ helpers shared by the tabs
const gestures = () => state.info?.g || [];
const codes = () => state.info?.c || [];
/** Spell slot for a name: the one already called that, else an empty one. */
function slotFor(wire) {
  const gs = gestures(), n = String(wire || '').toLowerCase();
  let s = gs.findIndex((g) => g.n.toLowerCase() === n);
  if (s < 0) s = gs.findIndex((g) => !g.c && !g.n);
  if (s < 0) s = gs.findIndex((g) => !g.c);
  return s;
}
/** The skip-th free IR slot (codes sent in a row don't see the INFO update in between). */
function freeSlot(skip = 0) {
  const free = codes().map((c, i) => (c.k ? -1 : i)).filter((i) => i >= 0);
  return free[skip] ?? -1;
}
const codeLabel = (c) => {
  const n = codes()[c]?.n;
  if (!n) return t('Code {n}', { n: c + 1 });
  const p = state.profile;
  for (const d of p?.devices || []) for (const k of d.keys) if (k.slot === c) return `${d.name} · ${t(k.label, k.vars)}`;
  if (n === 'Knight_hit') return `⚔️ ${t('Hit the knight')}`;
  if (n === 'Knight_heal') return `✨ ${t('Heal the knight')}`;
  return fromWire(n);
};

/** Learn one button from a remote into a scratch IR slot, read it back, free the slot. */
async function capture(name) {
  if (!state.link) return { ok: false, why: t('Connect the wand first') };
  const free = codes().map((c, i) => (c.k ? -1 : i)).filter((i) => i >= 0);
  const s = free[free.length - 1];
  if (s == null) return { ok: false, why: t('All 12 IR slots are used. Clear one in the IR codes tab') };
  send(`LEARN ${s} ${toWire(name)}`);
  const res = await expect((l) => new RegExp(`^LEARN ${s} (ok|timeout|unsupported)`).test(l), 25000);
  const p = (res || '').split(' ');
  if (p[2] !== 'ok') return { ok: false, why: p[2] === 'unsupported' ? t("Received something, but couldn't decode it. Try again, closer") : t('Nothing received. Try closer, within 10 cm') };
  send(`DUMPC ${s}`);
  const dump = await expect((l) => l.startsWith(`CODE ${s} `), 5000);
  send(`CCLR ${s}`);
  if (!dump) return { ok: false, why: t('The wand did not send the code back. Try again.') };
  const [, , khz, , list] = dump.split(' ');
  return { ok: true, key: learnedKey(p[3], p[4]), khz: +khz, d: list.split(',').map(Number) };
}

function profile() { return state.profile; }
function storeProfile() { if (state.profile) saveProfile(state.profile); }

// ------------------------------------------------------------------ backups
// A full copy of the wand: spells (DUMPG), IR codes (DUMPC), bindings, settings
// and this browser's profile. Requests close together are merged.
let backupTimer = 0;
function backup(reason) {
  clearTimeout(backupTimer);
  backupTimer = setTimeout(() => takeBackup(reason), 2500);
}
async function takeBackup(reason) {
  if (!state.link || !state.info || state.backupBusy) return;
  state.backupBusy = true;
  try {
    send('HELLO');
    await expect((l) => l.startsWith('INFO '), 4000);
    const info = state.info;
    const out = { reason, name: info.name, fw: info.fw, settings: { thr: info.thr, sleep: info.sleep, wake: info.wake, haptics: info.haptics },
      binds: gestures().map((g) => g.b), gestures: [], codes: [],
      profile: { owner: state.profile.owner, devices: state.profile.devices, knight: state.profile.knight } };
    for (const [i, g] of gestures().entries()) {
      if (!g.c) continue;
      send(`DUMPG ${i}`);
      const l = await expect((x) => x.startsWith(`GDATA ${i} `), 4000);
      if (!l) throw new Error('DUMPG');
      out.gestures.push({ slot: i, hex: l.split(' ')[2] });
    }
    for (const [i, c] of codes().entries()) {
      if (!c.k) continue;
      send(`DUMPC ${i}`);
      const l = await expect((x) => x.startsWith(`CODE ${i} `), 4000);
      if (!l) throw new Error('DUMPC');
      const [, , khz, name, list] = l.split(' ');
      out.codes.push({ slot: i, khz: +khz, name, d: list.split(',').map(Number) });
    }
    if (!out.gestures.length && !out.codes.length) return;
    addBackup(state.id, out);
    renderBackups();
    logLine(`backup saved (${reason}): ${out.gestures.length} spells, ${out.codes.length} IR codes`, 'in');
  } catch (e) {
    logLine(`backup failed: ${e.message} (the wand firmware may be older than 0.5.0)`, 'err');
  } finally {
    state.backupBusy = false;
  }
}

async function restore(b) {
  $('#restoreDlg').open && $('#restoreDlg').close();
  toast(t('Restoring your wand…'));
  const ok = async (line, prefix) => { send(line); return !!(await expect((l) => l.startsWith(prefix) || l.startsWith('ERR'), 4000)); };
  for (const g of b.gestures) await ok(`GLOAD ${g.slot} ${g.hex}`, `OK GLOAD ${g.slot}`);
  for (const c of b.codes) await ok(`CODE ${c.slot} ${c.khz} ${c.name} ${c.d.join(',')}`, `OK CODE ${c.slot}`);
  for (const [g, c] of b.binds.entries()) if (c >= 0) await ok(`BIND ${g} ${c}`, `OK BIND ${g}`);
  for (const [k, v] of Object.entries(b.settings)) await ok(`SET ${k} ${v}`, `OK SET ${k}`);
  Object.assign(state.profile, b.profile, { setupDone: true, setupStep: 'done' });
  storeProfile();
  send('HELLO');
  toast(t('Your wand is back as it was ✓'));
  const name = (b.name || '').replace(/^Wand-/, '');
  if (name && !factoryNamed({ name: b.name, uid: state.info?.uid })) { await sleep(800); send(`SET name ${name}`); }
}

function offerRestore() {
  const list = listBackups(state.id);
  $('#restoreText').textContent = t('This wand was reset to factory settings. Do you want to restore your settings from this browser?');
  $('#restoreList').innerHTML = backupItems(list);
  $('#restoreDlg').showModal();
}
$('#restoreList').addEventListener('click', (e) => {
  const b = e.target.closest('button[data-restore]');
  if (b) restore(listBackups(state.id)[+b.dataset.restore]);
});
$('#restoreNo').addEventListener('click', () => {
  $('#restoreDlg').close();
  state.profile.setupDone = false;
  state.profile.setupStep = 'names';
  storeProfile();
  startSetup();
});
const backupItems = (list) => list.map((b, i) => `<li><span>🗂️ <b>${new Date(b.ts).toLocaleString()}</b>
  <span class="muted small">${t('{s} spells · {c} IR codes · {d} devices', { s: b.gestures.length, c: b.codes.length, d: b.profile?.devices?.length || 0 })}</span></span>
  <button class="small" data-restore="${i}">${t('Restore')}</button></li>`).join('');
function renderBackups() {
  const list = state.id ? listBackups(state.id) : [];
  $('#backupList').innerHTML = list.length ? backupItems(list) : `<li class="muted">${t('No backups yet.')}</li>`;
}
$('#backupList').addEventListener('click', (e) => {
  const b = e.target.closest('button[data-restore]');
  if (b && confirm(t('Replace what is on the wand now with this copy?'))) restore(listBackups(state.id)[+b.dataset.restore]);
});
$('#backupBtn').addEventListener('click', () => takeBackup('manual'));
$('#setupBtn').addEventListener('click', () => { state.profile.setupStep = 'intro'; storeProfile(); startSetup(); });

// ------------------------------------------------------------------ tabs' modules
const demo = new URLSearchParams(location.search).has('demo');
const perform = demo ? (move, speed) => state.link?.perform?.(move, speed) : null;
const shared = {
  send: (l) => send(l), connected: () => !!state.link, info: () => state.info, log: (x) => logLine(x, 'in'), perform,
  profile, saveProfile: storeProfile, slotFor, freeSlot, codeLabel, capture, backup, toast: (x, e) => toast(x, e),
  practice: (on) => send(`PRACTICE ${on ? 1 : 0}`),
};
const training = initTraining({ ...shared, renameWand: (n) => send(`SET name ${n}`), goto: (tab, what) => goto(tab, what) });
const spells = initSpells({ ...shared, goto: (tab) => showTab(tab) });
const devices = initDevices(shared);
const knight = initKnight(shared);

function goto(tab, what) {
  showTab(tab);
  if (tab === 'devices' && what === 'add') devices.openWizard();
  if (tab === 'knight' && what === 'start') knight.start();
}
function startSetup() { showTab('training'); training.begin(); }

// ------------------------------------------------------------------ connection
// No known wand: the Summon window. A known wand: reconnect by itself, with a
// small bar ("Connect to a new wand"), and keep trying while it sleeps.
const connectDlg = $('#connectDlg');
connectDlg.addEventListener('cancel', (e) => e.preventDefault());  // Esc can't dismiss it
const demoKnown = () => { try { return localStorage.getItem('demoWandKnown') ? [{ name: 'Wand (demo)', id: 'demo', demo: true }] : []; } catch { return []; } };
const knownList = async () => (demo ? demoKnown() : await knownWands());

async function showConnect(msg = '') {
  document.body.classList.add('offline');
  $('#reconnectBar').hidden = true;
  $('#connectMsg').textContent = msg;
  $('#noBle').hidden = bluetoothAvailable() || demo;
  $('#findBtn').hidden = !bluetoothAvailable() && !demo;
  $('#demoLink').hidden = demo;
  $('#findBtn').textContent = demo ? t('Summon the demo wand') : t('Search for wands…');
  $('#knownWands').innerHTML = '';
  if (!connectDlg.open) connectDlg.showModal();
}

let autoGen = 0;
async function autoReconnect(known, why = '') {
  if (state.link) return;
  const g = ++autoGen;
  document.body.classList.add('offline');
  if (connectDlg.open) connectDlg.close();
  const last = lastWand.get();
  let target = known.find((d) => d.id === last?.id) || known[0];
  const bar = $('#reconnectBar');
  bar.hidden = false;
  const others = $('#otherWands');
  others.hidden = known.length < 2;
  others.innerHTML = known.map((d, i) => `<option value="${i}" ${d === target ? 'selected' : ''}>${esc(d.name || 'Wand')}</option>`).join('');
  others.onchange = () => { target = known[+others.value]; tries = 0; };
  let tries = 0;
  while (g === autoGen && !state.link) {
    // In the background (another tab, minimised) don't grab the wand: a wand
    // held by a forgotten page never sleeps and drains its battery.
    if (document.visibilityState === 'hidden') { await sleep(2000); continue; }
    tries++;
    $('#reconnectMsg').textContent = (why && tries === 1 ? why + ' ' : '') +
      t('Reconnecting to {name}… pick it up to wake it.', { name: target.name || 'Wand' }) + (tries > 1 ? ` (${tries})` : '');
    const ok = await connectTo(target.demo ? null : target, { quiet: true });
    if (ok || g !== autoGen) return;
    await sleep(3000);
  }
}
$('#newWandBtn').addEventListener('click', () => {
  autoGen++;
  if (demo) try { sessionStorage.removeItem('mockWand'); } catch { /* */ }
  connectTo(null);
});

async function connectTo(device, { quiet = false } = {}) {
  const link = demo ? new MockWand() : new BleWand(device);
  const progress = (msg) => { if (!quiet) $('#connectMsg').textContent = msg; console.log('[wand]', msg); };
  progress(device ? t('Reaching {name}… (pick the wand up to wake it)', { name: device.name }) : '');
  link.onClose(() => progress(t('The wand dropped the connection while setting up. Tap its button to restart it and try again.')));
  connectDlg.classList.add('busy');
  try {
    await link.connect(progress);
    // Prove the wand really answers: PING must come back as PONG.
    const pong = new Promise((res, rej) => {
      const timer = setTimeout(() => rej(new Error(t('connected, but the wand did not answer PING within 4 s'))), 4000);
      link.onLine((l) => { if (l.trim() === 'PONG') { clearTimeout(timer); res(); } });
    });
    progress(t('Sent PING, waiting for PONG…'));
    link.send('PING');
    await pong;
    link.onLine(onLine);
    link.onClose(onClosed);
  } catch (e) {
    connectDlg.classList.remove('busy');
    console.warn('[wand] connect failed', e);
    link.onClose(() => {});
    link.disconnect();
    if (e.name === 'NotFoundError' && /cancel/i.test(e.message)) { progress(''); return false; }  // chooser closed
    progress(t("Couldn't use the wand: {why}.", { why: e.message }));
    return false;
  }
  connectDlg.classList.remove('busy');
  autoGen++;
  state.link = link;
  state.lastDevice = link.device || (demo ? { name: link.name, id: 'demo', demo: true } : null);
  state.checked = false;
  lastWand.set({ id: link.device?.id || 'demo', name: link.name });
  if (demo) try { localStorage.setItem('demoWandKnown', '1'); } catch { /* */ }
  if (connectDlg.open) connectDlg.close();
  $('#reconnectBar').hidden = true;
  document.body.classList.remove('offline');
  send('HELLO');
  training.onConnect();
  updateStream();
  return true;
}

$('#findBtn').addEventListener('click', () => connectTo(null));
$('#connectBtn').addEventListener('click', () => { state.release = true; stopStream(); setTimeout(() => state.link?.disconnect(), 300); });

// Stop the motion stream BEFORE the page goes away. If the link drops while
// the wand is in the middle of sending a stream line, the Bluetooth library
// on the wand never returns and the wand freezes (until the watchdog resets
// it). So: stream off when the tab is hidden (switching away or closing both
// hide it first, while the page can still send), on again when it's back.
function stopStream() {
  if (!state.link) return;
  try { state.link.sendNow ? state.link.sendNow('STREAM 0') : state.link.send('STREAM 0'); } catch {}
}
// TEST (ble-silabs): page-exit STREAM 0 disabled so the wand gets a link that
// drops mid-stream on close/reload. Uncomment the three marked lines to restore.
// A page left in the background lets the wand go after a minute, so it can
// sleep (it reconnects by itself when the page is shown again).
let hiddenTimer = 0;
document.addEventListener('visibilitychange', () => {
  training.onShown(shownTab() === 'training' && document.visibilityState === 'visible');
  clearTimeout(hiddenTimer);
  if (!state.link) return;
  if (document.visibilityState === 'hidden') {
    hiddenTimer = setTimeout(() => {
      if (document.visibilityState !== 'hidden' || !state.link || state.backupBusy) return;
      state.release = true;
      stopStream();
      setTimeout(() => state.link?.disconnect(), 300);
    }, 60000);
  } else updateStream();
});
// TEST window.addEventListener('pagehide', stopStream);

function onClosed() {
  if (!state.link) return;  // already handled (a link can report its end twice)
  const name = state.info?.name || state.link?.name || t('the wand');
  const wasOn = !!state.link;
  state.link = null;
  state.info = null;
  state.recording = state.learning = -1;
  training.onDisconnect();
  setStatus(t('No wand connected'));
  render();
  for (const d of ['#libDlg', '#spellDlg', '#devDlg', '#restoreDlg']) if ($(d).open) $(d).close();
  const dev = state.lastDevice;
  if (state.release || !dev) { state.release = false; return knownList().then((k) => (k.length ? autoReconnect(k, t('Wand released.')) : showConnect())); }
  if (state.reconnectAfterRestart) {
    state.reconnectAfterRestart = false;
    return sleep(2500).then(() => autoReconnect([dev], t('The wand is restarting with its new name…')));
  }
  autoReconnect([dev], wasOn ? t('Lost {name}. It may have fallen asleep.', { name }) : '');
}

function send(line) {
  if (!state.link) return toast(t('Connect the wand first'), true);
  logLine(line, 'out');
  state.link.send(line);
  return true;
}

function setStatus(x) { $('#status').textContent = x; }

// ------------------------------------------------------------------ incoming
function onLine(line) {
  const [tag, ...p] = line.split(' ');
  if (tag !== 'F' && tag !== 'GDATA') logLine(line, tag === 'ERR' ? 'err' : 'in');
  switch (tag) {
    case 'INFO':
      try { state.info = JSON.parse(line.slice(5)); } catch { return toast(t('Bad INFO from wand'), true); }
      onInfo();
      break;
    case 'F':
      state.feats.push(p.slice(0, 4).map(Number));
      if (state.feats.length > 250) state.feats.shift();
      break;
    case 'MATCH': onMatch(+p[0], +p[1], +p[2], p[3] === '1'); break;
    case 'CAST': onCast(+p[0], +p[1], p[2] === '1'); break;
    case 'REC': onRec(+p[0], p[1], p.slice(2)); break;
    case 'LEARN': onLearn(+p[0], p[1], p[2], p[3], p[4], p[5]); break;
    case 'BAT':
      if (state.info) Object.assign(state.info, { bat: +p[0], volts: +p[1], chg: +p[2] });
      renderStatus();
      break;
    case 'CAL':
      if (p[0] === 'ok') { toast(t('Direction calibrated ✓')); send('HELLO'); }
      else if (p[0] === 'fail') toast(t('Calibration failed: keep the wand still, tip up'), true);
      else toast(t('Hold the wand still with the tip pointing straight up…'));
      break;
    case 'OK':
      if (['GNAME', 'GCLR', 'CODE', 'CNAME', 'CCLR', 'BIND', 'SET', 'RESET', 'GUNDO', 'GTHR', 'GLOAD'].includes(p[0])) send('HELLO');
      if (p[0] === 'CODE' || p[0] === 'CCLR') delete state.captured[+p[1]];  // slot replaced: old capture info is stale
      if (p[0] === 'RESET') state.captured = {};
      if ((p[0] === 'SEND' || p[0] === 'TRY') && !knightPairing()) toast(t('IR sent. Did the device react?'));
      if (p[0] === 'RESET') { toast(t('Wand reset to factory settings')); state.checked = false; }
      if (p[0] === 'SET' && p[1] === 'name') {
        state.reconnectAfterRestart = true;
        toast(t('Renamed to {name}. The wand restarts and reconnects…', { name: p[2] }));
        setTimeout(() => state.link?.disconnect(), 300);  // let it restart without a live link
      }
      break;
    case 'SLEEP': toast(t('Wand is going to sleep 💤')); break;
    case 'ERR': toast(line.slice(4), true); break;
  }
  feedWaiters(line);
  training.onLine(tag, p);
  spells.onLine(tag, p);
}
const knightPairing = () => !!document.querySelector('#knightArea .steps, #knightArea [data-act=pair-done]') || false;

// First INFO of a connection: new wand -> setup; reset wand known here -> restore.
function onInfo() {
  const id = wandId(state.info, state.link);
  if (id !== state.id) { state.id = id; state.profile = loadProfile(id); }
  render();
  knight.onInfo();
  if (state.checked) return;
  state.checked = true;
  const p = state.profile;
  if (wandIsEmpty(state.info) && listBackups(id).length) return offerRestore();
  if (!p.setupDone && (factoryNamed(state.info) || (p.setupStep && p.setupStep !== 'done'))) return startSetup();
  if (!p.setupDone) { p.setupDone = true; p.setupStep = 'done'; storeProfile(); }  // a wand set up before this app version
}

function onMatch(g, dist, second, accepted) {
  const li = document.createElement('li');
  li.className = accepted ? 'ok' : 'no';
  const gs = gestures()[g];
  const sp = gs ? spellOfName(gs.n) : null;
  if (g < 0) li.textContent = t('motion (no spells trained)');
  else if (accepted) li.innerHTML = `${spellIcon(sp, { size: 30, initial: gs.n })}<span>${esc(displayName(gs.n))}</span><span class="muted small">${dist.toFixed(2)}</span>`;
  else li.innerHTML = `${spellIcon(sp, { size: 30, unsure: true, initial: gs.n })}<span>${esc(t('Maybe {name}?', { name: displayName(gs.n) }))}</span>
    <button class="small" data-retrain="${g}">${t('Re-train')}</button>`;
  const h = $('#history');
  h.prepend(li);
  while (h.children.length > 12) h.lastChild.remove();
}
$('#history').addEventListener('click', (e) => {
  const b = e.target.closest('button[data-retrain]');
  if (b) { showTab('spells'); spells.open({ slot: +b.dataset.retrain }); }
});

function onCast(g, c, fired) {
  const el = $('#lastCast');
  el.classList.remove('muted', 'flash');
  void el.offsetWidth;
  el.classList.add('flash');
  const gs = gestures()[g];
  const what = c < 0 ? t('no action yet') : fired ? t('sent “{code}”', { code: codeLabel(c) }) : t('would send “{code}” (not sent while charging)', { code: codeLabel(c) });
  el.innerHTML = `<div class="cast-row">${spellIcon(gs ? spellOfName(gs.n) : null, { size: 84, initial: gs?.n })}
    <div><b>${esc(gs ? displayName(gs.n) : t('Unknown'))}</b><span class="sub">${esc(what)}</span></div></div>`;
}

function onRec(g, what, rest) {
  if (what === 'armed') state.recording = g;
  else if (what === 'ok' || what === 'timeout') { state.recording = -1; if (what === 'ok') send('HELLO'); }
}

// LEARN c count 3 / count 2 / count 1 / go / waiting /
//       ok <bits> <hex> <repeats> <khz> / timeout / unsupported
function onLearn(c, what, n, hex, reps, khz) {
  if (what === 'count' || what === 'go' || what === 'waiting') {
    state.learning = c;
    state.learnMsg = what === 'count' ? t('Get ready… {n}', { n }) : what === 'go' ? t('GO! Press the remote button now') : t('⏳ Listening: press the remote button now');
    if (what === 'go') toast(t('GO! Press the remote button now'));
    renderCodes();
    libLearnState(what === 'count' ? t('Aim your remote at the wand tip… {n}', { n }) : `${state.learnMsg}`);
  } else {
    state.learning = -1;
    const got = hex ? `0x${hex} x${reps} · ${khz} kHz` : t('{n} bits', { n });
    if (what === 'ok') state.captured[c] = got;
    libLearnState(what === 'ok' ? t('✓ Learned {code}. Test it, then save.', { code: got }) : t('✗ Nothing usable received. Try again, closer.'), what === 'ok' ? c : -1);
    if (what === 'ok') toast(t('Captured {code} ✓', { code: got }));
    else if (what === 'unsupported') toast(t("Received something, but couldn't decode it. Try again, closer"), true);
    else toast(t('Nothing received. Try closer, within 10 cm'), true);
    send('HELLO');
  }
}

// ------------------------------------------------------------------ rendering
function render() {
  renderStatus();
  spells.render();
  devices.render();
  knight.render();
  renderCodes();
  renderSettings();
  renderBackups();
}

function renderStatus() {
  const i = state.info;
  if (!state.link) return;
  if (!i) return setStatus(t('{name}: loading…', { name: state.link.name }));
  const bat = i.chg ? t('⚡ charging ({pct}%)', { pct: i.bat }) : `🔋 ${i.bat}%`;
  const who = state.profile?.owner ? t('{owner}’s {wand}', { owner: state.profile.owner, wand: i.name }) : i.name;
  setStatus(`${who} · ${bat} · fw ${i.fw}`);
}

function renderCodes() {
  const cs = state.info?.c || Array.from({ length: 12 }, () => ({ n: '', k: 0, l: 0 }));
  $('#codeList').innerHTML = cs.map((c, i) => `
    <div class="code ${state.learning === i ? 'learning' : ''}" data-c="${i}">
      <span class="idx">${i + 1}</span>
      <div>
        <input value="${esc(fromWire(c.n))}" placeholder="${c.k ? t('Code {n}', { n: i + 1 }) : t('empty slot')}" maxlength="15" data-act="name" ${c.k && state.link ? '' : 'disabled'} />
        <div class="meta">${state.learning === i ? esc(state.learnMsg || t('Get ready…')) : c.k && state.captured[i] ? esc(state.captured[i]) : c.k ? t('{n} pulses · {khz} kHz', { n: c.l, khz: c.k }) : '—'}</div>
      </div>
      <div class="actions">
        <button class="small" data-act="learn" ${state.link && state.info?.irrx ? '' : 'disabled'}>${t('Learn')}</button>
        <button class="small" data-act="preset" ${state.link ? '' : 'disabled'}>${t('Library')}</button>
        <button class="small" data-act="send" ${c.k && state.link ? '' : 'disabled'}>${t('Test')}</button>
        <button class="small" data-act="clear" ${c.k && state.link ? '' : 'disabled'}>${t('Clear')}</button>
      </div>
    </div>`).join('');
}

$('#codeList').addEventListener('click', (e) => {
  const act = e.target.dataset.act;
  const row = e.target.closest('.code');
  if (!row || !act || e.target.tagName !== 'BUTTON') return;
  const c = +row.dataset.c;
  const name = row.querySelector('input').value;
  if (act === 'learn') send(`LEARN ${c}${name.trim() ? ' ' + toWire(name) : ''}`);
  else if (act === 'send') send(`SEND ${c}`);
  else if (act === 'clear') confirm(t('Clear {name}?', { name: codeLabel(c) })) && send(`CCLR ${c}`);
  else if (act === 'preset') openLibrary({ slot: c });
});
$('#codeList').addEventListener('change', (e) => {
  const row = e.target.closest('.code');
  if (row && e.target.dataset.act === 'name') send(`CNAME ${row.dataset.c} ${toWire(e.target.value)}`);
});

// ------------------------------------------------------------------ library dialog (IR codes tab)
// Device -> brand -> button, Test it on the real device, then save it to the
// wand and (optionally) bind it to a spell.
const libDlg = $('#libDlg');
const libDev = $('#libDevice'), libBrand = $('#libBrand'), libAct = $('#libAction'), libSpell = $('#libSpell');
const fillLibDevices = () => {
  const v = libDev.value;
  libDev.innerHTML = Object.keys(LIBRARY).map((d) => `<option value="${esc(d)}">${esc(t(d))}</option>`).join('');
  if (v) libDev.value = v;
};
fillLibDevices();
const libEntry = () => LIBRARY[libDev.value]?.[libBrand.value];
const libAction = () => libEntry()?.actions[+libAct.value];
const libName = () => toWire(`${libEntry().short}_${libAction().short}`);
const slotByName = (name) => codes().findIndex((c) => c.k && c.n === name);

function fillLibBrands() {
  libBrand.innerHTML = Object.keys(LIBRARY[libDev.value]).map((b) => `<option value="${esc(b)}">${esc(t(b))}</option>`).join('');
  fillLibActions();
}
function fillLibActions() {
  const b = libEntry();
  $('#libNote').textContent = b.note ? t(b.note) : '';
  libAct.innerHTML = b.actions.map((a, k) => `<option value="${k}">${esc(t(a.label, a.vars))}${a.learn ? ' ' + t('(learn from your remote)') : ''}</option>`).join('');
  onLibAction();
}
function onLibAction() {
  const a = libAction();
  const learned = a.learn ? slotByName(libName()) : -1;
  if (state.lib) state.lib.learnSlot = learned;
  $('#libTest').hidden = a.learn && learned < 0;
  $('#libLearn').hidden = !a.learn;
  $('#libLearn').textContent = learned >= 0 ? t('Learn again') : t('Learn from remote');
  $('#libSave').disabled = a.learn && learned < 0;
  $('#libState').textContent = a.learn
    ? learned >= 0 ? t('Already learned (slot {n}).', { n: learned + 1 }) : t('Set your remote as you want it, press Learn, then press the button on your remote near the wand.')
    : t('Point the wand at the device and press Test. Did it react?');
}
function libLearnState(text, slot = -1) {
  if (!libDlg.open) return;
  $('#libState').textContent = text;
  if (slot >= 0) { state.lib.learnSlot = slot; $('#libTest').hidden = false; $('#libSave').disabled = false; }
}
libDev.addEventListener('change', fillLibBrands);
libBrand.addEventListener('change', fillLibActions);
libAct.addEventListener('change', onLibAction);

function openLibrary({ spell = -1, slot = -1 } = {}) {
  if (!state.link) return;
  state.lib = { spell, slot, learnSlot: -1 };
  libSpell.innerHTML = `<option value="-1">${t('— just save the code —')}</option>` +
    gestures().map((g, i) => (g.c ? `<option value="${i}" ${i === spell ? 'selected' : ''}>${esc(displayName(g.n))}</option>` : '')).join('');
  $('#libTitle').textContent = slot >= 0 ? t('Fill IR slot {n}', { n: slot + 1 }) : t('Remote library');
  if (!libBrand.options.length) fillLibBrands(); else onLibAction();
  libDlg.showModal();
}

$('#libTest').addEventListener('click', () => {
  const a = libAction();
  const s = a.learn ? state.lib.learnSlot : slotByName(libName());
  if (s >= 0) return send(`SEND ${s}`);
  const code = a.make();
  send(`TRY ${code.khz} ${code.d.join(',')}`);
});
$('#libLearn').addEventListener('click', () => {
  let s = state.lib.slot >= 0 ? state.lib.slot : slotByName(libName());
  if (s < 0) s = freeSlot();
  if (s < 0) return toast(t('All 12 IR slots are used. Clear one in the IR codes tab'), true);
  send(`LEARN ${s} ${libName()}`);
});
$('#libSave').addEventListener('click', (e) => {
  e.preventDefault();
  const a = libAction();
  const name = libName();
  let s;
  if (a.learn) {
    s = state.lib.learnSlot;
  } else {
    s = state.lib.slot >= 0 ? state.lib.slot : slotByName(name);
    if (s < 0) s = freeSlot();
    if (s < 0) return toast(t('All 12 IR slots are used. Clear one in the IR codes tab'), true);
    const code = a.make();
    send(`CODE ${s} ${code.khz} ${name} ${code.d.join(',')}`);
  }
  const g = +libSpell.value;
  if (g >= 0) send(`BIND ${g} ${s}`);
  toast(g >= 0 ? t('{spell} now sends {what} ✓', { spell: displayName(gestures()[g].n), what: `${t(libBrand.value)} · ${t(a.label, a.vars)}` }) : t('Saved as {name} ✓', { name: fromWire(name) }));
  libDlg.close();
});

// ------------------------------------------------------------------ settings
function renderSettings() {
  const i = state.info;
  const on = !!(state.link && i);
  for (const id of ['thr', 'sleep', 'wake', 'haptics', 'calBtn', 'buzzBtn', 'sleepBtn', 'exportBtn', 'resetBtn', 'wandName', 'renameBtn', 'backupBtn', 'setupBtn', 'ownerName', 'ownerBtn'])
    $('#' + id).disabled = !on;
  if (!i) return;
  if (document.activeElement !== $('#wandName')) $('#wandName').value = (i.name || '').replace(/^Wand-/, '');
  if (document.activeElement !== $('#ownerName')) $('#ownerName').value = state.profile?.owner || '';
  $('#thr').value = i.thr; $('#thrOut').textContent = (+i.thr).toFixed(2);
  $('#sleep').value = i.sleep; $('#sleepOut').textContent = `${i.sleep} s`;
  $('#wake').value = i.wake; $('#wakeOut').textContent = `${i.wake}`;
  $('#haptics').checked = !!i.haptics;
  $('#haptics').disabled = !on || !i.motor;
  $('#buzzBtn').disabled = !on || !i.motor;
  $('#deviceInfo').textContent = JSON.stringify({ fw: i.fw, id: i.uid || '(firmware before 0.5.0)', battery: `${i.bat}% (${i.volts} V)`, charging: !!i.chg,
    axis: i.axis, vibration_motor: !!i.motor, ir_receiver: !!i.irrx, storage: i.store || 'flash' }, null, 2);
}
const live = (id, fmt) => $('#' + id).addEventListener('input', (e) => ($('#' + id + 'Out').textContent = fmt(e.target.value)));
live('thr', (v) => (+v).toFixed(2));
live('sleep', (v) => `${v} s`);
live('wake', (v) => v);
$('#thr').addEventListener('change', (e) => send(`SET thr ${e.target.value}`));
$('#sleep').addEventListener('change', (e) => send(`SET sleep ${e.target.value}`));
$('#wake').addEventListener('change', (e) => send(`SET wake ${e.target.value}`));
$('#haptics').addEventListener('change', (e) => send(`SET haptics ${e.target.checked ? 1 : 0}`));
$('#ownerBtn').addEventListener('click', () => { state.profile.owner = $('#ownerName').value.trim(); storeProfile(); renderStatus(); toast(t('Saved ✓')); });

$('#calBtn').addEventListener('click', () => send('CAL'));
$('#renameBtn').addEventListener('click', () => {
  const v = $('#wandName').value.trim().replace(/[^A-Za-z0-9-]+/g, '_').slice(0, 11);
  send(`SET name ${v || '-'}`);
});
$('#buzzBtn').addEventListener('click', () => send('BUZZ 200'));
$('#sleepBtn').addEventListener('click', () => send('SLEEP'));
$('#resetBtn').addEventListener('click', () => {
  if (prompt(t('This erases every spell and IR code. Type RESET to confirm.')) === 'RESET') send('RESET yes');
});

// Download the IR codes as JSON (also kept in the browser backups).
$('#exportBtn').addEventListener('click', async () => {
  const used = codes().map((c, i) => (c.k ? i : -1)).filter((i) => i >= 0);
  if (!used.length) return toast(t('No IR codes to back up'), true);
  const out = [];
  for (const i of used) {
    send(`DUMPC ${i}`);
    const l = await expect((x) => x.startsWith(`CODE ${i} `), 4000);
    if (!l) continue;
    const [, slot, khz, name, list] = l.split(' ');
    out.push({ slot: +slot, khz: +khz, name: fromWire(name === '-' ? '' : name), d: list ? list.split(',').map(Number) : [] });
  }
  const blob = new Blob([JSON.stringify({ wand: state.link?.name, codes: out }, null, 2)], { type: 'application/json' });
  Object.assign(document.createElement('a'), { href: URL.createObjectURL(blob), download: 'wand-ir-codes.json' }).click();
});

// ------------------------------------------------------------------ log + raw commands
function logLine(text, cls) {
  const el = $('#log');
  const line = document.createElement('div');
  line.className = cls;
  line.textContent = (cls === 'out' ? '→ ' : '← ') + (text.length > 300 ? text.slice(0, 300) + '…' : text);
  el.append(line);
  while (el.children.length > 400) el.firstChild.remove();
  el.scrollTop = el.scrollHeight;
}
$('#cmdForm').addEventListener('submit', (e) => {
  e.preventDefault();
  const v = $('#cmdInput').value.trim();
  if (v) send(v);
  $('#cmdInput').value = '';
});

// ------------------------------------------------------------------ tabs, toast
// The motion stream runs only while the Home or Wizard tab (or the spell trainer) is shown.
const shownTab = () => document.querySelector('.tabs button[aria-selected="true"]')?.dataset.tab;
const streamTabShown = () => ['live', 'training'].includes(shownTab()) || spells.busy();
function updateStream() {
  if (!state.link) return;
  send(`STREAM ${streamTabShown() && document.visibilityState === 'visible' ? 1 : 0}`);
}
new MutationObserver(updateStream).observe($('#spellDlg'), { attributes: true, attributeFilter: ['open'] });

function showTab(name) {
  document.querySelectorAll('.tabs button').forEach((x) => x.setAttribute('aria-selected', x.dataset.tab === name));
  document.querySelectorAll('.tab').forEach((x) => (x.hidden = x.id !== 'tab-' + name));
  training.onShown(name === 'training' && document.visibilityState === 'visible');
  updateStream();
}
document.querySelectorAll('.tabs button').forEach((b) => b.addEventListener('click', () => showTab(b.dataset.tab)));

let toastTimer;
function toast(text, err = false) {
  const el = $('#toast');
  const host = document.querySelector('dialog[open]') || document.body;  // stay above open dialogs
  if (el.parentElement !== host) host.append(el);
  el.textContent = text;
  el.className = 'toast' + (err ? ' err' : '');
  el.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => (el.hidden = true), 3500);
}

// ------------------------------------------------------------------ plot
const canvas = $('#plot');
function drawPlot() {
  const dpr = window.devicePixelRatio || 1;
  const w = canvas.clientWidth, h = canvas.clientHeight;
  if (canvas.width !== w * dpr) { canvas.width = w * dpr; canvas.height = h * dpr; }
  const ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, h);
  const css = getComputedStyle(document.documentElement);
  ctx.strokeStyle = css.getPropertyValue('--line');
  ctx.beginPath(); ctx.moveTo(0, h / 2); ctx.lineTo(w, h / 2); ctx.stroke();
  const colors = ['--k1', '--k2', '--k3', '--k4'].map((k) => css.getPropertyValue(k));
  const N = 250, scale = h / 2 / 450; // ±4.5 (x100) fills the height
  colors.forEach((col, k) => {
    ctx.strokeStyle = col; ctx.lineWidth = 1.6; ctx.beginPath();
    state.feats.forEach((f, i) => {
      const x = (i + N - state.feats.length) * (w / N), y = h / 2 - f[k] * scale;
      i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
    });
    ctx.stroke();
  });
  requestAnimationFrame(drawPlot);
}
requestAnimationFrame(drawPlot);
// Language: static text now, everything else re-rendered on change.
applyStatic();
initPickers();
onLangChange(() => {
  render();
  fillLibDevices();
  if (libDlg.open) fillLibBrands();
  training.onLang();
  if (!state.link) $('#findBtn').textContent = demo ? t('Summon the demo wand') : t('Search for wands…');
  if (!state.link) setStatus(t('No wand connected'));
});
render();
setStatus(t('No wand connected'));
document.body.classList.add('offline');
knownList().then((k) => (k.length ? autoReconnect(k) : showConnect()));
