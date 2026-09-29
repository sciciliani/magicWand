#include "status.h"

#include <Arduino.h>

#include "config.h"

namespace status {

static State state = kAdvertising;
static uint32_t since = 0;
static bool forced = false;
static uint8_t flashLeft = 0;
static uint16_t flashOn = 0, flashOff = 0;
static uint32_t flashStart = 0;

static void led(bool on) {
#if HAS_STATUS_LED
  digitalWrite(PIN_USER_LED, on ? LOW : HIGH);  // active LOW
#else
  (void)on;
#endif
}

void begin() {
#if HAS_STATUS_LED
  pinMode(PIN_USER_LED, OUTPUT);
#endif
  led(false);
  since = millis();
}

void set(State s) {
  if (s != state) {
    state = s;
    since = millis();
  }
}

void solid(bool on) {
  forced = on;
  led(on);
}

void flash(Event e) {
  switch (e) {
    case kCast:  flashLeft = 1; flashOn = 400; flashOff = 200; break;
    case kSaved: flashLeft = 2; flashOn = 150; flashOff = 150; break;
    case kError: flashLeft = 5; flashOn = 50;  flashOff = 70;  break;
  }
  flashStart = millis();
}

void off() {
  forced = false;
  flashLeft = 0;
  led(false);
}

void update() {
  if (forced) return;
  uint32_t now = millis();

  if (flashLeft) {
    uint32_t t = now - flashStart;
    if (t < flashOn) { led(true); return; }
    if (t < (uint32_t)flashOn + flashOff) { led(false); return; }
    flashLeft--;
    flashStart = now;
    if (flashLeft) return;
    since = now;  // restart the state pattern cleanly after the event
  }

  uint32_t t = now - since;
  bool on = false;
  switch (state) {
    case kBusy:        on = true; break;
    case kLearning:    on = (t % 200) < 100; break;
    case kLowBattery: {
      uint32_t p = t % 4000;
      on = p < 600 && (p % 200) < 80;
      break;
    }
    case kAdvertising: on = (t % 1000) < 500; break;
    case kConnected:   on = (t % 5000) < 40; break;
  }
  led(on);
}

}  // namespace status
