// imu.h — LSM6DS3TR-C (on-board IMU of the XIAO nRF52840 Sense).
// Normal mode: accel ±4 g + gyro ±2000 dps at 104 Hz.
// Sleep mode:  gyro off, accel low-power 26 Hz, "wake-up" interrupt on INT1.
#pragma once
#include "gesture.h"

namespace imu {

bool begin();
// Blocking-free read of the latest sample. Returns false on I2C error.
// acc in g, gyro in deg/s (bias-corrected).
bool read(wand::Vec3& acc, wand::Vec3& gyro);
// Call with the motion energy each sample; slowly learns gyro bias while still.
void trackBias(float energy);
// Configure the IMU as a wake-up source and clear any pending event.
void armWakeOnMotion(uint8_t threshold);

}  // namespace imu
