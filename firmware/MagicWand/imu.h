// imu.h — LSM6DS3TR-C on the XIAO MG24 Sense (I2C 0x6A, powered from PD5).
// Normal mode: accel ±4 g + gyro ±2000 dps at 104 Hz.
#pragma once
#include "gesture.h"

namespace imu {

bool begin();
// acc in g, gyro in deg/s (bias-corrected). False on I2C error.
bool read(wand::Vec3& acc, wand::Vec3& gyro);
// Call every sample with the motion energy: learns gyro bias while still.
void trackBias(float energy);

// Fast path used by the sleep poller: power the IMU, take a few accel
// readings (~20 ms), return the average in g plus the largest deviation of
// |acc| from 1 g (i.e. "was it moving while we looked?").
bool quickGravity(wand::Vec3& g, float& shake);

// WAKE_IMU_INT mode only: IMU raises INT1 on motion.
void armWakeOnMotion(uint8_t threshold);

void powerOff();

}  // namespace imu
