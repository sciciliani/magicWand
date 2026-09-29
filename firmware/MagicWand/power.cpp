#include "power.h"

#include <Arduino.h>
#include <ArduinoLowPower.h>  // bundled with the Silicon Labs core
#include <math.h>
#include <string.h>

#include "comm.h"
#include "config.h"
#include "haptics.h"
#include "imu.h"
#include "ir.h"
#include "status.h"
#include "storage.h"

namespace power {

// Backup RAM (survives EM4) layout, 32-bit words.
// Starts at word 8 in case the core keeps its own bookkeeping in the first words.
enum : uint32_t { kMagicAddr = 8, kGxAddr = 9, kGyAddr = 10, kGzAddr = 11, kPollsAddr = 12, kSensAddr = 13 };
static const uint32_t kMagic = 0x57414E44;  // "WAND"
static bool woke = false;
static char why[64] = "reset";  // why this boot happened (printed in the BOOT line)

static uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float u2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

static void sleepAgain() {
  LowPower.deepSleep(WAKE_POLL_MS);
  while (true) {
  }
}

void earlyBoot() {
  woke = LowPower.wokeUpFromDeepSleep();
#if WAKE_MODE == WAKE_POLL
  if (!woke) return;
#if HAS_BUTTON
  // Button held while the wand sleeps: wake up right away.
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  delayMicroseconds(50);
  if (digitalRead(PIN_BUTTON) == LOW) {
    snprintf(why, sizeof(why), "button");
    LowPower.deepSleepMemoryWrite(kMagicAddr, 0);
    return;
  }
#endif
  if (LowPower.deepSleepMemoryRead(kMagicAddr) != kMagic) {
    snprintf(why, sizeof(why), "no-saved-tilt(mem=%lu words)", (unsigned long)LowPower.deepSleepMemorySize());
    return;
  }

  wand::Vec3 g;
  float shake;
  if (!imu::quickGravity(g, shake)) {
    snprintf(why, sizeof(why), "imu-check-failed");
    return;  // IMU trouble: boot normally
  }
  wand::Vec3 ref = {u2f(LowPower.deepSleepMemoryRead(kGxAddr)), u2f(LowPower.deepSleepMemoryRead(kGyAddr)),
                    u2f(LowPower.deepSleepMemoryRead(kGzAddr))};
  float ng = sqrtf(g.x * g.x + g.y * g.y + g.z * g.z);
  float nr = sqrtf(ref.x * ref.x + ref.y * ref.y + ref.z * ref.z);
  float c = (ng > 0.1f && nr > 0.1f) ? (g.x * ref.x + g.y * ref.y + g.z * ref.z) / (ng * nr) : 1.0f;
  float tiltDeg = acosf(fminf(1.0f, fmaxf(-1.0f, c))) * 57.2958f;

  // Wake sensitivity (SET wake / app slider, 1-63, default 3): higher = needs
  // a bigger tilt or shake, so table bumps don't wake it.
  uint32_t sens = LowPower.deepSleepMemoryRead(kSensAddr);
  if (sens < 1 || sens > 63) sens = 3;
  float tiltLimit = WAKE_TILT_DEG * sens / 3.0f;
  float shakeLimit = WAKE_SHAKE_G * sens / 3.0f;
  if (tiltDeg < tiltLimit && shake < shakeLimit) {
    LowPower.deepSleepMemoryWrite(kPollsAddr, LowPower.deepSleepMemoryRead(kPollsAddr) + 1);
    imu::powerOff();
    sleepAgain();  // still lying there: back to sleep, BLE never started
  }
  snprintf(why, sizeof(why), "moved tilt=%s shake=%s polls=%lu", FX(tiltDeg, 1).s, FX(shake).s,
           (unsigned long)LowPower.deepSleepMemoryRead(kPollsAddr));
  LowPower.deepSleepMemoryWrite(kMagicAddr, 0);  // picked up: full boot
#endif
}


void begin() {
  pinMode(PIN_USER_LED, OUTPUT);
  digitalWrite(PIN_USER_LED, HIGH);  // off (active LOW)
#ifdef PIN_VBAT_EN
  pinMode(PIN_VBAT_EN, OUTPUT);
  digitalWrite(PIN_VBAT_EN, HIGH);
#endif
#if HAS_DOCK_SENSE
  pinMode(PIN_DOCK_SENSE, INPUT);
#endif
}

bool wokeFromSleep() { return woke; }
const char* wakeReason() { return why; }

float batteryVolts() {
  analogReadResolution(12);
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += analogRead(PIN_VBAT);
  return (sum / 8.0f) * (2.0f * 3.3f / 4095.0f);  // Seeed wiki formula, VERIFY with a meter
}

uint8_t batteryPercent() {
  float v = batteryVolts();
  static const float V[] = {3.30f, 3.60f, 3.70f, 3.80f, 3.90f, 4.00f, 4.20f};
  static const uint8_t P[] = {0, 10, 25, 45, 65, 80, 100};
  if (v <= V[0]) return 0;
  for (int i = 1; i < 7; i++)
    if (v < V[i]) return P[i - 1] + (uint8_t)((v - V[i - 1]) / (V[i] - V[i - 1]) * (P[i] - P[i - 1]));
  return 100;
}

bool onCharger() {
#if HAS_DOCK_SENSE
  return digitalRead(PIN_DOCK_SENSE) == HIGH;
#else
  return false;
#endif
}

#if WAKE_MODE == WAKE_IMU_INT
static void onWake() {}
#endif

void deepSleep() {
  haptics::off();
  status::off();
  ir::stopLearn();
  digitalWrite(PIN_IR_LED, LOW);
  digitalWrite(PIN_USER_LED, HIGH);
  comm::end();  // stops BLE and Serial (Serial left open can block EM4 wake-up)

#if WAKE_MODE == WAKE_POLL
  // Remember "which way is down" so the poller can tell if we were moved.
  wand::Vec3 g;
  float shake;
  if (imu::quickGravity(g, shake)) {
    LowPower.deepSleepMemoryWrite(kGxAddr, f2u(g.x));
    LowPower.deepSleepMemoryWrite(kGyAddr, f2u(g.y));
    LowPower.deepSleepMemoryWrite(kGzAddr, f2u(g.z));
    LowPower.deepSleepMemoryWrite(kPollsAddr, 0);
    LowPower.deepSleepMemoryWrite(kSensAddr, settings.wakeThreshold);
    LowPower.deepSleepMemoryWrite(kMagicAddr, kMagic);
  }
  imu::powerOff();
  sleepAgain();
#else
  imu::armWakeOnMotion(settings.wakeThreshold);
  // The IMU must stay powered through EM4: keep GPIO states latched.
  EMU->EM4CTRL = (EMU->EM4CTRL & ~_EMU_EM4CTRL_EM4IORETMODE_MASK) | EMU_EM4CTRL_EM4IORETMODE_EM4EXIT;
  LowPower.attachInterruptWakeup(PIN_IMU_INT, onWake, RISING);
  LowPower.deepSleep();
  while (true) {
  }
#endif
}

}  // namespace power
