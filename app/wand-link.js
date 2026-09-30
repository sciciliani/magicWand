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

export const bluetoothAvailable = () => !!navigator.bluetooth;

/** Wands this browser was allowed to use before (Chrome keeps the permission). */
export async function knownWands() {
  try {
    if (!navigator.bluetooth?.getDevices) return [];
    return (await navigator.bluetooth.getDevices()).filter((d) => (d.name || '').startsWith('Wand'));
  } catch { return []; }
}

const withTimeout = (p, ms, what) =>
  Promise.race([p, new Promise((_, rej) => setTimeout(() => rej(new Error(`${what} timed out`)), ms))]);

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
      progress('Opening Chrome’s wand list…');
      this.device = await navigator.bluetooth.requestDevice({
        filters: [{ namePrefix: 'Wand' }, { services: [NUS] }],
        optionalServices: [NUS],
      });
    }
    this.name = this.device.name || 'Wand';
    progress(`Connecting to ${this.name}…`);
    if (!this._listening) {
      this.device.addEventListener('gattserverdisconnected', () => this._close());
      this._listening = true;
    }
    const server = await withTimeout(this.device.gatt.connect(), 10000, 'Connecting');
    progress('Connected. Looking for the wand service…');
    let svc;
    try {
      svc = await server.getPrimaryService(NUS);
    } catch (e) {
      throw new Error(`the wand's text service wasn't found (${e.message}). Restart the wand and try again`);
    }
    progress('Opening the channel…');
    this.rx = await svc.getCharacteristic(NUS_RX);
    this.tx = await svc.getCharacteristic(NUS_TX);
    this.tx.addEventListener('characteristicvaluechanged', (e) => this._onData(e.target.value));
    await this.tx.startNotifications();
    progress('Channel open. Saying hello…');
  }

  disconnect() { this.device?.gatt?.connected && this.device.gatt.disconnect(); }

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
export class MockWand {
  constructor() {
    this.name = 'Wand-DEMO';
    this._line = () => {};
    this._close = () => {};
    this.g = Array.from({ length: 8 }, () => ({ n: '', c: 0, t: 0.45, b: -1 }));
    this.c = Array.from({ length: 12 }, () => ({ n: '', k: 0, l: 0, d: [] }));
    this.s = { thr: 0.45, sleep: 30, wake: 3, haptics: 1, axis: [1, 0, 0], name: 'Wand-DEMO' };
    this.stream = false;
    this.t = 0;
  }
  onLine(cb) { this._line = cb; }
  onClose(cb) { this._close = cb; }
  async connect() {
    this._timer = setInterval(() => this._tick(), 40);
    this._emit('BOOT Wand-DEMO 0.1.0 wake=0');
  }
  disconnect() { clearInterval(this._timer); this._close(); }
  _emit(l) { setTimeout(() => this._line(l), 20); }

  _tick() {
    this.t += 0.04;
    const t = this.t, burst = Math.max(0, Math.sin(t * 0.8)) ** 12;
    if (this.stream) {
      const f = [Math.sin(t * 7) * 180 * burst, Math.cos(t * 7) * 150 * burst, Math.sin(t * 3) * 20, burst * 30];
      this._line(`F ${f.map((x) => Math.round(x)).join(' ')} ${Math.round(400 * burst)}`);
    }
    if (Math.abs((t % 7.85) - 2) < 0.02) {
      const trained = this.g.map((g, i) => [g, i]).filter(([g]) => g.c > 0);
      if (trained.length) {
        const [g, i] = trained[Math.floor(Math.random() * trained.length)];
        this._emit(`SEG 48`);
        this._emit(`MATCH ${i} 0.21 0.74 1`);
        this._emit(`CAST ${i} ${g.b} ${g.b >= 0 ? 1 : 0}`);
      }
    }
  }

  _info() {
    return 'INFO ' + JSON.stringify({
      name: this.s.name, fw: '0.1.0-demo', bat: 76, volts: 3.92, chg: 0, axis: this.s.axis, thr: this.s.thr,
      sleep: this.s.sleep, wake: this.s.wake, haptics: this.s.haptics, motor: 1, irrx: 1,
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
        if (a2) this.g[i].n = a2;
        this._emit(`REC ${i} armed`);
        setTimeout(() => {
          const g = this.g[i];
          g.c = Math.min(4, g.c + 1);
          if (!g.n) g.n = `move${i}`;
          this._line(`SEG 41`);
          this._line(`REC ${i} ok ${g.c} ${(0.45 + g.c * 0.03).toFixed(2)}`);
        }, 2200);
        break;
      case 'GNAME': this.g[i].n = a2; this._emit(`OK GNAME ${i}`); break;
      case 'GCLR': this.g[i] = { n: '', c: 0, t: 0.45, b: this.g[i].b }; this._emit(`OK GCLR ${i}`); break;
      case 'LEARN':
        this._emit(`LEARN ${i} waiting`);
        setTimeout(() => {
          const d = Array.from({ length: 67 }, (_, k) => (k % 2 ? 560 + (k % 3) * 565 : 560));
          this.c[i] = { n: a2 || this.c[i].n || `code${i}`, k: 38, l: d.length, d };
          this._line(`LEARN ${i} ok ${d.length}`);
        }, 2500);
        break;
      case 'CODE': {
        const d = rest[1].split(',').map(Number);
        this.c[i] = { n: rest[0] === '-' ? '' : rest[0], k: +a2, l: d.length, d };
        this._emit(`OK CODE ${i} ${d.length}`);
        break;
      }
      case 'DUMPC': this._emit(`CODE ${i} ${this.c[i].k} ${this.c[i].n || '-'} ${this.c[i].d.join(',')}`); break;
      case 'CNAME': this.c[i].n = a2; this._emit(`OK CNAME ${i}`); break;
      case 'CCLR': this.c[i] = { n: '', k: 0, l: 0, d: [] }; this.g.forEach((g) => g.b === i && (g.b = -1)); this._emit(`OK CCLR ${i}`); break;
      case 'SEND': this._emit(this.c[i]?.k ? `OK SEND ${i}` : 'ERR SEND empty'); break;
      case 'TRY': this._emit('OK TRY'); break;
      case 'BIND': this.g[i].b = a2 === '-' ? -1 : +a2; this._emit(`OK BIND ${i} ${this.g[i].b}`); break;
      case 'SET':
        if (a1 === 'name') { this.s.name = a2 === '-' ? 'Wand-DEMO' : `Wand-${a2}`; this._emit(`OK SET name ${this.s.name} (restarting)`); break; }
        this.s[a1] = +a2; this._emit(`OK SET ${a1} ${a2}`); break;
      case 'CAL': this._emit('CAL hold the wand still, tip pointing straight up'); setTimeout(() => this._line('CAL ok 0.012 0.998 -0.051'), 1500); break;
      case 'BUZZ': this._emit('OK BUZZ'); break;
      case 'BAT': this._emit('BAT 76 3.92 0'); break;
      case 'SLEEP': this._emit('SLEEP requested'); setTimeout(() => this.disconnect(), 300); break;
      case 'RESET': this._emit(a1 === 'yes' ? 'OK RESET' : 'ERR type RESET yes'); break;
      case 'PING': this._emit('PONG'); break;
      default: this._emit(`ERR unknown ${cmd}`);
    }
    return Promise.resolve();
  }
}
