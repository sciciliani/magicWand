// app.js — Wand Workshop: configure the wand over Web Bluetooth.
// Protocol reference: docs/PROTOCOL.md
import { BleWand, MockWand, bluetoothAvailable, knownWands } from './wand-link.js?v=10';
import { LIBRARY } from './ir-presets.js?v=10';

const $ = (s) => document.querySelector(s);
const esc = (s) => String(s ?? '').replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);
const toWire = (s) => (s || '').trim().replace(/[^A-Za-z0-9-]+/g, '_').slice(0, 15) || '-';
const fromWire = (s) => (s || '').replace(/_/g, ' ');

// Suggested moves: all built from grip-independent motions (see docs/GESTURES.md).
const SUGGESTED = [
  { name: 'Aperio', glyph: '🗝', move: 'Twist the wand like turning a key (clockwise).', use: 'TV power' },
  { name: 'Lux', glyph: '⤴', move: 'Quick flick of the tip upward, then back.', use: 'Volume up / AC on' },
  { name: 'Umbra', glyph: '⤵', move: 'Firm slash downward.', use: 'Volume down / AC off' },
  { name: 'Tacet', glyph: '➶', move: 'Stab straight forward, like a poke.', use: 'Mute' },
  { name: 'Orbis', glyph: '◯', move: 'Draw a circle with the tip.', use: 'Input / source' },
  { name: 'Ventus', glyph: '〰', move: 'Swish to the right, then flick down.', use: 'AC power' },
  { name: 'Voco', glyph: '→', move: 'Sweep the tip to the right.', use: 'Channel up' },
  { name: 'Fulgur', glyph: 'ϟ', move: 'Draw a Z (lightning bolt).', use: 'Anything fun' },
];

const state = {
  link: null,
  info: null,
  recording: -1,
  learning: -1,
  lib: null, // open library dialog: { slot, spell, learnSlot }
  lastDevice: null, // BLE device to offer for reconnect
  feats: [], // [right, up, twist, thrust]
  pendingCodeDump: [],
  backup: null,
};

// ------------------------------------------------------------------ connection
// Nothing works without a wand, so a dialog asks for one whenever none is
// connected (at start and after every disconnect).
const demo = new URLSearchParams(location.search).has('demo');
const connectDlg = $('#connectDlg');
connectDlg.addEventListener('cancel', (e) => e.preventDefault()); // Esc can't dismiss it

async function showConnect(msg = '') {
  document.body.classList.add('offline');
  $('#connectMsg').textContent = msg;
  $('#noBle').hidden = bluetoothAvailable() || demo;
  $('#findBtn').hidden = !bluetoothAvailable() && !demo;
  $('#demoLink').hidden = demo;
  $('#findBtn').textContent = demo ? 'Summon the demo wand' : 'Search for wands…';
  // Wands this browser has connected to before (Chrome remembers them)
  const known = demo ? [] : await knownWands();
  if (state.lastDevice && !known.includes(state.lastDevice)) known.unshift(state.lastDevice);
  $('#knownWands').innerHTML = known.map((d, i) =>
    `<button class="wand-pick" data-i="${i}"><span class="glyph">✦</span><b>${esc(d.name || 'Wand')}</b><span class="muted small">reconnect</span></button>`).join('');
  $('#knownWands').onclick = (e) => {
    const b = e.target.closest('.wand-pick');
    if (b) connectTo(known[+b.dataset.i]);
  };
  if (!connectDlg.open) connectDlg.showModal();
}

async function connectTo(device) {
  const link = demo ? new MockWand() : new BleWand(device);
  const progress = (t) => { $('#connectMsg').textContent = t; console.log('[wand]', t); };
  progress(device ? `Reaching ${device.name}… (pick the wand up to wake it)` : '');
  link.onClose(() => progress('The wand dropped the connection while setting up. Tap its button to restart it and try again.'));
  connectDlg.classList.add('busy');
  try {
    await link.connect(progress);
    // Prove the wand really answers: PING must come back as PONG.
    const pong = new Promise((res, rej) => {
      const t = setTimeout(() => rej(new Error('connected, but the wand did not answer PING within 4 s')), 4000);
      link.onLine((l) => { if (l.trim() === 'PONG') { clearTimeout(t); res(); } });
    });
    progress('Sent PING, waiting for PONG…');
    link.send('PING');
    await pong;
    link.onLine(onLine);
    link.onClose(onClosed);
  } catch (e) {
    connectDlg.classList.remove('busy');
    console.warn('[wand] connect failed', e);
    if (e.name === 'NotFoundError' && /cancel/i.test(e.message)) return progress(''); // chooser closed
    link.onClose(() => {});
    link.disconnect();
    progress(`Couldn't use the wand: ${e.message}.`);
    return;
  }
  connectDlg.classList.remove('busy');
  state.link = link;
  state.lastDevice = link.device || null;
  connectDlg.close();
  document.body.classList.remove('offline');
  $('#connectBtn').textContent = 'Release wand';
  send('HELLO');
  updateStream();
}

$('#findBtn').addEventListener('click', () => connectTo(null));
$('#connectBtn').addEventListener('click', () => { stopStream(); setTimeout(() => state.link?.disconnect(), 300); });

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
document.addEventListener('visibilitychange', () => {
  if (!state.link) return;
  if (document.visibilityState === 'hidden') { /* TEST stopStream(); */ }
  else updateStream();
});
// TEST window.addEventListener('pagehide', stopStream);
// Closing or reloading while connected: send STREAM 0 and ask "Leave site?".
// Chrome doesn't wait for Bluetooth writes once the page is going away; the
// dialog keeps the page alive long enough for STREAM 0 to reach the wand.
// (Chrome shows its own text; pages can't set the message.)
/* TEST
window.addEventListener('beforeunload', (e) => {
  if (!state.link) return;
  stopStream();
  e.preventDefault();
  e.returnValue = '';
  setTimeout(updateStream, 1000);  // only runs if the user chose to stay
});
*/

function onClosed() {
  const name = state.info?.name || state.link?.name || 'the wand';
  const wasOn = !!state.link;
  state.link = null;
  state.info = null;
  state.recording = state.learning = -1;
  setStatus('No wand connected');
  render();
  if ($('#libDlg').open) $('#libDlg').close();
  if (state.reconnectAfterRestart && state.lastDevice) {
    state.reconnectAfterRestart = false;
    showConnect('The wand is restarting with its new name…');
    setTimeout(() => connectTo(state.lastDevice), 3000);
    return;
  }
  showConnect(wasOn ? `Lost ${name}. It may have fallen asleep: pick it up and reconnect.` : '');
}

function send(line) {
  if (!state.link) return toast('Connect the wand first', true);
  logLine(line, 'out');
  state.link.send(line);
}

function setStatus(t) { $('#status').textContent = t; }

// ------------------------------------------------------------------ incoming
function onLine(line) {
  const [tag, ...p] = line.split(' ');
  if (tag !== 'F') logLine(line, tag === 'ERR' ? 'err' : 'in');
  switch (tag) {
    case 'INFO':
      try { state.info = JSON.parse(line.slice(5)); } catch { return toast('Bad INFO from wand', true); }
      render();
      break;
    case 'F':
      state.feats.push(p.slice(0, 4).map(Number));
      if (state.feats.length > 250) state.feats.shift();
      break;
    case 'MATCH': onMatch(+p[0], +p[1], +p[2], p[3] === '1'); break;
    case 'CAST': onCast(+p[0], +p[1], p[2] === '1'); break;
    case 'REC': onRec(+p[0], p[1], p.slice(2)); break;
    case 'LEARN': onLearn(+p[0], p[1], p[2]); break;
    case 'BAT':
      if (state.info) Object.assign(state.info, { bat: +p[0], volts: +p[1], chg: +p[2] });
      renderStatus();
      break;
    case 'CAL':
      if (p[0] === 'ok') { toast('Direction calibrated ✓'); send('HELLO'); }
      else if (p[0] === 'fail') toast('Calibration failed: keep the wand still, tip up', true);
      else toast('Hold the wand still with the tip pointing straight up…');
      break;
    case 'CODE': onCodeDump(p); break;
    case 'OK':
      if (['GNAME', 'GCLR', 'CODE', 'CNAME', 'CCLR', 'BIND', 'SET', 'RESET'].includes(p[0])) send('HELLO');
      if (p[0] === 'SEND' || p[0] === 'TRY') toast('IR sent. Did the device react?');
      if (p[0] === 'RESET') toast('Wand reset to factory settings');
      if (p[0] === 'SET' && p[1] === 'name') {
        state.reconnectAfterRestart = true;
        toast(`Renamed to ${p[2]}. The wand restarts and reconnects…`);
        setTimeout(() => state.link?.disconnect(), 300);  // let it restart without a live link
      }
      break;
    case 'SLEEP': toast('Wand is going to sleep 💤'); break;
    case 'ERR': toast(line.slice(4), true); break;
  }
}

function gestureName(g) {
  const n = state.info?.g?.[g]?.n;
  return n ? fromWire(n) : g >= 0 ? `Move ${g + 1}` : 'Unknown';
}
function codeName(c) {
  const n = state.info?.c?.[c]?.n;
  return c < 0 ? 'nothing' : n ? fromWire(n) : `Code ${c + 1}`;
}

function onMatch(g, dist, second, accepted) {
  const li = document.createElement('li');
  li.className = accepted ? 'ok' : 'no';
  li.textContent = accepted
    ? `${gestureName(g)}  (score ${dist.toFixed(2)})`
    : g >= 0 ? `not sure (closest: ${gestureName(g)}, ${dist.toFixed(2)})` : 'motion (no spells trained)';
  const h = $('#history');
  h.prepend(li);
  while (h.children.length > 12) h.lastChild.remove();
}

function onCast(g, c, fired) {
  const el = $('#lastCast');
  el.classList.remove('muted', 'flash');
  void el.offsetWidth;
  el.classList.add('flash');
  const what = c < 0 ? 'no IR code bound yet' : fired ? `sent “${codeName(c)}”` : `would send “${codeName(c)}” (not sent while charging)`;
  el.innerHTML = `✨ ${esc(gestureName(g))}<span class="sub">${esc(what)}</span>`;
}

function onRec(g, what, rest) {
  if (what === 'armed') {
    state.recording = g;
    setSpellState(g, 'Get ready… tick, tick, GO! (5 s)');
  } else if (what === 'ok') {
    state.recording = -1;
    const n = +rest[0];
    toast(n >= 3 ? `Saved sample ${n}. ${gestureName(g)} is ready!` : `Saved sample ${n} of 3`);
    send('HELLO');
  } else if (what === 'timeout') {
    state.recording = -1;
    setSpellState(g, 'No move detected. Try again with a brisker motion.');
    renderSpells();
  }
}

// LEARN c count 3 / count 2 / count 1 / go / waiting / ok|timeout|unsupported
function onLearn(c, what, n) {
  if (what === 'count' || what === 'go' || what === 'waiting') {
    state.learning = c;
    state.learnMsg = what === 'count' ? `Get ready… ${n}` : what === 'go' ? 'GO! Press the remote button now' : '⏳ Listening: press the remote button now';
    if (what === 'go') toast('GO! Press the remote button now');
    renderCodes();
    libLearnState(what === 'count' ? `Aim your remote at the wand tip… ${n}` : `${state.learnMsg}`);
  } else {
    state.learning = -1;
    libLearnState(what === 'ok' ? `✓ Learned (${n} bits). Test it, then save.` : '✗ Nothing usable received. Try again, closer.', what === 'ok' ? c : -1);
    if (what === 'ok') toast(`Captured a ${n}-bit code ✓`);
    else if (what === 'unsupported') toast("Received something, but couldn't decode it. Try again, closer", true);
    else toast('Nothing received. Try closer, within 10 cm', true);
    send('HELLO');
  }
}

// ------------------------------------------------------------------ rendering
function render() {
  renderStatus();
  renderSpells();
  renderCodes();
  renderSettings();
}

function renderStatus() {
  const i = state.info;
  if (!state.link) return;
  if (!i) return setStatus(`${state.link.name}: loading…`);
  const bat = i.chg ? `⚡ charging (${i.bat}%)` : `🔋 ${i.bat}%`;
  setStatus(`${i.name || state.link.name} · ${bat} · fw ${i.fw}`);
}

function codeOptions(selected) {
  const codes = state.info?.c || [];
  let html = `<option value="-">— no IR code —</option>`;
  codes.forEach((c, i) => {
    if (c.k) html += `<option value="${i}" ${i === selected ? 'selected' : ''}>${esc(codeName(i))}</option>`;
  });
  return html;
}

function renderSpells() {
  const grid = $('#spellGrid');
  const gs = state.info?.g || Array.from({ length: 8 }, () => ({ n: '', c: 0, t: 0, b: -1 }));
  grid.innerHTML = gs.map((g, i) => `
    <div class="spell ${state.recording === i ? 'recording' : ''}" data-g="${i}">
      <input type="text" value="${esc(fromWire(g.n))}" placeholder="Move ${i + 1} name" maxlength="15" data-act="name" />
      <div class="row">
        <span class="dots" title="${g.c} of 4 samples">${[0, 1, 2, 3].map((k) => `<i class="${k < g.c ? 'on' : ''}"></i>`).join('')}</span>
        <span class="muted small">${g.c ? (g.c >= 3 ? 'trained' : `${g.c}/3 samples`) : 'empty'}</span>
      </div>
      <div class="row">
        <button class="small primary" data-act="rec" ${state.link ? '' : 'disabled'}>${g.c ? 'Record another' : 'Record'}</button>
        <button class="small" data-act="clear" ${g.c && state.link ? '' : 'disabled'}>Clear</button>
      </div>
      <div class="row"><span class="small muted">sends</span>
        <select data-act="bind" ${state.link ? '' : 'disabled'}>${codeOptions(g.b)}</select>
        <button class="small" data-act="lib" title="Choose a device and button from the library" ${state.link ? '' : 'disabled'}>📜 Library</button>
      </div>
      <div class="state"></div>
    </div>`).join('');
}

function setSpellState(g, text) {
  const el = document.querySelector(`.spell[data-g="${g}"]`);
  if (!el) return;
  el.classList.toggle('recording', state.recording === g);
  el.querySelector('.state').textContent = text;
}

$('#spellGrid').addEventListener('click', (e) => {
  const act = e.target.dataset.act;
  const card = e.target.closest('.spell');
  if (!card || !act) return;
  const g = +card.dataset.g;
  if (act === 'rec') {
    const name = card.querySelector('[data-act=name]').value;
    send(`REC ${g}${name.trim() ? ' ' + toWire(name) : ''}`);
  } else if (act === 'clear') {
    if (confirm(`Forget all samples for ${gestureName(g)}?`)) send(`GCLR ${g}`);
  } else if (act === 'lib') {
    openLibrary({ spell: g });
  }
});
$('#spellGrid').addEventListener('change', (e) => {
  const card = e.target.closest('.spell');
  if (!card) return;
  const g = +card.dataset.g;
  if (e.target.dataset.act === 'name') {
    if (state.info?.g?.[g]?.c) send(`GNAME ${g} ${toWire(e.target.value)}`);
  } else if (e.target.dataset.act === 'bind') {
    send(`BIND ${g} ${e.target.value}`);
  }
});

$('#suggestions').innerHTML = SUGGESTED.map((s, k) => `
  <div class="sugg"><span class="glyph">${s.glyph}</span><b>${esc(s.name)}</b>
    <p>${esc(s.move)}<br/>Good for: ${esc(s.use)}</p>
    <button class="small" data-sugg="${k}">Use in next free slot</button></div>`).join('');
$('#suggestions').addEventListener('click', (e) => {
  const k = e.target.dataset.sugg;
  if (k === undefined) return;
  const gs = state.info?.g || [];
  let slot = gs.findIndex((g) => !g.c);
  if (slot < 0) return toast('All 8 slots are used. Clear one first', true);
  const input = document.querySelector(`.spell[data-g="${slot}"] [data-act=name]`);
  input.value = SUGGESTED[k].name;
  input.focus();
  toast(`Slot ${slot + 1} named ${SUGGESTED[k].name}. Press Record and do the move.`);
});

function renderCodes() {
  const cs = state.info?.c || Array.from({ length: 12 }, () => ({ n: '', k: 0, l: 0 }));
  $('#codeList').innerHTML = cs.map((c, i) => `
    <div class="code ${state.learning === i ? 'learning' : ''}" data-c="${i}">
      <span class="idx">${i + 1}</span>
      <div>
        <input value="${esc(fromWire(c.n))}" placeholder="${c.k ? `Code ${i + 1}` : 'empty slot'}" maxlength="15" data-act="name" ${c.k && state.link ? '' : 'disabled'} />
        <div class="meta">${state.learning === i ? esc(state.learnMsg || 'Get ready…') : c.k ? `${c.l} pulses · ${c.k} kHz` : '—'}</div>
      </div>
      <div class="actions">
        <button class="small" data-act="learn" ${state.link && state.info?.irrx ? '' : 'disabled'}>Learn</button>
        <button class="small" data-act="preset" ${state.link ? '' : 'disabled'}>Library</button>
        <button class="small" data-act="send" ${c.k && state.link ? '' : 'disabled'}>Test</button>
        <button class="small" data-act="clear" ${c.k && state.link ? '' : 'disabled'}>Clear</button>
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
  else if (act === 'clear') confirm(`Clear ${codeName(c)}?`) && send(`CCLR ${c}`);
  else if (act === 'preset') openLibrary({ slot: c });
});
$('#codeList').addEventListener('change', (e) => {
  const row = e.target.closest('.code');
  if (row && e.target.dataset.act === 'name') send(`CNAME ${row.dataset.c} ${toWire(e.target.value)}`);
});

// ------------------------------------------------------------------ library dialog
// Device -> brand -> button, Test it on the real device, then save it to the
// wand and (optionally) bind it to a spell. Opened from a spell card
// ("📜 Library") or from an IR code slot ("Library").
const libDlg = $('#libDlg');
const libDev = $('#libDevice'), libBrand = $('#libBrand'), libAct = $('#libAction'), libSpell = $('#libSpell');
libDev.innerHTML = Object.keys(LIBRARY).map((d) => `<option>${esc(d)}</option>`).join('');
const libEntry = () => LIBRARY[libDev.value]?.[libBrand.value];
const libAction = () => libEntry()?.actions[+libAct.value];
const libName = () => toWire(`${libEntry().short}_${libAction().short}`);
const slotByName = (name) => (state.info?.c || []).findIndex((c) => c.k && c.n === name);
const freeSlot = () => (state.info?.c || []).findIndex((c) => !c.k);

function fillLibBrands() {
  libBrand.innerHTML = Object.keys(LIBRARY[libDev.value]).map((b) => `<option>${esc(b)}</option>`).join('');
  fillLibActions();
}
function fillLibActions() {
  const b = libEntry();
  $('#libNote').textContent = b.note || '';
  libAct.innerHTML = b.actions.map((a, k) => `<option value="${k}">${esc(a.label)}${a.learn ? ' (learn from your remote)' : ''}</option>`).join('');
  onLibAction();
}
function onLibAction() {
  const a = libAction();
  const learned = a.learn ? slotByName(libName()) : -1;
  if (state.lib) state.lib.learnSlot = learned;
  $('#libTest').hidden = a.learn && learned < 0;
  $('#libLearn').hidden = !a.learn;
  $('#libLearn').textContent = learned >= 0 ? 'Learn again' : 'Learn from remote';
  $('#libSave').disabled = a.learn && learned < 0;
  $('#libState').textContent = a.learn
    ? learned >= 0 ? `Already learned (slot ${learned + 1}).` : 'Set your remote as you want it, press Learn, then press the button on your remote near the wand.'
    : 'Point the wand at the device and press Test. Did it react?';
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
  const gs = state.info?.g || [];
  libSpell.innerHTML = `<option value="-1">— just save the code —</option>` +
    gs.map((g, i) => `<option value="${i}" ${i === spell ? 'selected' : ''}>${esc(gestureName(i))}${g.c ? '' : ' (not trained yet)'}</option>`).join('');
  $('#libTitle').textContent = spell >= 0 ? `What should ${gestureName(spell)} do?` : slot >= 0 ? `Fill IR slot ${slot + 1}` : 'Remote library';
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
  if (s < 0) return toast('All 12 IR slots are used. Clear one in the IR codes tab', true);
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
    if (s < 0) return toast('All 12 IR slots are used. Clear one in the IR codes tab', true);
    const code = a.make();
    send(`CODE ${s} ${code.khz} ${name} ${code.d.join(',')}`);
  }
  const g = +libSpell.value;
  if (g >= 0) send(`BIND ${g} ${s}`);
  toast(g >= 0 ? `${gestureName(g)} now sends ${libBrand.value} · ${a.label} ✓` : `Saved as ${fromWire(name)} ✓`);
  libDlg.close();
});

// ------------------------------------------------------------------ settings
function renderSettings() {
  const i = state.info;
  const on = !!(state.link && i);
  for (const id of ['thr', 'sleep', 'wake', 'haptics', 'calBtn', 'buzzBtn', 'sleepBtn', 'exportBtn', 'resetBtn', 'wandName', 'renameBtn'])
    $('#' + id).disabled = !on;
  if (!i) return;
  if (document.activeElement !== $('#wandName')) $('#wandName').value = (i.name || '').replace(/^Wand-/, '');
  $('#thr').value = i.thr; $('#thrOut').textContent = (+i.thr).toFixed(2);
  $('#sleep').value = i.sleep; $('#sleepOut').textContent = `${i.sleep} s`;
  $('#wake').value = i.wake; $('#wakeOut').textContent = `${i.wake}`;
  $('#haptics').checked = !!i.haptics;
  $('#haptics').disabled = !on || !i.motor;
  $('#buzzBtn').disabled = !on || !i.motor;
  $('#deviceInfo').textContent = JSON.stringify({ fw: i.fw, battery: `${i.bat}% (${i.volts} V)`, charging: !!i.chg,
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

$('#calBtn').addEventListener('click', () => send('CAL'));
$('#renameBtn').addEventListener('click', () => {
  const v = $('#wandName').value.trim().replace(/[^A-Za-z0-9-]+/g, '_').slice(0, 11);
  send(`SET name ${v || '-'}`);
});
$('#buzzBtn').addEventListener('click', () => send('BUZZ 200'));
$('#sleepBtn').addEventListener('click', () => send('SLEEP'));
$('#resetBtn').addEventListener('click', () => {
  if (prompt('This erases every spell and IR code. Type RESET to confirm.') === 'RESET') send('RESET yes');
});

// Backup: ask for every stored code, then download them as JSON.
$('#exportBtn').addEventListener('click', () => {
  const used = (state.info?.c || []).map((c, i) => (c.k ? i : -1)).filter((i) => i >= 0);
  if (!used.length) return toast('No IR codes to back up', true);
  state.backup = { expected: used.length, codes: [] };
  used.forEach((i) => send(`DUMPC ${i}`));
});
function onCodeDump(p) {
  if (!state.backup) return;
  const [slot, khz, name, list] = p;
  state.backup.codes.push({ slot: +slot, khz: +khz, name: fromWire(name === '-' ? '' : name), d: list ? list.split(',').map(Number) : [] });
  if (state.backup.codes.length >= state.backup.expected) {
    const blob = new Blob([JSON.stringify({ wand: state.link?.name, codes: state.backup.codes }, null, 2)], { type: 'application/json' });
    const a = Object.assign(document.createElement('a'), { href: URL.createObjectURL(blob), download: 'wand-ir-codes.json' });
    a.click();
    state.backup = null;
  }
}

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
// The motion stream runs only while the Live tab is shown in a visible page.
const liveTabShown = () => document.querySelector('.tabs button[aria-selected="true"]')?.dataset.tab === 'live';
function updateStream() {
  if (!state.link) return;
  send(`STREAM ${liveTabShown() && document.visibilityState === 'visible' ? 1 : 0}`);
}

document.querySelectorAll('.tabs button').forEach((b) =>
  b.addEventListener('click', () => {
    setTimeout(updateStream, 0);  // after the tab switch below
    document.querySelectorAll('.tabs button').forEach((x) => x.setAttribute('aria-selected', x === b));
    document.querySelectorAll('.tab').forEach((t) => (t.hidden = t.id !== 'tab-' + b.dataset.tab));
  }));

let toastTimer;
function toast(text, err = false) {
  const t = $('#toast');
  const host = document.querySelector('dialog[open]') || document.body;  // stay above open dialogs
  if (t.parentElement !== host) host.append(t);
  t.textContent = text;
  t.className = 'toast' + (err ? ' err' : '');
  t.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => (t.hidden = true), 3500);
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
render();
showConnect();
