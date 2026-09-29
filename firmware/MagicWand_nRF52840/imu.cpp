#include "imu.h"

#include <Arduino.h>
#include <LSM6DS3.h>  // library: "Seeed Arduino LSM6DS3"
#include <Wire.h>

#include "config.h"

namespace imu {

// LSM6DS3TR-C registers
enum : uint8_t {
  WAKE_UP_SRC = 0x1B,
  CTRL1_XL = 0x10,
  CTRL2_G = 0x11,
  CTRL3_C = 0x12,
  CTRL6_C = 0x15,
  OUTX_L_G = 0x22,  // gyro X/Y/Z then accel X/Y/Z, 12 bytes
  TAP_CFG = 0x58,
  WAKE_UP_THS = 0x5B,
  WAKE_UP_DUR = 0x5C,
  MD1_CFG = 0x5E,
};

static LSM6DS3 dev(I2C_MODE, IMU_I2C_ADDR);
static wand::Vec3 bias = {0, 0, 0};
static wand::Vec3 lastRaw = {0, 0, 0};

constexpr float kAccScale = 0.122f / 1000.0f;  // ±4 g: 0.122 mg/LSB
constexpr float kGyroScale = 70.0f / 1000.0f;  // ±2000 dps: 70 mdps/LSB

bool begin() {
#ifdef PIN_LSM6DS3TR_C_POWER
  pinMode(PIN_LSM6DS3TR_C_POWER, OUTPUT);
  digitalWrite(PIN_LSM6DS3TR_C_POWER, HIGH);  // IMU is powered from a GPIO
  delay(10);
#endif
  if (dev.begin() != 0) return false;
  // Clear any sleep-mode configuration left over from before System OFF.
  dev.writeRegister(MD1_CFG, 0x00);
  dev.writeRegister(TAP_CFG, 0x00);
  dev.writeRegister(CTRL6_C, 0x00);  // high-performance accel
  dev.writeRegister(CTRL3_C, 0x44);  // BDU + auto-increment
  dev.writeRegister(CTRL1_XL, 0x48); // 104 Hz, ±4 g
  dev.writeRegister(CTRL2_G, 0x4C);  // 104 Hz, ±2000 dps
  delay(50);
  return true;
}

bool read(wand::Vec3& acc, wand::Vec3& gyro) {
  uint8_t b[12];
  if (dev.readRegisterRegion(b, OUTX_L_G, 12) != 0) return false;
  auto s16 = [&](int i) { return (int16_t)(b[i] | (b[i + 1] << 8)); };
  lastRaw = {s16(0) * kGyroScale, s16(2) * kGyroScale, s16(4) * kGyroScale};
  gyro = {lastRaw.x - bias.x, lastRaw.y - bias.y, lastRaw.z - bias.z};
  acc = {s16(6) * kAccScale, s16(8) * kAccScale, s16(10) * kAccScale};
  return true;
}

void trackBias(float energy) {
  // While the wand rests, the gyro should read zero: nudge the bias estimate.
  static int still = 0;
  still = energy < 8.0f ? still + 1 : 0;
  if (still > SAMPLE_HZ / 2) {
    const float k = 0.01f;
    bias.x += k * (lastRaw.x - bias.x);
    bias.y += k * (lastRaw.y - bias.y);
    bias.z += k * (lastRaw.z - bias.z);
  }
}

void armWakeOnMotion(uint8_t threshold) {
  dev.writeRegister(CTRL2_G, 0x00);          // gyro off
  dev.writeRegister(CTRL6_C, 0x10);          // accel low-power mode
  dev.writeRegister(CTRL1_XL, 0x20);         // 26 Hz, ±2 g
  dev.writeRegister(WAKE_UP_DUR, 0x00);
  dev.writeRegister(WAKE_UP_THS, threshold & 0x3F);
  dev.writeRegister(TAP_CFG, 0x91);          // interrupts on, slope filter, latched
  dev.writeRegister(MD1_CFG, 0x20);          // wake-up event -> INT1
  delay(100);                                // let the filter settle
  uint8_t src;
  dev.readRegister(&src, WAKE_UP_SRC);       // clears the latch
}

}  // namespace imu
