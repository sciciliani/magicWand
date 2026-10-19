// wand-link.js — talks to the wand over Web Bluetooth (Nordic UART Service),
// plus a MockWand so the app can be explored without hardware (?demo).
//
// Both expose: connect(), disconnect(), send(line), onLine(cb), onClose(cb), name

const NUS = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
const NUS_RX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'; // app -> wand (write)
const NUS_TX = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'; // wand -> app (notify)
// Long writes: try 180-byte chunks, fall back to 20 if the wand's BLE stack
// only accepts one packet per write (MTU 23).
let CHUNK = 180;

import { t } from './i18n.js?v=5';
import { nec } from './ir-presets.js?v=14';

export const bluetoothAvailable = () => !!navigator.bluetooth;

/** Wands this browser was allowed to use before (Chrome keeps the permission). */
export async function knownWands() {
  try {
    if (!navigator.bluetooth?.getDevices) return [];
    return (await navigator.bluetooth.getDevices()).filter((d) => (d.name || '').startsWith('Wand'));
  } catch { return []; }
}

const withTimeout = (p, ms, what) =>
  Promise.race([p, new Promise((_, rej) => setTimeout(() => rej(new Error(t('{what} timed out', { what }))), ms))]);

export class BleWand {
  /** device: a BluetoothDevice to reconnect to, or null to show Chrome's chooser. */
  constructor(device = null) {
    this.device = device;
    this.name = device?.name || '';
    this._line = () => {};
    this._close = () => {};
    this._buf = '';
    this._queue = Promise.resolve();
    this._dec = new TextDecoder();
    this._enc = new TextEncoder();
  }
  onLine(cb) { this._line = cb; }
  onClose(cb) { this._close = cb; }

  /** progress(text) is called at each step, so the UI can show where it stops. */
  async connect(progress = () => {}) {
    if (!this.device) {
      progress(t('Opening Chrome’s wand list…'));
      this.device = await navigator.bluetooth.requestDevice({
        filters: [{ namePrefix: 'Wand' }, { services: [NUS] }],
        optionalServices: [NUS],
      });
    }
    this.name = this.device.name || 'Wand';
    progress(t('Connecting to {name}…', { name: this.name }));
    if (!this._listening) {
      this.device.addEventListener('gattserverdisconnected', () => this._close());
      this._listening = true;
    }
    const server = await withTimeout(this.device.gatt.connect(), 10000, t('Connecting'));
    progress(t('Connected. Looking for the wand service…'));
    let svc;
    try {
      svc = await server.getPrimaryService(NUS);
    } catch (e) {
      throw new Error(t("the wand's text service wasn't found ({why}). Restart the wand and try again", { why: e.message }));
    }
    progress(t('Opening the channel…'));
    this.rx = await svc.getCharacteristic(NUS_RX);
    this.tx = await svc.getCharacteristic(NUS_TX);
    this.tx.addEventListener('characteristicvaluechanged', (e) => this._onData(e.target.value));
    await this.tx.startNotifications();
    progress(t('Channel open. Saying hello…'));
  }

  disconnect() { this.device?.gatt?.connected && this.device.gatt.disconnect(); }

  /** Immediate write that skips the queue (used while the page is closing). */
  sendNow(line) {
    const bytes = this._enc.encode(line + '\n');
    const w = this.rx?.writeValueWithoutResponse ? this.rx.writeValueWithoutResponse(bytes) : this.rx?.writeValue(bytes);
    w?.catch?.(() => {});
  }

  _onData(dv) {
    this._buf += this._dec.decode(dv, { stream: true });
    let i;
    while ((i = this._buf.indexOf('\n')) >= 0) {
      const line = this._buf.slice(0, i).trim();
      this._buf = this._buf.slice(i + 1);
      if (line) this._line(line);
    }
  }

  // Writes are queued so long lines (IR codes) are never interleaved.
  send(line) {
    const bytes = this._enc.encode(line + '\n');
    this._queue = this._queue.then(async () => {
      for (let o = 0; o < bytes.length; ) {
        const part = bytes.slice(o, o + CHUNK);
        try {
          await this.rx.writeValueWithResponse(part);
          o += part.length;
        } catch (e) {
          if (CHUNK === 20) throw e;
          CHUNK = 20; // retry this part in small packets
        }
      }
    }).catch((e) => this._line(`ERR app write failed: ${e.message}`));
    return this._queue;
  }
}

// ---------------------------------------------------------------------------
// MockWand: a pretend wand that speaks the same protocol, for demos and UI work.
// Its state lives in sessionStorage, so a reload (or the restart after a
// rename) finds the same pretend wand; a factory reset empties it.
const MOCK_UID = '0000A1B2C3D4BEEF';
const MOCK_FACTORY = 'Wand-BEEF';
export class MockWand {
  constructor() {
    this._line = () => {};
    this._close = () => {};
    this._fresh();
    try { Object.assign(this, JSON.parse(sessionStorage.getItem('mockWand') || '{}')); } catch { /* fresh */ }
    this.name = this.s.name;
    this.stream = false;
    this.practice = false;
    this.t = 0;
    this.rec = -1;
  }
  _fresh() {
    this.g = Array.from({ length: 8 }, () => ({ n: '', c: 0, t: 0.45, b: -1 }));
    this.c = Array.from({ length: 12 }, () => ({ n: '', k: 0, l: 0, d: [] }));
    this.s = { thr: 0.45, sleep: 30, wake: 3, haptics: 1, axis: [1, 0, 0], name: MOCK_FACTORY };
  }
  _save() { try { sessionStorage.setItem('mockWand', JSON.stringify({ g: this.g, c: this.c, s: this.s })); } catch { /* private */ } }

  // Demo only: act out a move on the motion stream (Training tab test buttons).
  // speed 1 = normal, 1/3 = much too slow, 3 = rushed.
  perform(move, speed = 1) {
    const bump = (s, a, b) => (s <= a || s >= b ? 0 : Math.sin(Math.PI * (s - a) / (b - a)) ** 2);
    const dur = { circle: 1, hit: 0.3, zigzag: 0.9, stroke: 0.4, tipup: 0.4, swish: 0.45, other: 0.45 }[move] / speed;
    const P = 280 * speed;  // 420 deg/s peak (x100 / 1.5)
    const rows = [];
    const n = Math.max(3, Math.round(dur * 25));
    for (let i = 0; i < 6; i++) rows.push([0, 0, 0, 0, 8]);
    for (let i = 0; i < n; i++) {
      const s = i / n;
      let r = 0, u = 0;
      if (move === 'circle') { const e = 0.6 * bump(s, 0, 1) + 0.4; r = 0.8 * P * e * Math.cos(2 * Math.PI * s); u = 0.8 * P * e * Math.sin(2 * Math.PI * s); }
      if (move === 'hit') u = 1.2 * P * bump(s, 0, 0.5) - P * bump(s, 0.5, 1);
      if (move === 'zigzag') r = P * (bump(s, 0, 0.33) - bump(s, 0.33, 0.66) + bump(s, 0.66, 1));
      if (move === 'stroke') { u = -P * bump(s, 0, 1); r = -0.3 * P * bump(s, 0, 1); }
      if (move === 'tipup') { u = P * bump(s, 0, 1); r = 0.2 * P * bump(s, 0, 1); }
      if (move === 'swish' || move === 'other') r = P * bump(s, 0, 1);
      rows.push([Math.round(r), Math.round(u), 0, 0, Math.round(Math.hypot(r, u) * 1.5)]);
    }
    for (let i = 0; i < 14; i++) rows.push([0, 0, 0, 0, 8]);
    this._perf = rows;
    // Recording: the wand stores this move once it has been done.
    if (this.rec >= 0) {
      const g = this.rec;
      clearTimeout(this._recTimer);
      this._recTimer = setTimeout(() => this._recDone(g), rows.length * 40 + 150);
      return;
    }
    // Otherwise it recognises it (in practice mode nothing is sent).
    const wire = { zigzag: 'zigzag', stroke: 'stroke', tipup: 'tipup', circle: 'circle', hit: 'flick' }[move];
    let g = this.g.findIndex((x) => x.c && x.n.toLowerCase() === wire);
    if (g < 0 && move === 'other') g = this.g.findIndex((x) => x.c && !['zigzag', 'stroke', 'tipup', 'circle', 'flick'].includes(x.n.toLowerCase()));
    setTimeout(() => {
      this._line('SEG 30');
      const any = this.g.findIndex((x) => x.c);
      this._line(g >= 0 ? `MATCH ${g} 0.31 0.90 1` : `MATCH ${any} 0.95 1.20 0`);
      if (g >= 0 && !this.practice) this._line(`CAST ${g} ${this.g[g].b} ${this.g[g].b >= 0 ? 1 : 0}`);
    }, rows.length * 40 + 150);
  }
  _recDone(g) {
    this.rec = -1;
    const s = this.g[g];
    s.c = Math.min(4, s.c + 1);
    if (!s.n) s.n = `move${g}`;
    s.t = +(0.45 + s.c * 0.03).toFixed(2);
    this._save();
    // A made-up spell (not one of the app's) looks like the first spell: shows the warning.
    const other = this.g.findIndex((x, i) => i !== g && x.c > 0);
    const own = !['zigzag', 'stroke', 'tipup', 'circle', 'swishright', 'swishleft', 'keyturn', 'poke', 'flick', 'swishflick', 'circleback'].includes(s.n.toLowerCase());
    this._line('SEG 41');
    this._line(`REC ${g} ok ${s.c} ${s.t.toFixed(2)} near ${other} ${other >= 0 ? (own ? '0.400' : '0.920') : '-1.000'} ${other >= 0 ? '0.450' : '-1.000'}`);
  }
  onLine(cb) { this._line = cb; }
  onClose(cb) { this._close = cb; }
  async connect() {
    this._timer = setInterval(() => this._tick(), 40);
    this._emit('BOOT Wand-DEMO 0.1.0 wake=0');
  }
  disconnect() {
    if (!this._timer) return;
    clearInterval(this._timer);
    this._timer = 0;
    this._close();
  }
  _emit(l) { setTimeout(() => this._line(l), 20); }

  _tick() {
    this.t += 0.04;
    const t = this.t, burst = Math.max(0, Math.sin(t * 0.8)) ** 12;
    if (this.stream && this._perf?.length) {
      this._line(`F ${this._perf.shift().join(' ')}`);
    } else if (this.stream) {
      const k = this.practice ? 0 : burst;  // practising: hold still until a move is acted out
      const f = [Math.sin(t * 7) * 180 * k, Math.cos(t * 7) * 150 * k, Math.sin(t * 3) * 20 * (k || 0.05), k * 30];
      this._line(`F ${f.map((x) => Math.round(x)).join(' ')} ${Math.round(400 * k + 8)}`);
    }
    if (!this.practice && Math.abs((t % 7.85) - 2) < 0.02) {
      const trained = this.g.map((g, i) => [g, i]).filter(([g]) => g.c > 0);
      if (trained.length) {
        const [g, i] = trained[Math.floor(Math.random() * trained.length)];
        const sure = (this._casts = (this._casts || 0) + 1) % 3 !== 0;  // every third move: "not sure"
        this._emit(`SEG 48`);
        this._emit(`MATCH ${i} ${sure ? '0.21' : '0.58'} 0.74 ${sure ? 1 : 0}`);
        if (sure) this._emit(`CAST ${i} ${g.b} ${g.b >= 0 ? 1 : 0}`);
      }
    }
  }

  _info() {
    return 'INFO ' + JSON.stringify({
      name: this.s.name, fw: '0.5.0-demo', bat: 76, volts: 3.92, chg: 0, axis: this.s.axis, thr: this.s.thr,
      sleep: this.s.sleep, wake: this.s.wake, haptics: this.s.haptics, motor: 1, irrx: 1, uid: MOCK_UID,
      g: this.g, c: this.c.map(({ n, k, l }) => ({ n, k, l })),
    });
  }

  send(line) {
    const [cmd, a1, a2, ...rest] = line.split(' ');
    const i = parseInt(a1, 10);
    switch (cmd) {
      case 'HELLO': case 'INFO': this._emit(this._info()); break;
      case 'STREAM': this.stream = a1 === '1'; this._emit(`OK STREAM ${a1}`); break;
      case 'REC':
        // Recorded when a pretend move is acted out (perform), else timeout.
        if (a2) this.g[i].n = a2;
        this.rec = i;
        this._emit(`REC ${i} armed`);
        clearTimeout(this._recTimer);
        this._recTimer = setTimeout(() => { if (this.rec === i) { this.rec = -1; this._line(`REC ${i} timeout`); } }, 5650);
        break;
      case 'GNAME': this.g[i].n = a2; this._save(); this._emit(`OK GNAME ${i}`); break;
      case 'GCLR': this.g[i] = { n: '', c: 0, t: 0.45, b: this.g[i].b }; this._save(); this._emit(`OK GCLR ${i}`); break;
      case 'GUNDO': if (this.g[i].c) this.g[i].c--; this._save(); this._emit(`OK GUNDO ${i} ${this.g[i].c}`); break;
      case 'GTHR': this.g[i].t = +a2; this._save(); this._emit(`OK GTHR ${i} ${a2}`); break;
      case 'DUMPG': {
        const g = this.g[i];
        const name = Array.from({ length: 16 }, (_, k) => (g.n.charCodeAt(k) || 0).toString(16).padStart(2, '0')).join('');
        this._emit(`GDATA ${i} ${(name + g.c.toString(16).padStart(2, '0') + '000000' + '00000000' + '00'.repeat(128 * g.c)).toUpperCase()}`);
        break;
      }
      case 'GLOAD': {
        const hx = a2 || '';
        const name = (hx.slice(0, 32).match(/../g) || []).map((h) => parseInt(h, 16)).filter((c) => c).map((c) => String.fromCharCode(c)).join('');
        this.g[i] = { n: name, c: parseInt(hx.slice(32, 34), 16) || 0, t: 0.5, b: this.g[i].b };
        this._save();
        this._emit(`OK GLOAD ${i} ${this.g[i].c}`);
        break;
      }
      case 'LEARN':
        [3, 2, 1].forEach((k, j) => setTimeout(() => this._line(`LEARN ${i} count ${k}`), j * 1000));
        setTimeout(() => this._line(`LEARN ${i} go`), 3000);
        setTimeout(() => this._line(`LEARN ${i} waiting`), 3300);
        setTimeout(() => {
          // The pretend remote is an LG TV: Power, then Volume up, then others.
          const cmds = [[0x08, '20DF10EF'], [0x02, '20DF40BF'], [0x03, '20DFC03F'], [0x5a, '20DF5AA5']];
          const [cmd, hex] = cmds[(this._learnN = (this._learnN || 0) + 1) - 1 & 3];
          const d = nec(0x04, cmd).d;
          this.c[i] = { n: a2 || this.c[i].n || `code${i}`, k: 38, l: d.length, d };
          this._save();
          this._line(`LEARN ${i} ok 32 ${hex} 1 38`);
        }, 5300);
        break;
      case 'CODE': {
        const d = rest[1].split(',').map(Number);
        this.c[i] = { n: rest[0] === '-' ? '' : rest[0], k: +a2, l: d.length, d };
        this._save();
        this._emit(`OK CODE ${i} ${d.length}`);
        break;
      }
      case 'DUMPC': this._emit(`CODE ${i} ${this.c[i].k} ${this.c[i].n || '-'} ${this.c[i].d.join(',')}`); break;
      case 'CNAME': this.c[i].n = a2; this._save(); this._emit(`OK CNAME ${i}`); break;
      case 'CCLR': this.c[i] = { n: '', k: 0, l: 0, d: [] }; this.g.forEach((g) => g.b === i && (g.b = -1)); this._save(); this._emit(`OK CCLR ${i}`); break;
      case 'SEND': this._emit(this.c[i]?.k ? `OK SEND ${i}` : 'ERR SEND empty'); break;
      case 'TRY': this._emit('OK TRY'); break;
      case 'BIND': this.g[i].b = a2 === '-' ? -1 : +a2; this._save(); this._emit(`OK BIND ${i} ${this.g[i].b}`); break;
      case 'SET':
        if (a1 === 'name') {
          this.s.name = a2 === '-' ? MOCK_FACTORY : `Wand-${a2}`;
          this._save();
          this._emit(`OK SET name ${this.s.name} (restarting)`);
          setTimeout(() => this.disconnect(), 400);  // the real wand restarts
          break;
        }
        this.s[a1] = +a2; this._save(); this._emit(`OK SET ${a1} ${a2}`); break;
      case 'CAL': this._emit('CAL hold the wand still, tip pointing straight up'); setTimeout(() => this._line('CAL ok 0.012 0.998 -0.051'), 1500); break;
      case 'BUZZ': this._emit('OK BUZZ'); break;
      case 'PRACTICE': this.practice = a1 === '1'; this._emit(`OK PRACTICE ${this.practice ? 1 : 0}`); break;
      case 'BAT': this._emit('BAT 76 3.92 0'); break;
      case 'SLEEP': this._emit('SLEEP requested'); setTimeout(() => this.disconnect(), 300); break;
      case 'RESET':
        if (a1 === 'yes') { this._fresh(); this._save(); this.name = this.s.name; }
        this._emit(a1 === 'yes' ? 'OK RESET' : 'ERR type RESET yes'); break;
      case 'PING': this._emit('PONG'); break;
      default: this._emit(`ERR unknown ${cmd}`);
    }
    return Promise.resolve();
  }
}
