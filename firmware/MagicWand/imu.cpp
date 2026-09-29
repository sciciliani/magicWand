#include "imu.h"

#include <Arduino.h>
#include <LSM6DS3.h>  // library: "Seeed Arduino LSM6DS3" (works on the MG24 per Seeed wiki)
#include <Wire.h>
#include <math.h>

#include "config.h"

namespace imu {

enum : uint8_t {
  WAKE_UP_SRC = 0x1B,
  CTRL1_XL = 0x10,
  CTRL2_G = 0x11,
  CTRL3_C = 0x12,
  CTRL6_C = 0x15,
  OUTX_L_G = 0x22,   // gyro X/Y/Z then accel X/Y/Z (12 bytes)
  OUTX_L_XL = 0x28,  // accel only (6 bytes)
  TAP_CFG = 0x58,
  WAKE_UP_THS = 0x5B,
  WAKE_UP_DUR = 0x5C,
  MD1_CFG = 0x5E,
};

static LSM6DS3 dev(I2C_MODE, IMU_I2C_ADDR);
static wand::Vec3 bias = {0, 0, 0};
static wand::Vec3 lastRaw = {0, 0, 0};
static bool powered = false;

constexpr float kAccScale = 0.122f / 1000.0f;  // ±4 g
constexpr float kGyroScale = 70.0f / 1000.0f;  // ±2000 dps

static bool powerUp() {
  if (!powered) {
    pinMode(PIN_IMU_PWR, OUTPUT);
    digitalWrite(PIN_IMU_PWR, HIGH);
    delay(15);  // LSM6DS3 boot time
    powered = true;
  }
  return dev.begin() == 0;
}

bool begin() {
  if (!powerUp()) return false;
  dev.writeRegister(MD1_CFG, 0x00);
  dev.writeRegister(TAP_CFG, 0x00);
  dev.writeRegister(CTRL6_C, 0x00);
  dev.writeRegister(CTRL3_C, 0x44);   // BDU + auto-increment
  dev.writeRegister(CTRL1_XL, 0x48);  // 104 Hz, ±4 g
  dev.writeRegister(CTRL2_G, 0x4C);   // 104 Hz, ±2000 dps
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
  static int still = 0;
  still = energy < 8.0f ? still + 1 : 0;
  if (still > SAMPLE_HZ / 2) {
    const float k = 0.01f;
    bias.x += k * (lastRaw.x - bias.x);
    bias.y += k * (lastRaw.y - bias.y);
    bias.z += k * (lastRaw.z - bias.z);
  }
}

bool quickGravity(wand::Vec3& g, float& shake) {
  if (!powerUp()) return false;
  dev.writeRegister(CTRL3_C, 0x44);
  dev.writeRegister(CTRL2_G, 0x00);   // gyro off
  dev.writeRegister(CTRL1_XL, 0x68);  // 416 Hz, ±4 g
  // Right after power-up the first readings can be zero or unsettled, which
  // would look like a jolt and wake the wand for nothing: skip them.
  delay(20);
  g = {0, 0, 0};
  shake = 0;
  const int N = 4;
  int got = 0;
  for (int i = 0; i < N + 6 && got < N; i++) {
    uint8_t b[6];
    if (dev.readRegisterRegion(b, OUTX_L_XL, 6) != 0) return false;
    auto s16 = [&](int k) { return (int16_t)(b[k] | (b[k + 1] << 8)); };
    wand::Vec3 a = {s16(0) * kAccScale, s16(2) * kAccScale, s16(4) * kAccScale};
    float mag = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
    delay(3);
    if (i < 2 || mag < 0.2f) continue;  // settling / not ready yet
    g = {g.x + a.x / N, g.y + a.y / N, g.z + a.z / N};
    float dev1g = fabsf(mag - 1.0f);
    if (dev1g > shake) shake = dev1g;
    got++;
  }
  return got == N;
}

void armWakeOnMotion(uint8_t threshold) {
  dev.writeRegister(CTRL2_G, 0x00);
  dev.writeRegister(CTRL6_C, 0x10);   // accel low-power
  dev.writeRegister(CTRL1_XL, 0x20);  // 26 Hz, ±2 g
  dev.writeRegister(WAKE_UP_DUR, 0x00);
  dev.writeRegister(WAKE_UP_THS, threshold & 0x3F);
  dev.writeRegister(TAP_CFG, 0x90);   // interrupts on, slope filter, pulsed
  dev.writeRegister(MD1_CFG, 0x20);   // wake-up -> INT1
  delay(100);
  uint8_t src;
  dev.readRegister(&src, WAKE_UP_SRC);
}

void powerOff() {
  digitalWrite(PIN_IMU_PWR, LOW);
  powered = false;
}

}  // namespace imu
