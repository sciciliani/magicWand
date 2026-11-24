// light.h — small ARGB (WS2812B) ring: short blinks for the wand's events.
//
// Wiring (HAS_LIGHT in config.h):
//   ring 5V/VCC -> PIN_LIGHT_PWR (D8): the pin powers the ring, so it's
//                  completely off between blinks and while the wand sleeps
//   ring DIN    -> PIN_LIGHT_DATA (D10), 330 R in series if you have one
//   ring GND    -> GND
// Being powered from a pin, the ring gets little current. So, always:
//   - at most LIGHT_MAX_LIT LEDs lit at once (the rest stay black),
//   - every colour capped at LIGHT_MAX_LEVEL, and the whole frame at LIGHT_BUDGET.
// These limits are enforced in one place (show()), whatever an effect asks for.
//
// Non-blocking like sound: event()/test() start an effect, update() runs it.
// A frame is bit-banged on D10 with interrupts off (~30 µs per LED).
#pragma once
#include <stdint.h>

#include "sound.h"

namespace light {

// Same names and order as sound::Sound, so a sound and its light can share an event.
enum Effect : uint8_t {
  kCast, kLightning, kGlow, kError, kLearned, kWake, kLowBat,
  kNumEvents,
  kWalk = kNumEvents,  // test: one LED at a time around the ring
  kPixel,              // test: one chosen LED, one colour
  kNumEffects
};

struct Prefs {
  uint8_t on;      // 0 = no event blinks (tests still light)
  uint8_t bright;  // 0-100 %, of the LIGHT_MAX_LEVEL cap
  uint8_t events;  // bit (1 << Effect) set = that event blinks
  uint8_t reserved;
};
constexpr uint8_t kEventMask = (1 << kCast) | (1 << kError) | (1 << kLearned) | (1 << kWake) | (1 << kLowBat);
Prefs defaults();

void begin();
void setPrefs(const Prefs& p);
const Prefs& prefs();
void setEnabled(bool on);
void setBright(uint8_t pct);
void setEvents(uint8_t mask);

// An event happened. `event` decides whether it blinks (prefs), `shown` is
// what to show: for a cast, the sound the wand chose (cast/lightning/glow).
void event(sound::Sound event, sound::Sound shown);
void test(Effect e);                                  // ignores prefs.on/events
void pixel(uint8_t index, uint32_t rgb, uint16_t ms); // test one LED
void update();   // every loop()
bool busy();
void off();      // stop now and power the ring down (before sleep)

bool fromName(const char* name, Effect& out);  // cast, lightning, glow, error, learned, wake, lowbat, walk
const char* info();

}  // namespace light
