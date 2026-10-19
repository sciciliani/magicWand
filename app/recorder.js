// recorder.js — one recorded attempt of a spell, shared by the setup and the
// Spells tab:
//
//   "Ready…"  "one ZigZag…"  -> REC <slot> <name> -> the wand ticks twice and
//   buzzes GO -> you do the move ONCE -> the wand stores it (REC ok).
//
// At the same time the app watches the motion stream and judges the move
// (training-moves.js). A wrong move is taken back on the wand (GUNDO), so only
// good samples stay. Moving before GO restarts the count: the wand must be
// still and centred before every spell.
import { MoveCatcher, measure, judge, describe } from './training-moves.js?v=6';

const BEAT_MS = 750;        // "Ready…" -> "one X…" -> REC
const GO_AFTER_ARMED = 600; // the wand's tick, tick, GO takes ~650 ms after REC
const WATCH_AFTER_GO = 300; // the GO buzz shakes the wand: ignore it
const MOVING = 90;          // energy above this while counting = moved too early
const SEG_WAIT_MS = 1600;   // after REC ok, wait this long for the app's own reading

export class Recorder {
  /** app: { send(line), log(text), changed(rec) } */
  constructor(app) {
    this.app = app;
    this.catcher = new MoveCatcher();
    this.phase = 'idle';  // idle | count | arming | go | reading
    this.beat = 0;
    this.timer = 0;
    this.gen = 0;
  }
  get busy() { return this.phase !== 'idle'; }

  /** One attempt. done(result): { kind: 'ok'|'bad'|'nothing'|'early'|'cancel', verdict, count, near } */
  attempt({ slot, wire, move = null }, done) {
    this.cancel(true);
    Object.assign(this, { slot, wire, move, done, seg: null, recOk: null });
    const g = ++this.gen;
    const later = (fn, ms) => { this.timer = setTimeout(() => { if (g === this.gen) fn(); }, ms); };
    this._set('count', 1);
    later(() => {
      this._set('count', 2);
      later(() => {
        this._set('arming', 2);
        this.app.send(`REC ${slot}${wire ? ' ' + wire : ''}`);
      }, BEAT_MS);
    }, BEAT_MS);
    this._later = later;
  }

  cancel(silent = false) {
    clearTimeout(this.timer);
    this.gen++;
    const was = this.phase !== 'idle';
    this.phase = 'idle';
    this.beat = 0;
    if (was && !silent) this._finish({ kind: 'cancel' });
  }

  _set(phase, beat = this.beat) {
    this.phase = phase;
    this.beat = beat;
    this.app.changed?.(this);
  }

  _finish(result) {
    clearTimeout(this.timer);
    this.gen++;
    this.phase = 'idle';
    this.beat = 0;
    const done = this.done;
    this.done = null;
    this.app.changed?.(this);
    done?.(result);
  }

  /** Every line from the wand. */
  feed(tag, p) {
    if (this.phase === 'idle') return;
    if (tag === 'F') return this._feature(p.slice(0, 5).map(Number));
    if (tag !== 'REC' || +p[0] !== this.slot) return;
    if (p[1] === 'armed' && this.phase === 'arming') {
      this._later(() => { this.goAt = Date.now(); this.catcher.reset(); this._set('go', 3); }, GO_AFTER_ARMED);
    } else if (p[1] === 'timeout') {
      this._finish({ kind: 'nothing' });
    } else if (p[1] === 'ok') {
      // REC g ok <count> <thr> near <g2> <dist> <thr2>
      const near = p[4] === 'near' ? { g: +p[5], dist: +p[6], thr: +p[7] } : { g: -1 };
      near.similar = near.g >= 0 && near.dist > 0 && near.dist < near.thr * 1.15;
      this.recOk = { count: +p[2], thr: +p[3], near };
      this._set('reading', 3);
      if (!this.move || this.seg) return this._verdict();
      this._later(() => this._verdict(), SEG_WAIT_MS);
    }
  }

  _feature(row) {
    if (this.phase === 'count') {
      // Moved before GO (REC not sent yet): start the count again.
      if (row[4] > MOVING) this._finish({ kind: 'early' });
      return;
    }
    if (this.phase === 'arming') return;  // REC sent: whatever comes next is judged
    if ((this.phase !== 'go' && this.phase !== 'reading') || Date.now() - this.goAt < WATCH_AFTER_GO) return;
    const seg = this.catcher.push(row);
    if (!seg || this.seg) return;
    this.seg = seg;
    if (this.recOk) this._verdict();
  }

  _verdict() {
    const r = this.recOk;
    if (!r) return;
    if (!this.move) return this._finish({ kind: 'ok', count: r.count, near: r.near });
    if (!this.seg) {  // the wand stored a move the app did not see whole: keep it (forgiving)
      this.app.log?.(`TRAIN ${this.move} kept (no reading on the app side)`);
      return this._finish({ kind: 'ok', count: r.count, near: r.near });
    }
    const m = measure(this.seg);
    const v = judge(this.move, m);
    this.app.log?.(`TRAIN ${this.move} ${v.ok ? 'YES' : 'NO (' + v.kind + ')'}: ${describe(m)}`);
    if (v.ok) return this._finish({ kind: 'ok', verdict: v, count: r.count, near: r.near });
    this.app.send(`GUNDO ${this.slot}`);  // only good samples stay on the wand
    this._finish({ kind: 'bad', verdict: v, count: Math.max(0, r.count - 1), near: r.near });
  }
}
