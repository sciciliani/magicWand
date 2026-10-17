#include "ir.h"

#include <Arduino.h>
#include <em_cmu.h>
#include <em_gpio.h>
#include <em_timer.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

// Transmit (0.5.0): the carrier comes from a hardware TIMER in PWM mode (see
// "hardware carrier" below): exact frequency and duty from the crystal clock,
// whatever the CPU is doing. The CPU only places the mark/space edges. If no
// timer can drive the LED pin (checked at boot), or IRMODE sw is set, the
// carrier is bit-banged as before:
//
// Software carrier: the 38 kHz carrier is bit-banged. Timing uses the Cortex-M33
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

// One IR LED pin (PIN_IR_LED) driving the LED driver board's input.
// XIAO MG24: D0 = PC0 ... D7 = PC7 (variant pin table), so the Arduino pin
// number is the port C bit. D1 is never allowed: LOW on D1 at reset = the
// bootloader's upload-recovery loop.
static_assert(PIN_IR_LED <= D7 && PIN_IR_LED != D1, "PIN_IR_LED must be D0 or D2..D7 (port C), never D1");
static const uint8_t kLedPin = PIN_IR_LED;   // port C pin number
static const uint32_t ledMask = 1u << kLedPin;

uint32_t pinMask() { return ledMask; }

// Strong push-pull drive, starting LOW, written straight to the GPIO registers
// (the core's pinMode() is a no-op until the core has finished its own init).
static void ledPinsOutput() { GPIO_PinModeSet(gpioPortC, kLedPin, gpioModePushPull, 0); }

static inline void ledOn() { GPIO_PortOutSet(gpioPortC, ledMask); }
void ledOff() { GPIO_PortOutClear(gpioPortC, ledMask); }

void forceOff() {
  ledOff();
  ledPinsOutput();
}

// ------------------------------------------------------------------ hardware carrier
// A TIMER counts the peripheral clock in PWM mode: TOP = one carrier period,
// compare value = the on-time (IR_DUTY_PERCENT). Channel CC0 is routed to the
// IR LED pin. The carrier runs all through a
// send; the CPU only switches the route on for each mark and off for each space
// (route off = the pin goes back to its GPIO value, which is LOW). Interrupts
// can no longer bend the carrier itself; at worst they move an edge.
//
// Which timer can reach which pin differs per chip, so at boot each candidate
// is tried on the LED pin for ~0.3 ms and the pin is read back: the first
// timer that really toggles it is kept. None = software carrier (the BOOT line
// "IR timing:" says which). TIMER0 is left to the core (analogWrite, LED PWM).
#ifndef IR_HW_CARRIER
#define IR_HW_CARRIER 1
#endif

struct HwTimer {
  TIMER_TypeDef* t;
  CMU_Clock_TypeDef clk;
  uint8_t idx;
  const char* name;
};
static const HwTimer kHwTimers[] = {
    {TIMER1, cmuClock_TIMER1, 1, "TIMER1"},
    {TIMER2, cmuClock_TIMER2, 2, "TIMER2"},
    {TIMER3, cmuClock_TIMER3, 3, "TIMER3"},
    {TIMER4, cmuClock_TIMER4, 4, "TIMER4"},
};
static const HwTimer* hw = nullptr;  // timer that passed the boot check
static bool hwWanted = true;         // IRMODE hw|sw
static uint32_t hwTop = 0;
static uint32_t hwClockHz = 0;
static uint16_t hwKhz = 0;

// Carrier at `khz` on CC0, counter running, not routed to the pin yet.
static bool hwConfigure(const HwTimer& h, uint16_t khz) {
  CMU_ClockEnable(h.clk, true);
  uint32_t f = CMU_ClockFreqGet(h.clk);
  uint32_t period = (f + khz * 500u) / (khz * 1000u);  // timer ticks per carrier cycle
  if (period < 20 || period > 65536) return false;    // TIMER1..4 are 16-bit
  TIMER_Init_TypeDef init = TIMER_INIT_DEFAULT;
  init.enable = false;
  TIMER_Init(h.t, &init);
  TIMER_InitCC_TypeDef cc = TIMER_INITCC_DEFAULT;
  cc.mode = timerCCModePWM;  // output set at overflow, cleared at compare match
  TIMER_InitCC(h.t, 0, &cc);
  hwTop = period - 1;
  TIMER_TopSet(h.t, hwTop);
  uint32_t on = (period * IR_DUTY_PERCENT + 50) / 100;
  TIMER_CompareSet(h.t, 0, on);
  TIMER_Enable(h.t, true);
  hwClockHz = f;
  hwKhz = khz;
  return true;
}

// CC0 -> IR LED pin (port C). The route stays disabled until a mark.
static void hwRoute(const HwTimer& h) {
  GPIO->TIMERROUTE[h.idx].CC0ROUTE = ((uint32_t)gpioPortC << _GPIO_TIMER_CC0ROUTE_PORT_SHIFT) |
                                     ((uint32_t)kLedPin << _GPIO_TIMER_CC0ROUTE_PIN_SHIFT);
}

// Mark: restart the carrier cycle (next tick overflows = output HIGH) and
// connect the timer to the pins. Space: disconnect, pins fall back to LOW.
static inline void hwMarkOn() {
  hw->t->CNT = hwTop;
  GPIO->TIMERROUTE_SET[hw->idx].ROUTEEN = GPIO_TIMER_ROUTEEN_CC0PEN;
}
static inline void hwMarkOff() { GPIO->TIMERROUTE_CLR[hw->idx].ROUTEEN = GPIO_TIMER_ROUTEEN_CC0PEN; }

static void hwStop() {
  if (!hw) return;
  hwMarkOff();
  TIMER_Enable(hw->t, false);
  CMU_ClockEnable(hw->clk, false);
  ledOff();
}

static bool hwStart(uint16_t khz) {
  if (!hw || !hwConfigure(*hw, khz)) return false;
  hwRoute(*hw);
  return true;
}

// Boot check: does this timer really toggle the LED pin?
static bool hwProbe(const HwTimer& h) {
  const uint8_t pin = kLedPin;
  hw = &h;
  bool ok = false;
  if (hwStart(IR_CARRIER_KHZ)) {
    hwMarkOn();
    int toggles = 0;
    unsigned last = GPIO_PinInGet(gpioPortC, pin);
    uint32_t t0 = cycOk ? DWT->CYCCNT : 0;
    for (uint32_t i = 0; i < 200000 && toggles < 40; i++) {
      unsigned v = GPIO_PinInGet(gpioPortC, pin);
      if (v != last) { toggles++; last = v; }
      if (cycOk && (uint32_t)(DWT->CYCCNT - t0) > 300 * cyclesPerUs) break;
    }
    ok = toggles >= 10;  // 0.3 ms at 38 kHz = ~22 toggles
  }
  hwStop();
  hw = nullptr;
  return ok;
}

static void hwFind() {
  hw = nullptr;
#if IR_HW_CARRIER
  for (const HwTimer& h : kHwTimers) {
    if (hwProbe(h)) { hw = &h; break; }
  }
#endif
}

static inline bool useHw() { return hw && hwWanted; }

bool setHwCarrier(bool on) {
  hwWanted = on;
  return useHw();
}
bool hwCarrier() { return useHw(); }

void begin() {
  forceOff();
#if HAS_IR_RECEIVER
#ifdef PIN_IR_RECV_PWR
  pinMode(PIN_IR_RECV_PWR, OUTPUT);
  digitalWrite(PIN_IR_RECV_PWR, LOW);
#endif
  pinMode(PIN_IR_RECV, INPUT_PULLUP);
#endif
  initCycleCounter();
  hwFind();
}

const char* timingInfo() {
  static char buf[110];
  int n;
  if (cycOk) n = snprintf(buf, sizeof(buf), "cycle counter %lu MHz (measured %lu)", (unsigned long)cyclesPerUs, (unsigned long)measuredPerUs);
  else n = snprintf(buf, sizeof(buf), "delay fallback");
  if (n < 0 || n >= (int)sizeof(buf)) return buf;
  if (useHw()) snprintf(buf + n, sizeof(buf) - n, ", carrier HW %s", hw->name);
  else if (hw) snprintf(buf + n, sizeof(buf) - n, ", carrier SW (IRMODE sw; HW %s available)", hw->name);
  else snprintf(buf + n, sizeof(buf) - n, ", carrier SW (no timer reached the LED pin)");
  return buf;
}

const char* carrierInfo() {
  static char buf[64];
  if (!useHw()) return "SW (bit-banged)";
  if (hwKhz) {
    snprintf(buf, sizeof(buf), "HW %s %lu Hz clock, %u ticks/cycle", hw->name, (unsigned long)hwClockHz, (unsigned)(hwTop + 1));
    return buf;
  }
  snprintf(buf, sizeof(buf), "HW %s", hw->name);
  return buf;
}

static void markDelay(uint32_t us, uint16_t khz) {
  uint32_t periodUs = 1000 / khz;                  // 26 µs at 38 kHz
  uint32_t onUs = periodUs * IR_DUTY_PERCENT / 100;
  uint32_t offUs = periodUs - onUs;
  if (offUs > 2) offUs -= 2;                       // digitalWrite overhead
  uint32_t n = us / periodUs;
  for (uint32_t i = 0; i < n; i++) {
    ledOn();
    delayMicroseconds(onUs);
    ledOff();
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
    ledOff();
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
        ledOn();
        waitUntil(t0, t + onCycles);
        ledOff();
        waitUntil(t0, t + period);
      }
    }
    waitUntil(t0, end);
    at = end;
  });
  ledOff();
#if IR_SEND_IRQ_OFF
  interrupts();
#endif
}

// Hardware carrier: the timer already runs (hwStart); here only the edges.
static void transmitHw(const IrCode& f) {
  if (!cycOk) {
    walk(f, [&](uint32_t us, bool isMark) {
      if (isMark) hwMarkOn();
      delayMicroseconds(us);
      hwMarkOff();
    });
    return;
  }
#if IR_SEND_IRQ_OFF
  noInterrupts();
#endif
  const uint32_t t0 = DWT->CYCCNT;
  uint32_t at = 0;
  walk(f, [&](uint32_t us, bool isMark) {
    uint32_t end = at + us * cyclesPerUs;
    if (isMark) hwMarkOn();
    waitUntil(t0, end);
    if (isMark) hwMarkOff();
    at = end;
  });
  hwMarkOff();
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

// Send the frame as many times as the remote did (the "x2" IrDump prints).
void send(const IrCode& code) {
  if (code.khz < 20 || code.khz > 60 || code.nbits == 0) return;
  ledPinsOutput();  // in case a library re-purposed one of them (D4/D5 = I2C pins)
  int reps = code.repeats < 1 ? 1 : (code.repeats > IR_MAX_REPEATS ? IR_MAX_REPEATS : code.repeats);
  IrCode frame = code;
  frame.repeats = 1;
  bool useTimer = useHw() && hwStart(code.khz);
  for (int r = 0; r < reps; r++) {
    if (r > 0) pauseUs(code.gap ? code.gap : 40000);
    if (useTimer) transmitHw(frame);
    else transmit(frame);
  }
  if (useTimer) hwStop();
  ledOff();
}

// Continuous carrier for `ms` (IRTEST): for a meter across R3, a logic
// analyzer, or a phone camera. Software mode runs in 10 ms chunks with
// interrupts off, like a long mark.
void carrierTest(uint16_t khz, uint32_t ms) {
  if (khz < 20 || khz > 60 || ms == 0) return;
  if (ms > 5000) ms = 5000;
  ledPinsOutput();
  if (useHw() && hwStart(khz)) {
    hwMarkOn();
    delay(ms);
    hwStop();
    return;
  }
  IrCode m;
  memset(&m, 0, sizeof(m));
  m.khz = khz;
  for (uint32_t done = 0; done < ms; done += 10) {
    // one "frame" = a single 10 ms mark: header mark only, no bits
    m.hdrMark = 10000;
    m.hdrSpace = 1;
    m.nbits = 0;
    m.repeats = 1;
    transmit(m);
  }
  ledOff();
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
      {"rca", IR_RCA_KHZ, 4000, 4000, 500, 2000, 1000, 8000, 0, 2},  // RCA: 12 bits + inverted copy, 2 frames
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

// Same as IrDump: split into frames at long spaces (> 5 ms) or at a new
// header mark (> 2 ms) and decode each one. Keep the most complete frame
// (most bits: a press caught half-way gives a short first frame) and count
// how many times it was received: IrDump's "0xB24D7B84E01F x2".
bool decode(const uint16_t* d, int n, uint16_t khz, IrCode& out) {
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
    int end = hdr ? i - 1 : i;  // frames always end on a mark
    memset(&f, 0, sizeof(f));
    if (end - start >= 3 && decodeFrame(d, start, end, f) && f.nbits >= 8) {
      if (!have || f.nbits > out.nbits) {  // first, or more complete than what we had
        out = f;
        out.repeats = 1;
        out.gap = 0;
        have = true;
      } else if (sameBits(out, f) && out.repeats < IR_MAX_REPEATS) {
        if (out.repeats == 1) out.gap = gapBefore;
        out.repeats++;
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
// The receiver can't measure a remote's carrier (it only outputs the decoded
// on/off envelope), so guess it from the format: RCA (4 ms + 4 ms header,
// 24 bits = 12 bits + their inverse) gets IR_RCA_KHZ and Sony (2.4 ms header,
// pulse-width) 40 kHz; everything else IR_CARRIER_KHZ. If a learned code only
// works up close, the guess is wrong: fix it with KHZ <c> <khz>.
static uint16_t carrierFor(const IrCode& c) {
  if (!c.pw && c.nbits == 24 && c.hdrMark > 3200 && c.hdrMark < 4800 && c.hdrSpace > 3200 && c.hdrSpace < 4800) {
    uint32_t v = ((uint32_t)c.data[0] << 16) | ((uint32_t)c.data[1] << 8) | c.data[2];
    if (((v >> 12) ^ (v & 0xFFF)) == 0xFFF) return IR_RCA_KHZ;
  }
  if (c.pw && c.hdrMark > 1900 && c.hdrMark < 2900) return 40;
  return IR_CARRIER_KHZ;
}

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

// Power the receiver ahead of time (at the start of the countdown) so it is
// settled when capture starts, like IrDump where it is always on.
static bool rxPowered = false;

void receiverOn() {
#if HAS_IR_RECEIVER && defined(PIN_IR_RECV_PWR)
  digitalWrite(PIN_IR_RECV_PWR, HIGH);
#endif
  rxPowered = true;
}

void startLearn() {
#if HAS_IR_RECEIVER
#ifdef PIN_IR_RECV_PWR
  if (!rxPowered) {
    receiverOn();
    delay(50);  // receiver settling after power-up
  }
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
  rxPowered = false;
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
      else if (!decode(raw, nd, IR_CARRIER_KHZ, out)) state = kUnsupported;
      else {
        out.khz = carrierFor(out);
        state = kDone;
      }
    }
  }
  LearnState s = state;
  if (s == kDone || s == kTimeout || s == kUnsupported) state = kIdle;
  return s;
}

}  // namespace ir
