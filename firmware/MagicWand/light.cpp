// light.cpp — see light.h.
#include "light.h"

#include "config.h"

#if HAS_LIGHT
#include <Arduino.h>
#include <em_gpio.h>
#include <string.h>

static_assert(LIGHT_MAX_LIT >= 1 && LIGHT_MAX_LIT <= 3, "the pin-powered ring: 1 to 3 LEDs lit at most");
static_assert(LIGHT_LEDS >= LIGHT_MAX_LIT && LIGHT_LEDS <= 16, "LIGHT_LEDS: 1-16");

namespace light {

struct Rgb { uint8_t r, g, b; };

// The data pin is written straight to the GPIO register (fast enough for the
// WS2812B timing), so it needs its port and pin on the XIAO MG24.
static_assert(PIN_LIGHT_DATA == D8 || PIN_LIGHT_DATA == D10 || PIN_LIGHT_DATA == D4 || PIN_LIGHT_DATA == D5,
              "PIN_LIGHT_DATA: D4, D5, D8 or D10");
static_assert(PIN_LIGHT_PWR != PIN_LIGHT_DATA, "PIN_LIGHT_PWR and PIN_LIGHT_DATA must differ");
static const GPIO_Port_TypeDef kDataPort = (PIN_LIGHT_DATA == D8 || PIN_LIGHT_DATA == D10) ? gpioPortA : gpioPortC;
static const uint32_t kDataMask = 1u << (PIN_LIGHT_DATA == D8 ? 3 : PIN_LIGHT_DATA == D10 ? 5 : PIN_LIGHT_DATA == D4 ? 4 : 5);

static Prefs pf = defaults();
static Rgb px[LIGHT_LEDS];
static Effect cur = kNumEffects;
static uint32_t t0 = 0, lastFrame = 0;
static bool powered = false;
static uint8_t pixIndex = 0;
static Rgb pixColor = {0, 0, 0};
static uint16_t pixMs = 0;
static uint32_t cPerUs = 0;  // CPU cycles per µs (DWT cycle counter, switched on by ir.cpp)

// ------------------------------------------------------------------ the wire
// WS2812B: each bit is ~1.25 µs, high for 0.35 µs (0) or 0.8 µs (1). GRB order, MSB first.
static inline void sendByte(uint8_t v, uint32_t t0h, uint32_t t1h, uint32_t tbit) {
  for (int b = 7; b >= 0; b--) {
    uint32_t start = DWT->CYCCNT;
    GPIO_PortOutSet(kDataPort, kDataMask);
    uint32_t hi = (v >> b) & 1 ? t1h : t0h;
    while ((uint32_t)(DWT->CYCCNT - start) < hi) {}
    GPIO_PortOutClear(kDataPort, kDataMask);
    while ((uint32_t)(DWT->CYCCNT - start) < tbit) {}
  }
}

// Push px[] to the ring, after applying the safety limits.
static void show() {
  // 1) at most LIGHT_MAX_LIT LEDs lit
  Rgb out[LIGHT_LEDS];
  uint8_t lit = 0;
  uint16_t level = (uint16_t)LIGHT_MAX_LEVEL * pf.bright / 100;  // brightest a channel may be
  uint32_t sum = 0;
  for (int i = 0; i < LIGHT_LEDS; i++) {
    Rgb p = px[i];
    bool on = p.r || p.g || p.b;
    if (on && lit >= LIGHT_MAX_LIT) on = false;
    if (on) {
      lit++;
      p.r = (uint8_t)((p.r * level + 254) / 255);  // round up: a dim colour stays visible
      p.g = (uint8_t)((p.g * level + 254) / 255);
      p.b = (uint8_t)((p.b * level + 254) / 255);
      sum += p.r + p.g + p.b;
      out[i] = p;
    } else {
      out[i] = {0, 0, 0};
    }
  }
  // 2) the whole frame within the pin's current budget
  if (sum > LIGHT_BUDGET) {
    for (auto& p : out) {
      p.r = (uint8_t)(p.r * LIGHT_BUDGET / sum);
      p.g = (uint8_t)(p.g * LIGHT_BUDGET / sum);
      p.b = (uint8_t)(p.b * LIGHT_BUDGET / sum);
    }
  }
  if (!cPerUs) return;
  uint32_t t0h = cPerUs * 35 / 100, t1h = cPerUs * 80 / 100, tbit = cPerUs * 130 / 100;
  noInterrupts();
  for (const auto& p : out) {
    sendByte(p.g, t0h, t1h, tbit);
    sendByte(p.r, t0h, t1h, tbit);
    sendByte(p.b, t0h, t1h, tbit);
  }
  interrupts();
  GPIO_PortOutClear(kDataPort, kDataMask);  // low = latch (the next frame comes > 10 ms later)
}

static void powerOn() {
  if (powered) return;
  digitalWrite(PIN_LIGHT_DATA, LOW);
  digitalWrite(PIN_LIGHT_PWR, HIGH);
  powered = true;
  delayMicroseconds(1500);  // let the LEDs' own chips start before the first frame
}

static void powerOff() {
  digitalWrite(PIN_LIGHT_DATA, LOW);  // a high data pin would feed the ring through its input
  digitalWrite(PIN_LIGHT_PWR, LOW);
  powered = false;
}

// ------------------------------------------------------------------ effects
static const Rgb kGold = {255, 150, 30}, kWhite = {190, 210, 255}, kViolet = {170, 80, 255}, kRed = {255, 20, 10},
                 kGreen = {30, 255, 60}, kBlue = {40, 110, 255}, kAmber = {255, 90, 0}, kDim = {120, 120, 120};

static Rgb scale(Rgb c, uint16_t k /* 0-256 */) {
  return {(uint8_t)(c.r * k >> 8), (uint8_t)(c.g * k >> 8), (uint8_t)(c.b * k >> 8)};
}
static uint16_t ramp(uint32_t t, uint32_t a, uint32_t b) {  // 0 before a, 256 after b, eased (squared) in between
  if (t <= a) return 0;
  if (t >= b) return 256;
  uint32_t x = (t - a) * 256 / (b - a);
  return (uint16_t)(x * x / 256);
}
static bool on(uint32_t t, uint32_t a, uint32_t b) { return t >= a && t < b; }

// Fill px[] for time t (ms) of effect e. Returns false when the effect is over.
static bool frame(Effect e, uint32_t t) {
  memset(px, 0, sizeof(px));
  const int N = LIGHT_LEDS;
  switch (e) {
    case kCast: {  // a two-LED comet runs once round the ring (~320 ms)
      if (t >= 340) return false;
      int head = (int)(t * N / 320) % N;
      px[head] = kGold;
      px[(head + N - 1) % N] = scale(kGold, 70);
      return true;
    }
    case kLightning: {  // two white flashes on three spread LEDs, the second fainter
      if (t >= 240) return false;
      uint16_t k = on(t, 0, 60) ? 256 : on(t, 110, 190) ? 140 : 0;
      for (int j = 0; j < 3; j++) px[j * N / 3] = scale(kWhite, k);
      return true;
    }
    case kGlow: {  // three neighbours fade in and out (~500 ms)
      if (t >= 500) return false;
      uint16_t k = t < 180 ? ramp(t, 0, 180) : 256 - ramp(t, 180, 500);
      for (int j = 0; j < 3; j++) px[j] = scale(kViolet, j == 1 ? k : k * 3 / 5);
      return true;
    }
    case kError:   // two opposite reds, two short blinks
    case kLearned: {  // two opposite greens, two short blinks
      if (t >= 300) return false;
      if (on(t, 0, 100) || on(t, 180, 280)) {
        Rgb c = e == kError ? kRed : kGreen;
        px[0] = c;
        px[N / 2] = c;
      }
      return true;
    }
    case kWake: {  // one blue breath (~260 ms)
      if (t >= 260) return false;
      px[0] = scale(kBlue, t < 100 ? ramp(t, 0, 100) : 256 - ramp(t, 100, 260));
      return true;
    }
    case kLowBat: {  // one amber LED, three short blinks
      if (t >= 560) return false;
      if (on(t, 0, 80) || on(t, 240, 320) || on(t, 480, 560)) px[0] = kAmber;
      return true;
    }
    case kWalk: {  // test: each LED in turn, 150 ms each
      if (t >= (uint32_t)N * 150) return false;
      px[t / 150] = kDim;
      return true;
    }
    case kPixel: {
      if (t >= pixMs) return false;
      px[pixIndex % N] = pixColor;
      return true;
    }
    default:
      return false;
  }
}

static void start(Effect e) {
  if (!cPerUs) return;
  cur = e;
  t0 = millis();
  lastFrame = 0;
  powerOn();
  frame(cur, 0);
  show();
}

void begin() {
  pinMode(PIN_LIGHT_PWR, OUTPUT);
  pinMode(PIN_LIGHT_DATA, OUTPUT);
  powerOff();
  cPerUs = SystemCoreClock / 1000000;
  if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0) cPerUs = 0;  // ir::begin() switches the counter on
}

void event(sound::Sound ev, sound::Sound shown) {
  if (!pf.on || ev >= (sound::Sound)kNumEvents || !(pf.events & (1 << ev))) return;
  start((Effect)shown);
}
void test(Effect e) {
  if (e < kNumEffects && e != kPixel) start(e);
}
void pixel(uint8_t index, uint32_t rgb, uint16_t ms) {
  pixIndex = index;
  pixColor = {(uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb};
  pixMs = ms > 5000 ? 5000 : ms;
  start(kPixel);
}

void update() {
  if (cur == kNumEffects) return;
  uint32_t now = millis();
  if (now - lastFrame < 15) return;  // ~60 frames/s is plenty for blinks
  lastFrame = now;
  if (!frame(cur, now - t0)) { off(); return; }
  show();
}

bool busy() { return cur != kNumEffects; }

void off() {
  cur = kNumEffects;
  if (powered) {
    memset(px, 0, sizeof(px));
    show();
  }
  powerOff();
}

void setEnabled(bool en) { pf.on = en; }
void setBright(uint8_t pct) { pf.bright = pct > 100 ? 100 : pct; }
void setEvents(uint8_t m) { pf.events = m & kEventMask; }
void setPrefs(const Prefs& p) {
  setEnabled(p.on);
  setBright(p.bright);
  setEvents(p.events);
}
const Prefs& prefs() { return pf; }

const char* info() {
  static char buf[72];
  snprintf(buf, sizeof(buf), "%d LEDs, max %d lit, power D%d, data D%d%s", LIGHT_LEDS, LIGHT_MAX_LIT,
           (int)PIN_LIGHT_PWR, (int)PIN_LIGHT_DATA, cPerUs ? "" : ", NO cycle counter (dark)");
  return buf;
}

}  // namespace light

#else  // HAS_LIGHT 0
namespace light {
static Prefs pf = defaults();
void begin() {}
void setPrefs(const Prefs& p) { pf = p; }
const Prefs& prefs() { return pf; }
void setEnabled(bool) {}
void setBright(uint8_t) {}
void setEvents(uint8_t) {}
void event(sound::Sound, sound::Sound) {}
void test(Effect) {}
void pixel(uint8_t, uint32_t, uint16_t) {}
void update() {}
bool busy() { return false; }
void off() {}
const char* info() { return "no light"; }
}  // namespace light
#endif

// ------------------------------------------------------------------ both builds
#include <strings.h>
namespace light {
static const char* const kNames[] = {"cast", "lightning", "glow", "error", "learned", "wake", "lowbat", "walk"};
Prefs defaults() {
  Prefs p = {};
  p.on = 1;
  p.bright = 60;
  p.events = kEventMask;
  return p;
}
bool fromName(const char* nm, Effect& out) {
  for (uint8_t k = 0; k < sizeof(kNames) / sizeof(kNames[0]); k++)
    if (!strcasecmp(nm, kNames[k])) { out = (Effect)k; return true; }
  return false;
}
}  // namespace light
