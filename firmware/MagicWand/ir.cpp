#include "ir.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

// Transmit: the 38 kHz carrier is bit-banged. Timing uses the Cortex-M33
// cycle counter (DWT->CYCCNT), which we switch on ourselves and CHECK at boot:
// the previous version trusted getCPUCycleCount() and, if that counter wasn't
// running, looped forever with interrupts off (wand frozen, button dead).
// If the counter doesn't work, sending falls back to delayMicroseconds()
// timing, which is less exact but can't hang.
//
// Why not micros(): on this core it comes from the 32 kHz sleep timer, so it
// moves in 30.5 us steps. And every space between marks used to run with
// interrupts on, so the Bluetooth stack could cut in and stretch a "0" into
// a "1". Now each frame is sent with interrupts off and every edge is placed
// at an absolute cycle-counter time from the start of the frame. Interrupts
// come back between repeated frames (Bluetooth just misses a few connection
// events, ~100 ms at most; the link survives that).
//
// Learning timestamps edges with the cycle counter too, and keeps the chip
// out of deep sleep (EM2) while it listens: in EM2 the counter stops.

#ifndef IR_SEND_IRQ_OFF
#define IR_SEND_IRQ_OFF 1
#endif
#ifndef IR_HOLD_MS
#define IR_HOLD_MS 300
#endif

namespace ir {

static bool cycOk = false;
static uint32_t cyclesPerUs = 0;
static uint32_t measuredPerUs = 0;

static void initCycleCounter() {
#if defined(DCB_DEMCR_TRCENA_Msk)
  DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;
#else
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
#endif
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  uint32_t expect = SystemCoreClock / 1000000;  // cycles per µs
  uint32_t c0 = DWT->CYCCNT;
  delayMicroseconds(1000);
  uint32_t got = (DWT->CYCCNT - c0) / 1000;
  measuredPerUs = got;
  // Counter must run, and roughly agree with the clock the core reports.
  cycOk = expect >= 8 && got > expect / 2 && got < expect * 2;
  // Use the core's clock figure for timing (delayMicroseconds, used to measure,
  // can be a few % off; the measurement only proves the counter runs).
  cyclesPerUs = cycOk ? expect : 0;
}

void begin() {
  pinMode(PIN_IR_LED, OUTPUT);
  digitalWrite(PIN_IR_LED, LOW);
#if HAS_IR_RECEIVER
#ifdef PIN_IR_RECV_PWR
  pinMode(PIN_IR_RECV_PWR, OUTPUT);
  digitalWrite(PIN_IR_RECV_PWR, LOW);
#endif
  pinMode(PIN_IR_RECV, INPUT_PULLUP);
#endif
  initCycleCounter();
}

const char* timingInfo() {
  static char buf[40];
  if (cycOk) snprintf(buf, sizeof(buf), "cycle counter %lu MHz (measured %lu)", (unsigned long)cyclesPerUs, (unsigned long)measuredPerUs);
  else snprintf(buf, sizeof(buf), "delay fallback");
  return buf;
}

static void markDelay(uint32_t us, uint16_t khz) {
  uint32_t periodUs = 1000 / khz;                  // 26 µs at 38 kHz
  uint32_t onUs = periodUs * IR_DUTY_PERCENT / 100;
  uint32_t offUs = periodUs - onUs;
  if (offUs > 2) offUs -= 2;                       // digitalWrite overhead
  uint32_t n = us / periodUs;
  for (uint32_t i = 0; i < n; i++) {
    digitalWrite(PIN_IR_LED, HIGH);
    delayMicroseconds(onUs);
    digitalWrite(PIN_IR_LED, LOW);
    delayMicroseconds(offUs);
  }
}

static inline void waitUntil(uint32_t t0, uint32_t at) {
  while ((uint32_t)(DWT->CYCCNT - t0) < at) {
  }
}

// Walk every duration of a code: emit(us, isMark).
template <typename F>
static int walk(const IrCode& c, F emit) {
  int count = 0;
  int reps = c.repeats < 1 ? 1 : (c.repeats > IR_MAX_REPEATS ? IR_MAX_REPEATS : c.repeats);
  for (int r = 0; r < reps; r++) {
    if (r > 0) { emit(c.gap, false); count++; }
    if (c.hdrMark) {
      emit(c.hdrMark, true);
      emit(c.hdrSpace, false);
      count += 2;
    }
    for (int b = 0; b < c.nbits; b++) {
      bool one = (c.data[b / 8] >> (7 - b % 8)) & 1;
      if (!c.pw) {
        emit(c.bitMark, true);
        emit(one ? c.one : c.zero, false);
        count += 2;
      } else {
        emit(one ? c.one : c.zero, true);
        count++;
        if (b < c.nbits - 1) { emit(c.bitMark, false); count++; }
      }
    }
    if (!c.pw) { emit(c.bitMark, true); count++; }  // stop bit
  }
  return count;
}

// One frame (or an NEC repeat burst), interrupts off while it goes out.
static void transmit(const IrCode& f) {
  if (!cycOk) {  // fallback: plain delays (less exact, can't hang)
    walk(f, [&](uint32_t us, bool isMark) {
      if (isMark) markDelay(us, f.khz);
      else delayMicroseconds(us);
    });
    digitalWrite(PIN_IR_LED, LOW);
    return;
  }
  const uint32_t period = cyclesPerUs * 1000 / f.khz;
  const uint32_t onCycles = period * IR_DUTY_PERCENT / 100;
#if IR_SEND_IRQ_OFF
  noInterrupts();
#endif
  const uint32_t t0 = DWT->CYCCNT;
  uint32_t at = 0;  // where this edge belongs, in cycles from t0
  walk(f, [&](uint32_t us, bool isMark) {
    uint32_t end = at + us * cyclesPerUs;
    if (isMark) {
      for (uint32_t t = at; t + period <= end; t += period) {
        digitalWrite(PIN_IR_LED, HIGH);
        waitUntil(t0, t + onCycles);
        digitalWrite(PIN_IR_LED, LOW);
        waitUntil(t0, t + period);
      }
    }
    waitUntil(t0, end);
    at = end;
  });
  digitalWrite(PIN_IR_LED, LOW);
#if IR_SEND_IRQ_OFF
  interrupts();
#endif
}

// Pause between frames, interrupts on (timing not critical).
static void pauseUs(uint32_t us) {
  if (!cycOk) { delayMicroseconds(us); return; }
  uint32_t t0 = DWT->CYCCNT;
  waitUntil(t0, us * cyclesPerUs);
}

// NEC / LG: 9 ms + 4.5 ms header, 32 bits. A held NEC remote sends the frame
// once, then short "repeat" bursts (9 ms, 2.25 ms, 560 us) every 108 ms.
static bool isNec(const IrCode& c) {
  return !c.pw && c.nbits == 32 && c.hdrMark > 8000 && c.hdrMark < 10000 && c.hdrSpace > 3500 && c.hdrSpace < 5500;
}

void send(const IrCode& code) {
  if (code.khz < 20 || code.khz > 60 || code.nbits == 0) return;
  int reps = code.repeats < 1 ? 1 : (code.repeats > IR_MAX_REPEATS ? IR_MAX_REPEATS : code.repeats);
  IrCode frame = code;  // one frame at a time; the gap between them is ours
  frame.repeats = 1;
  uint32_t gap = code.gap ? code.gap : 40000;

  uint32_t start = millis();
  for (int r = 0; r < reps; r++) {
    if (r > 0) pauseUs(gap);
    transmit(frame);
  }

  // Then keep going like a button held for IR_HOLD_MS: what a real remote
  // does on a normal press, and much easier to see/receive than one frame.
  // NEC gets repeat bursts (a TV reads them as "still held", not as new
  // presses); other protocols repeat the whole frame, as their remotes do.
  if (isNec(code)) {
    IrCode burst = {};
    burst.khz = code.khz;
    burst.hdrMark = code.hdrMark;
    burst.hdrSpace = code.hdrSpace / 2;  // 2.25 ms
    burst.bitMark = code.bitMark;        // nbits 0: header + stop mark only
    burst.repeats = 1;
    uint32_t wait = gap;                 // after the full frame: ~40 ms
    while (millis() - start < IR_HOLD_MS) {
      pauseUs(wait);
      transmit(burst);
      wait = 96000;                      // bursts every 108 ms (start to start)
    }
  } else {
    while (millis() - start < IR_HOLD_MS) {
      pauseUs(gap);
      transmit(frame);
    }
  }
  digitalWrite(PIN_IR_LED, LOW);
}

int rawLength(const IrCode& c) {
  if (!c.khz || !c.nbits) return 0;
  return walk(c, [](uint32_t, bool) {});
}

int toRaw(const IrCode& c, uint16_t* out, int max) {
  int n = 0;
  if (!c.khz || !c.nbits) return 0;
  walk(c, [&](uint32_t us, bool) {
    if (n < max) out[n++] = us > 65535 ? 65535 : us;
  });
  return n;
}

void hex(const IrCode& c, char* out, size_t cap) {
  static const char* H = "0123456789ABCDEF";
  size_t k = 0;
  int nibbles = (c.nbits + 3) / 4;
  for (int i = 0; i < nibbles && k + 1 < cap; i++) {
    uint8_t byte = c.data[i / 2];
    out[k++] = H[(i % 2 == 0) ? byte >> 4 : byte & 0xF];
  }
  out[k] = 0;
}

bool fromHex(const char* s, IrCode& c) {
  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
  memset(c.data, 0, sizeof(c.data));
  int nib = 0;
  for (; *s; s++) {
    char ch = *s;
    int v = (ch >= '0' && ch <= '9') ? ch - '0' : (ch >= 'a' && ch <= 'f') ? ch - 'a' + 10
          : (ch >= 'A' && ch <= 'F') ? ch - 'A' + 10 : -1;
    if (v < 0) return false;
    if (nib >= IR_MAX_BYTES * 2 || nib * 4 + 4 > 255) return false;
    c.data[nib / 2] |= (nib % 2 == 0) ? v << 4 : v;
    nib++;
  }
  c.nbits = nib * 4;
  return nib > 0;
}

bool setProtocol(const char* name, IrCode& c) {
  struct P { const char* n; uint16_t khz, hm, hs, bm, one, zero, gap; uint8_t pw, rep; };
  static const P kP[] = {
      {"nec", 38, 9000, 4500, 560, 1690, 560, 40000, 0, 1},      // LG TV and most NEC
      {"samsung", 38, 4500, 4500, 560, 1690, 560, 47000, 0, 1},  // Samsung TV
      {"coolix", 38, 4692, 4692, 552, 1656, 552, 5244, 0, 2},    // Midea & co. ACs
      {"sony", 40, 2400, 600, 600, 1200, 600, 25000, 1, 3},      // Sony SIRC
  };
  for (const P& p : kP) {
    if (strcasecmp(name, p.n)) continue;
    c.khz = p.khz; c.hdrMark = p.hm; c.hdrSpace = p.hs; c.bitMark = p.bm;
    c.one = p.one; c.zero = p.zero; c.gap = p.gap; c.pw = p.pw; c.repeats = p.rep;
    return true;
  }
  return false;
}

// ------------------------------------------------------------------ decode
// One frame = d[s..e), starting with a mark. Fills timings + bits of `f`.
static bool decodeFrame(const uint16_t* d, int s, int e, IrCode& f) {
  memset(f.data, 0, sizeof(f.data));
  f.hdrMark = f.hdrSpace = 0;
  if (e - s >= 2 && d[s] > 2000) {
    f.hdrMark = d[s];
    f.hdrSpace = d[s + 1];
    s += 2;
  }
  uint16_t sMin = 65535, sMax = 0, mMin = 65535, mMax = 0;
  for (int i = s; i < e; i += 2) {
    if (d[i] < mMin) mMin = d[i];
    if (d[i] > mMax) mMax = d[i];
    if (i + 1 < e) {
      if (d[i + 1] < sMin) sMin = d[i + 1];
      if (d[i + 1] > sMax) sMax = d[i + 1];
    }
  }
  if (mMax == 0) return false;
  if (sMax > sMin * 1.6f) f.pw = 0;
  else if (mMax > mMin * 1.6f) f.pw = 1;
  else return false;

  uint32_t thr = f.pw ? (mMin + mMax) / 2 : (sMin + sMax) / 2;
  uint32_t sumOne = 0, sumZero = 0, sumFix = 0, nOne = 0, nZero = 0, nFix = 0;
  int bits = 0;
  for (int i = s; i < e; i += 2) {
    uint16_t v, fixed;
    if (!f.pw) {
      if (i + 1 >= e) break;              // stop bit
      v = d[i + 1]; fixed = d[i];
    } else {
      v = d[i]; fixed = (i + 1 < e) ? d[i + 1] : 0;
    }
    if (bits >= 255 || bits >= IR_MAX_BYTES * 8) return false;
    bool one = v > thr;
    if (one) { f.data[bits / 8] |= 0x80 >> (bits % 8); sumOne += v; nOne++; }
    else { sumZero += v; nZero++; }
    if (fixed) { sumFix += fixed; nFix++; }
    bits++;
  }
  if (bits < 4) return false;
  f.nbits = bits;
  f.bitMark = nFix ? sumFix / nFix : 560;
  f.one = nOne ? sumOne / nOne : f.bitMark * 3;
  f.zero = nZero ? sumZero / nZero : f.bitMark;
  return true;
}

static bool sameBits(const IrCode& a, const IrCode& b) {
  return a.nbits == b.nbits && a.pw == b.pw && (a.hdrMark != 0) == (b.hdrMark != 0) &&
         memcmp(a.data, b.data, (a.nbits + 7) / 8) == 0;
}

bool decode(const uint16_t* d, int n, uint16_t khz, IrCode& out) {
  // Split into frames at long spaces (> 5 ms) or at a new header mark (> 2 ms).
  static IrCode f;
  bool have = false;
  int start = 0;
  uint16_t gapBefore = 0;
  char name[sizeof(out.name)];
  memcpy(name, out.name, sizeof(name));
  for (int i = 0; i <= n; i++) {
    bool gap = i < n && (i % 2 == 1) && d[i] > 5000;
    bool hdr = i < n && (i % 2 == 0) && i > start && d[i] > 2000;
    if (!(i == n || gap || hdr)) continue;
    int end = hdr ? i - 1 : i;             // frames always end on a mark
    if (end - start >= 3) {
      memset(&f, 0, sizeof(f));
      if (decodeFrame(d, start, end, f)) {
        if (!have && f.nbits >= 8) {
          out = f;
          out.repeats = 1;
          out.gap = 0;
          have = true;
        } else if (have && sameBits(out, f) && out.repeats < IR_MAX_REPEATS) {
          if (out.repeats == 1) out.gap = gapBefore;
          out.repeats++;
        } else if (have) {
          break;                           // different frame: stop here
        }
      } else if (have) {
        break;                             // e.g. an NEC "repeat" burst
      }
    }
    if (gap) { gapBefore = d[i]; start = i + 1; }
    else if (hdr) { gapBefore = d[i - 1]; start = i; }
  }
  memcpy(out.name, name, sizeof(name));
  if (!have) return false;
  out.khz = khz;
  if (!out.gap) out.gap = 40000;
  return true;
}

// ------------------------------------------------------------------ learn
static volatile uint32_t edges[IR_CAPTURE_EDGES];
static volatile int nEdges = 0;
static LearnState state = kIdle;
static uint32_t learnStart = 0;
static uint16_t raw[IR_CAPTURE_EDGES];

// Edge time: cycle counter if it works (exact), else micros() (30 us steps).
static inline uint32_t stamp() { return cycOk ? DWT->CYCCNT : micros(); }
static inline uint32_t ticksPerUs() { return cycOk ? cyclesPerUs : 1; }

static void onEdge() {
  if (nEdges < IR_CAPTURE_EDGES) edges[nEdges++] = stamp();  // extra edges (held button) are ignored
}

// Stay out of EM2 while listening (the cycle counter stops there, and waking
// from it adds latency to every edge).
static bool holdingEm1 = false;
static void holdAwake(bool on) {
#if defined(SL_CATALOG_POWER_MANAGER_PRESENT)
  if (on && !holdingEm1) sl_power_manager_add_em_requirement(SL_POWER_MANAGER_EM1);
  if (!on && holdingEm1) sl_power_manager_remove_em_requirement(SL_POWER_MANAGER_EM1);
#endif
  holdingEm1 = on;
}

void startLearn() {
#if HAS_IR_RECEIVER
#ifdef PIN_IR_RECV_PWR
  digitalWrite(PIN_IR_RECV_PWR, HIGH);
  delay(50);  // receiver settling after power-up
#endif
  holdAwake(true);
  nEdges = 0;
  attachInterrupt(digitalPinToInterrupt(PIN_IR_RECV), onEdge, CHANGE);
  state = kWaiting;
  learnStart = millis();
#else
  state = kTimeout;
#endif
}

void stopLearn() {
#if HAS_IR_RECEIVER
  detachInterrupt(digitalPinToInterrupt(PIN_IR_RECV));
#ifdef PIN_IR_RECV_PWR
  digitalWrite(PIN_IR_RECV_PWR, LOW);
#endif
#endif
  holdAwake(false);
  if (state == kWaiting || state == kCapturing) state = kIdle;
}

LearnState pollLearn(IrCode& out) {
  if (state == kWaiting) {
    if (nEdges > 0) state = kCapturing;
    else if (millis() - learnStart > IR_LEARN_TIMEOUT_MS) {
      stopLearn();
      state = kTimeout;
    }
  }
  if (state == kCapturing) {
    noInterrupts();
    int n = nEdges;
    uint32_t last = n ? edges[n - 1] : 0;
    interrupts();
    if (n >= IR_CAPTURE_EDGES || stamp() - last > (uint32_t)IR_END_GAP_US * ticksPerUs()) {
      stopLearn();
      int nd = n - 1;
      for (int i = 0; i < nd; i++) {
        int32_t d = (int32_t)((edges[i + 1] - edges[i]) / ticksPerUs());
        d += (i % 2 == 0) ? -IR_MARK_EXCESS_US : IR_MARK_EXCESS_US;  // receiver stretches marks
        raw[i] = (uint16_t)(d < 1 ? 1 : (d > 65535 ? 65535 : d));
      }
      if (nd < 8) state = kTimeout;
      else state = decode(raw, nd, IR_CARRIER_KHZ, out) ? kDone : kUnsupported;
    }
  }
  LearnState s = state;
  if (s == kDone || s == kTimeout || s == kUnsupported) state = kIdle;
  return s;
}

}  // namespace ir
