// haptics.h — optional vibration motor (HAS_MOTOR in config.h).
// Non-blocking: play() starts a pattern, update() must be called from loop().
// With HAS_MOTOR 0 everything compiles to no-ops.
#pragma once
#include <stdint.h>

namespace haptics {

enum Pattern : uint8_t {
  kWake,      // one clear buzz: "I'm awake and ready"
  kCast,      // buzz, buzz, long buzz: spell recognized
  kUnknown,   // two quick ticks (unused: unrecognized moves stay silent)
  kRecord,    // 3 ticks countdown: "perform the move now"
  kSaved,     // long-short: saved
  kError,     // three short
  kSleep,     // soft fade: going to sleep
  kNumPatterns
};

void begin();
void play(Pattern p);
void pulse(uint16_t ms);  // ad-hoc buzz (from the app)
void update();
bool busy();
void setEnabled(bool on);
void off();

}  // namespace haptics
