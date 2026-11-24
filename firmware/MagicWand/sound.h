// sound.h — passive buzzer (HAS_BUZZER in config.h).
// Non-blocking: play() starts a sound, update() must be called from loop().
// The tone comes from a hardware TIMER (not the one the IR carrier uses), so
// pitch and the duty-cycle "fades" are exact; update() only moves to the next
// step of the table (2-12 ms each). With HAS_BUZZER 0 everything is a no-op.
//
// Two ways to start a sound:
//   play(s)  the wand's own events (cast, learned, error, wake, low battery).
//            Respects mute and the per-event switches; kCast plays the chosen
//            cast sound (cast, lightning, glow or a random one of those).
//   test(s)  the app's sound lab: always plays (even when muted), so the user
//            hears what a setting does.
// Volume and pitch apply to both, and to tone()/sweep().
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

// What the cast event plays (Prefs::castSound).
enum CastSound : uint8_t { kCastCarillon, kCastLightning, kCastGlow, kCastRandom };

// Saved in flash by storage (its own key, so adding it kept every other setting).
struct Prefs {
  uint8_t on;         // 0 = muted
  uint8_t volume;     // 0-100 %
  int8_t pitch;       // semitones, -12..+12 (moves every sound up or down)
  uint8_t castSound;  // CastSound
  uint8_t events;     // bit (1 << Sound) set = that event makes a sound
  uint8_t reserved[3];
};
constexpr uint8_t kEventMask = (1 << kCast) | (1 << kError) | (1 << kLearned) | (1 << kWake) | (1 << kLowBat);
Prefs defaults();

void begin();               // after ir::begin() (it avoids the IR timer)
void setPrefs(const Prefs& p);
const Prefs& prefs();

void play(Sound s);         // event sound (see above); replaces whatever is playing
Sound resolve(Sound s);     // what an event shows: kCast -> the chosen cast sound (random picks one)
void playResolved(Sound event, Sound shown);  // like play(event), but plays `shown` (from resolve)
void test(Sound s);         // sound lab: ignores mute and the event switches
void tone(uint16_t hz, uint16_t ms, uint8_t duty);    // one tone, up to 5 s
void sweep(uint16_t fromHz, uint16_t toHz, uint16_t ms);  // glide, up to 5 s
void update();              // call every loop()
bool busy();
void stop();                // silence now (call before IR or sleep)

void setEnabled(bool on);   // runtime mute (SOUND off / SOUND on)
bool enabled();
void setVolume(uint8_t pct);
void setPitch(int8_t semitones);
void setCastSound(uint8_t c);
void setEvents(uint8_t mask);

bool fromName(const char* name, Sound& out);   // "cast", "lightning", ...
const char* name(uint8_t s);
bool castFromName(const char* name, uint8_t& out);  // "cast", "lightning", "glow", "random"
const char* castName(uint8_t c);
const char* info();         // e.g. "TIMER4 on D0" or "no timer"

}  // namespace sound
