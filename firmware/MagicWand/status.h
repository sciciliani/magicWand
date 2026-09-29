// status.h — the XIAO's yellow LED (PA7) as a status light.
// (The red LED by the USB-C port belongs to the charger; firmware can't drive it.)
//
//   State (highest priority first)
//   solid ON ........................ sending IR / recording a spell / calibrating
//   fast blink (5/s) ................ learning: waiting for a remote button
//   3 quick blinks every 4 s ........ battery low (< LOW_BATTERY_PCT)
//   slow blink (1/s) ................ waiting for the app to connect (pairing)
//   short blip every 5 s ............ connected to the app
//   Events (one-off, then back to the state)
//   one long flash (0.4 s) .......... spell recognized
//   two flashes ..................... saved
//   five very fast flashes .......... error / timeout
#pragma once
#include <stdint.h>

namespace status {

enum State : uint8_t { kConnected, kAdvertising, kLowBattery, kLearning, kBusy };
enum Event : uint8_t { kCast, kSaved, kError };

void begin();
void set(State s);      // call every loop; the pattern restarts only on change
void solid(bool on);    // force ON around a blocking job (IR send)
void flash(Event e);    // one-off event pattern
void update();
void off();             // before deep sleep

}  // namespace status
