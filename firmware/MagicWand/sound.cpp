// sound.cpp — see sound.h.
#include "sound.h"

#include "config.h"

#if HAS_BUZZER
#include <Arduino.h>
#include <math.h>
#include <em_cmu.h>
#include <em_gpio.h>
#include <em_timer.h>
#include <string.h>
#include <strings.h>

#include "ir.h"
#include "sounds_data.h"

static_assert(PIN_BUZZER <= D7 && PIN_BUZZER != D1, "PIN_BUZZER must be D0 or D2..D7 (port C), never D1");

namespace sound {

struct Timer {
  TIMER_TypeDef* t;
  CMU_Clock_TypeDef clk;
  uint8_t idx;
  const char* name;
};
// TIMER0 belongs to the core (analogWrite); the IR carrier takes the first of
// TIMER1..4 that reaches its pin. Not every timer can reach every port on the
// MG24 (0.6.3 shipped with TIMER4 on D0 and stayed silent), so the buzzer
// tries them in this order and keeps the first one that really toggles the pin.
// TIMER3 first: it serves port C (D0..D7) and the IR normally takes TIMER1.
static const Timer kTimers[] = {
    {TIMER3, cmuClock_TIMER3, 3, "TIMER3"},
    {TIMER1, cmuClock_TIMER1, 1, "TIMER1"},
    {TIMER2, cmuClock_TIMER2, 2, "TIMER2"},
    {TIMER4, cmuClock_TIMER4, 4, "TIMER4"},
};
static const char* probeLog = "not probed";
static const uint8_t kPin = PIN_BUZZER;  // port C pin number (D0..D7 = PC0..PC7)

struct Entry { const BuzzTone* t; uint16_t n; const char* name; };
#define E(a, nm) {a, (uint16_t)(sizeof(a) / sizeof(a[0])), nm}
static const Entry kSounds[kNumSounds] = {
    E(kCastTones, "cast"),   E(kLightningTones, "lightning"), E(kGlowTones, "glow"),
    E(kErrorTones, "error"), E(kLearnedTones, "learned"),     E(kWakeTones, "wake"),
    E(kLowBatTones, "lowbat"),
};
#undef E

static const Timer* tm = nullptr;
static uint32_t tickHz = 0;   // timer counting rate after the prescaler
static Prefs pf = defaults();
static float pitchMul = 1.0f; // 2^(pitch/12)
static float gain = 1.0f;     // wave amplitude scale from the volume
static const BuzzTone* cur = nullptr;
static BuzzTone custom[200];  // tone() / sweep() steps
static uint16_t n = 0, i = 0;
static uint32_t stepStartUs = 0;

static void routeOff() {
  if (tm) GPIO->TIMERROUTE_CLR[tm->idx].ROUTEEN = GPIO_TIMER_ROUTEEN_CC0PEN;
  GPIO_PinOutClear(gpioPortC, kPin);  // pin LOW = buzzer driver off
}

// Volume: a square wave's fundamental is proportional to sin(pi * duty), so
// scaling that amplitude (not the duty itself) keeps the table's fades smooth
// at every volume. The volume slider is squared to follow the ear (50 % ~ -12 dB).
static float dutyFor(uint8_t duty) {
  float a = sinf(3.14159265f * duty / 100.0f) * gain;
  if (a > 1.0f) a = 1.0f;
  return asinf(a) / 3.14159265f;  // 0 .. 0.5
}

static void apply(const BuzzTone& s) {
  if (!tm || s.hz == 0 || s.duty == 0 || gain <= 0.0f) { routeOff(); return; }
  float hz = s.hz * pitchMul;
  if (hz < 100) hz = 100;
  if (hz > 12000) hz = 12000;
  uint32_t period = (uint32_t)(tickHz / hz);
  if (period < 4) period = 4;
  if (period > 65536) period = 65536;
  uint32_t cc = (uint32_t)(period * dutyFor(s.duty));
  if (cc < 1) cc = 1;
  if (!(GPIO->TIMERROUTE[tm->idx].ROUTEEN & GPIO_TIMER_ROUTEEN_CC0PEN)) {  // starting: set now
    TIMER_TopSet(tm->t, period - 1);
    TIMER_CompareSet(tm->t, 0, cc);
    TIMER_CounterSet(tm->t, 0);
    GPIO->TIMERROUTE_SET[tm->idx].ROUTEEN = GPIO_TIMER_ROUTEEN_CC0PEN;
  } else {  // playing: buffered, takes effect at the next wave cycle (no clicks)
    TIMER_TopBufSet(tm->t, period - 1);
    TIMER_CompareBufSet(tm->t, 0, cc);
  }
}

// Set up timer t for tones (prescaler so ~200 Hz still fits 16 bits), CC0 -> the buzzer pin.
static void setup(const Timer& t) {
  CMU_ClockEnable(t.clk, true);
  uint32_t f = CMU_ClockFreqGet(t.clk);
  TIMER_Init_TypeDef init = TIMER_INIT_DEFAULT;
  init.enable = false;
  static const struct { uint32_t div; TIMER_Prescale_TypeDef p; } kDiv[] = {
      {1, timerPrescale1}, {2, timerPrescale2}, {4, timerPrescale4}, {8, timerPrescale8},
      {16, timerPrescale16}, {32, timerPrescale32}, {64, timerPrescale64}};
  uint32_t div = 1;
  init.prescale = timerPrescale1;
  for (const auto& d : kDiv) {
    div = d.div;
    init.prescale = d.p;
    if (f / div / 200 <= 65535) break;
  }
  tickHz = f / div;
  TIMER_Init(t.t, &init);
  TIMER_InitCC_TypeDef cc = TIMER_INITCC_DEFAULT;
  cc.mode = timerCCModePWM;
  TIMER_InitCC(t.t, 0, &cc);
  GPIO->TIMERROUTE[t.idx].CC0ROUTE = ((uint32_t)gpioPortC << _GPIO_TIMER_CC0ROUTE_PORT_SHIFT) |
                                     ((uint32_t)kPin << _GPIO_TIMER_CC0ROUTE_PIN_SHIFT);
  TIMER_TopSet(t.t, 1000);
  TIMER_Enable(t.t, true);
}

// Does timer t really reach the pin? 4 kHz for up to 3 ms, count the edges we read back.
// (The buzzer gets ~2 ms of a faint 4 kHz tick at boot; it's masked by the wake sound.)
static bool probe(const Timer& t) {
  setup(t);
  uint32_t period = tickHz / 4000;
  TIMER_TopSet(t.t, period - 1);
  TIMER_CompareSet(t.t, 0, period / 2);
  TIMER_CounterSet(t.t, 0);
  GPIO->TIMERROUTE_SET[t.idx].ROUTEEN = GPIO_TIMER_ROUTEEN_CC0PEN;
  int toggles = 0;
  unsigned last = GPIO_PinInGet(gpioPortC, kPin);
  uint32_t t0 = micros();
  while ((uint32_t)(micros() - t0) < 3000 && toggles < 12) {
    unsigned v = GPIO_PinInGet(gpioPortC, kPin);
    if (v != last) { toggles++; last = v; }
  }
  GPIO->TIMERROUTE_CLR[t.idx].ROUTEEN = GPIO_TIMER_ROUTEEN_CC0PEN;
  GPIO_PinOutClear(gpioPortC, kPin);
  if (toggles >= 6) return true;  // 3 ms at 4 kHz = 24 edges
  TIMER_Enable(t.t, false);
  CMU_ClockEnable(t.clk, false);
  return false;
}

void begin() {
  GPIO_PinModeSet(gpioPortC, kPin, gpioModePushPull, 0);
  int irIdx = ir::timerIndex();
  static char log[48];
  int n = snprintf(log, sizeof(log), "tried");
  tm = nullptr;
  for (const Timer& t : kTimers) {
    if (t.idx == irIdx) continue;
    if (probe(t)) { tm = &t; break; }
    n += snprintf(log + n, sizeof(log) - n, " %s", t.name);
  }
  probeLog = log;
  if (!tm) return;
  setup(*tm);
  routeOff();
}

static void start(const BuzzTone* t, uint16_t count) {
  if (!tm || !count) return;
  cur = t;
  n = count;
  i = 0;
  routeOff();
  stepStartUs = micros();
  apply(cur[0]);
}

void test(Sound s) {
  if (s < kNumSounds) start(kSounds[s].t, kSounds[s].n);
}

Sound resolve(Sound s) {
  if (s != kCast) return s;
  uint8_t c = pf.castSound == kCastRandom ? (uint8_t)(micros() % 3) : pf.castSound;
  return c == kCastLightning ? kLightning : c == kCastGlow ? kGlow : kCast;
}

void playResolved(Sound event, Sound shown) {
  if (!pf.on || event >= kNumSounds || !(pf.events & (1 << event))) return;
  test(shown);
}

void play(Sound s) { playResolved(s, resolve(s)); }

void tone(uint16_t hz, uint16_t ms, uint8_t duty) {
  if (ms > 5000) ms = 5000;
  if (duty > 50) duty = 50;
  uint16_t k = 0;
  while (ms > 0 && k < 200) {
    uint8_t step = ms > 250 ? 250 : (uint8_t)ms;
    custom[k++] = {hz, step, duty};
    ms -= step;
  }
  start(custom, k);
}

void sweep(uint16_t f0, uint16_t f1, uint16_t ms) {
  if (ms > 5000) ms = 5000;
  if (ms < 20) ms = 20;
  if (f0 < 100) f0 = 100;
  if (f1 < 100) f1 = 100;
  uint16_t k = ms / 10 < 200 ? ms / 10 : 200;  // >= 10 ms per step, at most 200 steps
  if (k < 2) k = 2;
  uint8_t step = (uint8_t)(ms / k);
  float r = logf((float)f1 / f0);
  for (uint16_t j = 0; j < k; j++)  // equal steps in pitch (log), like a slide whistle
    custom[j] = {(uint16_t)(f0 * expf(r * j / (k - 1))), step, 50};
  start(custom, k);
}

void update() {
  if (!cur) return;
  uint32_t now = micros();
  bool moved = false;
  // catch up on time if loop() was late: skipped steps are dropped, the length stays right
  while ((uint32_t)(now - stepStartUs) >= (uint32_t)cur[i].ms * 1000u) {
    stepStartUs += (uint32_t)cur[i].ms * 1000u;
    if (++i >= n) { stop(); return; }
    moved = true;
  }
  if (moved) apply(cur[i]);
}

bool busy() { return cur != nullptr; }

void stop() {
  cur = nullptr;
  routeOff();
}

void setEnabled(bool en) {
  pf.on = en;
  if (!en) stop();
}
bool enabled() { return pf.on; }

void setVolume(uint8_t pct) {
  pf.volume = pct > 100 ? 100 : pct;
  float v = pf.volume / 100.0f;
  gain = v * v;
}
void setPitch(int8_t st) {
  pf.pitch = st < -12 ? -12 : st > 12 ? 12 : st;
  pitchMul = powf(2.0f, pf.pitch / 12.0f);
}
void setCastSound(uint8_t c) { pf.castSound = c <= kCastRandom ? c : (uint8_t)kCastCarillon; }
void setEvents(uint8_t m) { pf.events = m & kEventMask; }
void setPrefs(const Prefs& p) {
  setEnabled(p.on);
  setVolume(p.volume);
  setPitch(p.pitch);
  setCastSound(p.castSound);
  setEvents(p.events);
}
const Prefs& prefs() { return pf; }

const char* info() {
  static char buf[64];
  if (!tm) {
    snprintf(buf, sizeof(buf), "SILENT: no timer reaches D%u (%s)", (unsigned)kPin, probeLog);
    return buf;
  }
  snprintf(buf, sizeof(buf), "%s on D%u, %lu Hz ticks", tm->name, (unsigned)kPin, (unsigned long)tickHz);
  return buf;
}

}  // namespace sound

#else  // HAS_BUZZER 0
namespace sound {
static Prefs pf = defaults();
void begin() {}
void setPrefs(const Prefs& p) { pf = p; }
const Prefs& prefs() { return pf; }
void play(Sound) {}
Sound resolve(Sound s) {
  if (s != kCast) return s;
  return pf.castSound == kCastLightning ? kLightning : pf.castSound == kCastGlow ? kGlow : kCast;
}
void playResolved(Sound, Sound) {}
void test(Sound) {}
void tone(uint16_t, uint16_t, uint8_t) {}
void sweep(uint16_t, uint16_t, uint16_t) {}
void update() {}
bool busy() { return false; }
void stop() {}
void setEnabled(bool) {}
bool enabled() { return false; }
void setVolume(uint8_t) {}
void setPitch(int8_t) {}
void setCastSound(uint8_t) {}
void setEvents(uint8_t) {}
const char* info() { return "no buzzer"; }
}  // namespace sound
#endif

// ------------------------------------------------------------------ names (both builds)
#include <strings.h>
namespace sound {
static const char* const kNames[kNumSounds] = {"cast", "lightning", "glow", "error", "learned", "wake", "lowbat"};
static const char* const kCastNames[] = {"cast", "lightning", "glow", "random"};

Prefs defaults() {
  Prefs p = {};
  p.on = 1;
  p.volume = 100;
  p.pitch = 0;
  p.castSound = kCastCarillon;
  p.events = kEventMask;
  return p;
}
bool fromName(const char* nm, Sound& out) {
  for (uint8_t k = 0; k < kNumSounds; k++)
    if (!strcasecmp(nm, kNames[k])) { out = (Sound)k; return true; }
  return false;
}
const char* name(uint8_t s) { return s < kNumSounds ? kNames[s] : "?"; }
bool castFromName(const char* nm, uint8_t& out) {
  for (uint8_t k = 0; k <= kCastRandom; k++)
    if (!strcasecmp(nm, kCastNames[k])) { out = k; return true; }
  return false;
}
const char* castName(uint8_t c) { return c <= kCastRandom ? kCastNames[c] : "cast"; }
}  // namespace sound
