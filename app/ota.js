// ota.js — Settings > Firmware: update the wand over Bluetooth.
//
// The update file (.gbl) comes from app/fw/latest.json, published next to the
// app by `make ota`, or from a file the user picks. The wand stores it in its
// bootloader's spare slot, checks it, and installs it on restart. If anything
// goes wrong before the end, the wand simply keeps its current firmware.
// Protocol: firmware/MagicWand/ota.h
import { t } from './i18n.js?v=6';

const $ = (s) => document.querySelector(s);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const WINDOW = 6144;  // bytes in flight beyond the wand's last ACK (its buffer holds 12 KB)

function crc32(bytes) {
  let c = ~0;
  for (let i = 0; i < bytes.length; i++) {
    c ^= bytes[i];
    for (let k = 0; k < 8; k++) c = (c >>> 1) ^ (0xEDB88320 & -(c & 1));
  }
  return (~c) >>> 0;
}

/** "0.6.0-mg24" > "0.5.1-mg24"? (numbers only, suffix ignored) */
export function newer(a, b) {
  const n = (v) => String(v || '').split('-')[0].split('.').map((x) => parseInt(x, 10) || 0);
  const [x, y] = [n(a), n(b)];
  for (let i = 0; i < Math.max(x.length, y.length); i++) if ((x[i] || 0) !== (y[i] || 0)) return (x[i] || 0) > (y[i] || 0);
  return false;
}

const REASONS = {
  'no-bootloader': 'This wand has no bootloader, so it can only be updated with the USB cable.',
  'bootloader-init': 'The wand’s bootloader didn’t answer. Update it with the USB cable.',
  'no-slot': 'This wand’s bootloader has no space for updates. Update it with the USB cable.',
  'needs-signed': 'This wand only accepts signed firmware. Update it with the USB cable.',
  'needs-encrypted': 'This wand only accepts encrypted firmware. Update it with the USB cable.',
  'slot-overlaps-app': 'The firmware is too big for updates over Bluetooth. Update it with the USB cable.',
  'slot-overlaps-data': 'An update over Bluetooth would erase the spells on this wand. Update it with the USB cable.',
  'too-big': 'This update file is too big for the wand.',
};
const reasonText = (r) => (REASONS[r] ? t(REASONS[r]) : t('The wand refused the update ({why}).', { why: r }));

export function initOta(app) {
  let latest = null;   // {version, file, size}
  let busy = false;
  let acked = 0, failed = '', ackWait = null;

  async function check() {
    try {
      const r = await fetch(`app/fw/latest.json?ts=${Date.now()}`, { cache: 'no-store' });
      latest = r.ok ? await r.json() : null;
    } catch { latest = null; }
    render();
  }

  function render(progress) {
    const info = app.info();
    const on = app.connected() && !!info;
    const cur = info?.fw || '—';
    $('#fwCurrent').textContent = cur;
    const can = on && info?.ota;
    const upd = latest && info && newer(latest.version, info.fw);
    $('#fwLatest').textContent = latest ? latest.version : t('none published');
    $('#fwUpdateBtn').hidden = !upd;
    $('#fwUpdateBtn').textContent = upd ? t('Update to {v}', { v: latest.version }) : '';
    $('#fwUpdateBtn').disabled = !can || busy;
    $('#fwFileBtn').disabled = !can || busy;
    $('#fwNote').textContent = !on ? '' : !info.ota ? t('This firmware ({v}) can’t be updated over Bluetooth yet. Install 0.6.0 or newer once with the USB cable; after that, updates come through here.', { v: cur })
      : upd ? '' : latest ? t('Your wand is up to date.') : '';
    const bar = $('#fwBar');
    bar.hidden = progress == null;
    if (progress != null) bar.value = progress;
  }

  function onLine(tag, p) {
    if (tag !== 'OTA') return;
    if (p[0] === 'ACK') { acked = +p[1]; ackWait?.(); }
    if (p[0] === 'ERR') { failed = p.slice(1).join(' ') || 'error'; ackWait?.(); }
  }

  function status(text) { $('#fwStatus').textContent = text; }

  async function install(bytes, label) {
    if (busy) return;
    if (!app.connected()) return app.toast(t('Connect the wand first'), true);
    if (bytes.length < 16 || bytes[0] !== 0xEB || bytes[1] !== 0x17 || bytes[2] !== 0xA6 || bytes[3] !== 0x03)
      return app.toast(t('That isn’t a wand firmware file (.gbl).'), true);
    if (!confirm(t('Install {what} on the wand? It takes about a minute. Keep the wand next to this device and don’t close the page. Your spells and IR codes stay on the wand.', { what: label }))) return;
    busy = true; failed = ''; acked = 0;
    render(0);
    try {
      status(t('Making a backup first…'));
      await app.backupNow();
      app.send('STREAM 0');
      app.send('OTA');
      const info = await app.expect((l) => l.startsWith('OTA INFO') || l.startsWith('ERR unknown OTA'), 5000);
      if (!info || info.startsWith('ERR')) throw new Error(t('The wand didn’t answer the update request.'));
      const [, , ok, why] = info.split(' ');
      if (ok !== 'ok') throw new Error(reasonText(why));
      const crc = crc32(bytes).toString(16).padStart(8, '0');
      app.send(`OTA BEGIN ${bytes.length} ${crc}`);
      const ready = await app.expect((l) => l.startsWith('OTA READY') || l.startsWith('OTA ERR'), 8000);
      if (!ready) throw new Error(t('The wand didn’t answer the update request.'));
      if (ready.startsWith('OTA ERR')) throw new Error(reasonText(ready.split(' ')[2]));
      const chunk = Math.max(20, Math.min(240, parseInt(ready.split(' ')[2], 10) || 20)) & ~3;
      const t0 = performance.now();
      for (let off = 0; off < bytes.length; off += chunk) {
        while (off - acked > WINDOW && !failed) {
          const got = await Promise.race([new Promise((r) => (ackWait = r)), sleep(10000).then(() => 'late')]);
          ackWait = null;
          if (got === 'late') throw new Error(t('The wand stopped answering during the update.'));
        }
        if (failed) throw new Error(t('The wand stopped the update: {why}', { why: failed }));
        if (!app.connected()) throw new Error(t('The connection dropped.'));
        await app.link().writeOta(bytes.subarray(off, Math.min(off + chunk, bytes.length)));
        if ((off / chunk) % 16 === 0) {
          const pct = off / bytes.length;
          const secs = (performance.now() - t0) / 1000;
          const left = pct > 0.03 ? Math.round(secs / pct - secs) : null;
          render(pct);
          status(left != null ? t('Sending… {pct}% (about {s} s left)', { pct: Math.round(pct * 100), s: left }) : t('Sending… {pct}%', { pct: Math.round(pct * 100) }));
        }
      }
      const until = Date.now() + 15000;
      while (acked < bytes.length && !failed && Date.now() < until) await sleep(100);
      if (failed) throw new Error(t('The wand stopped the update: {why}', { why: failed }));
      if (acked < bytes.length) throw new Error(t('The wand stopped answering during the update.'));
      render(1);
      status(t('Checking the file on the wand…'));
      app.send('OTA END');
      const done = await app.expect((l) => l.startsWith('OTA OK') || l.startsWith('OTA ERR'), 20000);
      if (!done) throw new Error(t('The wand didn’t confirm the update.'));
      if (done.startsWith('OTA ERR')) throw new Error(t('The file didn’t pass the wand’s check ({why}). Nothing was changed.', { why: done.slice(8) }));
      try { sessionStorage.setItem('otaFrom', app.info()?.fw || ''); } catch { /* */ }
      status(t('Installing… the wand restarts and reconnects by itself.'));
      app.expectRestart(t('The wand is installing the new firmware…'));
    } catch (e) {
      app.send('OTA ABORT');
      status(t('Update stopped: {why} The wand keeps its current firmware.', { why: e.message }));
      app.toast(t('Update failed'), true);
      render();
    } finally {
      busy = false;
    }
  }

  /** After a reconnect: did an update just finish? */
  function onInfo() {
    render();
    let from = '';
    try { from = sessionStorage.getItem('otaFrom') || ''; } catch { /* */ }
    if (!from) return;
    try { sessionStorage.removeItem('otaFrom'); } catch { /* */ }
    const now = app.info()?.fw;
    if (now && now !== from) { app.toast(t('Firmware updated to {v} ✓', { v: now })); status(t('Updated from {a} to {b}.', { a: from, b: now })); }
    else { app.toast(t('The wand restarted but still runs {v}.', { v: now || '?' }), true); status(t('The wand restarted but still runs {v}.', { v: now || '?' })); }
  }

  $('#fwUpdateBtn').addEventListener('click', async () => {
    if (!latest) return;
    status(t('Downloading {v}…', { v: latest.version }));
    try {
      const r = await fetch(`app/fw/${latest.file}?ts=${Date.now()}`, { cache: 'no-store' });
      if (!r.ok) throw new Error(r.status);
      install(new Uint8Array(await r.arrayBuffer()), latest.version);
    } catch (e) { status(t('Couldn’t download the update ({why}).', { why: e.message })); }
  });
  $('#fwFileBtn').addEventListener('click', () => $('#fwFile').click());
  $('#fwFile').addEventListener('change', async (e) => {
    const f = e.target.files[0];
    e.target.value = '';
    if (f) install(new Uint8Array(await f.arrayBuffer()), f.name);
  });

  check();
  return { render, onLine, onInfo, busy: () => busy };
}
