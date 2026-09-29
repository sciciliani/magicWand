// power.h — battery, dock detection and deep sleep (EM4) on the XIAO MG24.
//
// Sleep model (no power switch). Waking from EM4 reboots the chip.
//
//   WAKE_POLL (default):
//     awake --(idle)--> save "which way is down" in backup RAM, EM4 for 1.2 s
//     boot  --> earlyBoot(): quick tilt check (~25 ms)
//               unchanged -> straight back to EM4 (BLE never starts)
//               changed   -> normal boot, buzz, ready
//   WAKE_IMU_INT:
//     IMU stays powered in low-power mode; its INT1 pin wakes the chip.
#pragma once
#include <stdint.h>

namespace power {

// Call FIRST in setup(). In WAKE_POLL mode this may never return (it goes
// back to sleep if the wand hasn't moved).
void earlyBoot();
void begin();
float batteryVolts();
uint8_t batteryPercent();
bool onCharger();       // needs HAS_DOCK_SENSE wiring, else always false
bool wokeFromSleep();
const char* wakeReason();  // debug: why this boot happened
[[noreturn]] void deepSleep();

}  // namespace power
