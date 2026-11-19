// sound.h — passive buzzer (HAS_BUZZER in config.h).
// Non-blocking: play() starts a sound, update() must be called from loop().
// The tone comes from a hardware TIMER (not the one the IR carrier uses), so
// pitch and the duty-cycle "fades" are exact; update() only moves to the next
// step of the table (2-12 ms each). With HAS_BUZZER 0 everything is a no-op.
#pragma once
#include <stdint.h>

namespace sound {

enum Sound : uint8_t {
  kCast,       // rising carillon: spell sent
  kLightning,  // zap up + roll down
  kGlow,       // sparkle chord that fades
  kError,      // two soft falling notes
  kLearned,    // ding-dong: code learned / move saved
  kWake,       // small breath upward: woke up
  kLowBat,     // three soft falling notes
  kNumSounds
};

void begin();               // after ir::begin() (it avoids the IR timer)
void play(Sound s);         // replaces whatever is playing
void update();              // call every loop()
bool busy();
void stop();                // silence now (call before IR or sleep)
void setEnabled(bool on);   // runtime mute (SOUND off / SOUND on)
bool enabled();
bool fromName(const char* name, Sound& out);  // "cast", "lightning", ...
const char* info();         // e.g. "TIMER4 on D0" or "no timer"

}  // namespace sound
