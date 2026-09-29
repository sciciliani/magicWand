#include "haptics.h"

#include <Arduino.h>

#include "config.h"

namespace haptics {

// Patterns: alternating ON/OFF durations in ms, 0-terminated.
static const uint16_t kPatterns[kNumPatterns][8] = {
    {150, 0},                     // kWake: one clear buzz = "ready"
    {90, 90, 90, 90, 300, 0},     // kCast: buzz, buzz, looong buzz
    {30, 70, 30, 0},              // kUnknown (currently unused: misses are silent)
    {40, 260, 40, 260, 120, 0},   // kRecord (tick, tick, go!)
    {150, 80, 50, 0},             // kSaved
    {40, 60, 40, 60, 40, 0},      // kError
    {60, 0},                      // kSleep
};

static const uint16_t* cur = nullptr;
static uint16_t adhoc[2];
static int idx = 0;
static uint32_t stepStart = 0;
static bool enabled = true;

static void motor(bool on) {
#if HAS_MOTOR
  digitalWrite(PIN_MOTOR, on ? HIGH : LOW);
#else
  (void)on;
#endif
}

void begin() {
#if HAS_MOTOR
  pinMode(PIN_MOTOR, OUTPUT);
#endif
  motor(false);
}

static void start(const uint16_t* p) {
  if (!enabled || !HAS_MOTOR) return;
  cur = p;
  idx = 0;
  stepStart = millis();
  motor(true);
}

void play(Pattern p) {
  if (p < kNumPatterns) start(kPatterns[p]);
}

void pulse(uint16_t ms) {
  adhoc[0] = ms > 1000 ? 1000 : ms;
  adhoc[1] = 0;
  start(adhoc);
}

void update() {
  if (!cur) return;
  if (millis() - stepStart < cur[idx]) return;
  idx++;
  stepStart = millis();
  if (cur[idx] == 0) {
    cur = nullptr;
    motor(false);
    return;
  }
  motor(idx % 2 == 0);  // even steps are ON
}

bool busy() { return cur != nullptr; }

void setEnabled(bool on) {
  enabled = on;
  if (!on) off();
}

void off() {
  cur = nullptr;
  motor(false);
}

}  // namespace haptics
