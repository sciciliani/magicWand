// power.h — battery, charger detection and deep sleep.
//
// Sleep model (no power switch):
//   awake  --(no motion for settings.idleSleepMs)-->  System OFF (~5 uA)
//   System OFF --(IMU wake-up interrupt on INT1)-->  chip resets, setup() runs
// So every wake is a fresh boot (~0.3 s). The move that woke the wand is not
// cast; the wand buzzes once when it's ready.
#pragma once
#include <stdint.h>

namespace power {

void begin();
float batteryVolts();     // ~3.0 (empty) .. 4.2 (full)
uint8_t batteryPercent(); // rough LiPo curve
bool onCharger();         // 5 V present on USB/5V pin (cable or wireless coil)
bool wokeFromSleep();     // true if this boot was a wake from System OFF
[[noreturn]] void deepSleep(uint8_t wakeThreshold);

}  // namespace power
