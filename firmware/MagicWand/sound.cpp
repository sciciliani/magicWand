// sound.cpp — see sound.h.
#include "sound.h"

#include "config.h"

#if HAS_BUZZER
#include <Arduino.h>
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
// TIMER1..4 that works, so the buzzer starts from the other end.
static const Timer kTimers[] = {
    {TIMER4, cmuClock_TIMER4, 4, "TIMER4"},
    {TIMER3, cmuClock_TIMER3, 3, "TIMER3"},
    {TIMER2, cmuClock_TIMER2, 2, "TIMER2"},
};
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
static bool on = true;
static const BuzzTone* cur = nullptr;
static uint16_t n = 0, i = 0;
static uint32_t stepStartUs = 0;

static void routeOff() {
  if (tm) GPIO->TIMERROUTE_CLR[tm->idx].ROUTEEN = GPIO_TIMER_ROUTEEN_CC0PEN;
  GPIO_PinOutClear(gpioPortC, kPin);  // pin LOW = buzzer driver off
}

static void apply(const BuzzTone& s) {
  if (!tm || s.hz == 0 || s.duty == 0) { routeOff(); return; }
  uint32_t period = tickHz / s.hz;
  if (period < 4) period = 4;
  if (period > 65536) period = 65536;
  uint32_t cc = period * s.duty / 100;
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

void begin() {
  GPIO_PinModeSet(gpioPortC, kPin, gpioModePushPull, 0);
  int irIdx = ir::timerIndex();
  for (const Timer& t : kTimers) {
    if (t.idx == irIdx) continue;
    tm = &t;
    break;
  }
  if (!tm) return;
  CMU_ClockEnable(tm->clk, true);
  uint32_t f = CMU_ClockFreqGet(tm->clk);
  // prescaler so the lowest note (~200 Hz) still fits the 16-bit counter
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
  TIMER_Init(tm->t, &init);
  TIMER_InitCC_TypeDef cc = TIMER_INITCC_DEFAULT;
  cc.mode = timerCCModePWM;
  TIMER_InitCC(tm->t, 0, &cc);
  GPIO->TIMERROUTE[tm->idx].CC0ROUTE = ((uint32_t)gpioPortC << _GPIO_TIMER_CC0ROUTE_PORT_SHIFT) |
                                       ((uint32_t)kPin << _GPIO_TIMER_CC0ROUTE_PIN_SHIFT);
  TIMER_TopSet(tm->t, 1000);
  TIMER_Enable(tm->t, true);
  routeOff();
}

void play(Sound s) {
  if (!on || !tm || s >= kNumSounds) return;
  cur = kSounds[s].t;
  n = kSounds[s].n;
  i = 0;
  routeOff();
  stepStartUs = micros();
  apply(cur[0]);
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
  on = en;
  if (!on) stop();
}
bool enabled() { return on; }

bool fromName(const char* name, Sound& out) {
  for (uint8_t k = 0; k < kNumSounds; k++)
    if (!strcasecmp(name, kSounds[k].name)) { out = (Sound)k; return true; }
  return false;
}

const char* info() {
  static char buf[40];
  if (!tm) return "no free timer";
  snprintf(buf, sizeof(buf), "%s on D%u, %lu Hz ticks", tm->name, (unsigned)kPin, (unsigned long)tickHz);
  return buf;
}

}  // namespace sound

#else  // HAS_BUZZER 0
namespace sound {
void begin() {}
void play(Sound) {}
void update() {}
bool busy() { return false; }
void stop() {}
void setEnabled(bool) {}
bool enabled() { return false; }
bool fromName(const char*, Sound&) { return false; }
const char* info() { return "no buzzer"; }
}  // namespace sound
#endif
