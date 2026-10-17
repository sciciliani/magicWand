#include "power.h"

#include <Arduino.h>
#include <ArduinoLowPower.h>  // bundled with the Silicon Labs core
#include <WatchdogTimer.h>    // Silicon Labs core library
#include <em_burtc.h>
#include <em_cmu.h>
#include <em_emu.h>
#include <em_gpio.h>
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
enum : uint32_t { kMagicAddr = 8, kGxAddr = 9, kGyAddr = 10, kGzAddr = 11, kPollsAddr = 12, kSensAddr = 13, kQuietAddr = 14, kWhereAddr = 15,
                  kAxAddr = 16, kAyAddr = 17, kAzAddr = 18, kPendAddr = 19, kLowBatAddr = 20 };
static const uint32_t kLowBatMagic = 0x4C4F5742;  // "LOWB": battery was low when the wand went to sleep
static const uint32_t kQuietMagic = 0x51554945;  // "QUIE"
static bool quiet = false;
static const char* const kWhere[] = {"?", "loop: before comm::poll", "comm::poll start", "inside BLE.poll()", "comm::poll after BLE.poll", "loop: readLine/handle", "loop: learn", "loop: sampling", "notify(): sending to the app", "loop: button", "loop: end (idle delay)"};
void where(uint8_t step) { LowPower.deepSleepMemoryWrite(kWhereAddr, step); }
static const uint32_t kMagic = 0x57414E44;  // "WAND"
static bool woke = false;
static char why[64] = "reset";  // why this boot happened (printed in the BOOT line)

static uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float u2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

// EM4 for `ms`, woken by the BURTC timer (waking = a reboot). Same steps as
// the core's LowPower.deepSleep(ms), with one difference: the core's version
// releases EVERY pin in EM4, so the IR LED driver's input (D6) floats for the
// whole sleep. Here the IR LED pins stay driven LOW (EM4 pin retention).
// Retention would also freeze every other pin as it is now (Serial TX high,
// VBAT_EN high, I2C...), so those are first set to exactly what EM4 gives them
// without retention: disabled, no pull. Only the IR pins behave differently.
// Retention ends by itself on wake-up; setup() then drives them LOW again
// (ir::forceOff()) before anything else.
static void em4SleepIrHeldLow(uint32_t ms) {
  const uint32_t irMask = ir::pinMask();
  for (int port = 0; port <= GPIO_PORT_MAX; port++) {
    for (unsigned pin = 0; pin < 16; pin++) {
      if (!GPIO_PORT_PIN_VALID(port, pin)) continue;
      if (port == gpioPortA && (pin == 1 || pin == 2)) continue;  // SWCLK/SWDIO: leave the debugger pins alone
      bool isIr = port == gpioPortC && (irMask & (1u << pin));
      GPIO_PinModeSet((GPIO_Port_TypeDef)port, pin, isIr ? gpioModePushPull : gpioModeDisabled, 0);
    }
  }

  CMU_ClockSelectSet(cmuClock_EM4GRPACLK, cmuSelect_ULFRCO);
  CMU_ClockEnable(cmuClock_BURTC, true);
  CMU_ClockEnable(cmuClock_BURAM, true);
  BURTC_Init_TypeDef burtcInit = BURTC_INIT_DEFAULT;
  burtcInit.compare0Top = true;  // restart the counter at the compare value
  burtcInit.em4comp = true;      // compare match wakes the chip from EM4 (reset)
  BURTC_Init(&burtcInit);
  BURTC_CounterReset();
  BURTC_CompareSet(0, ms);       // ULFRCO = 1 kHz: 1 count = 1 ms
  BURTC_IntEnable(BURTC_IEN_COMP);
  NVIC_EnableIRQ(BURTC_IRQn);    // handler lives in the core's ArduinoLowPower library
  BURTC_Enable(true);

  EMU_EM4Init_TypeDef em4 = EMU_EM4INIT_DEFAULT;
  em4.pinRetentionMode = emuPinRetentionEm4Exit;  // hold pin states in EM4, release on wake-up
  EMU_EM4Init(&em4);
  EMU_EnterEM4();
}

[[noreturn]] static void sleepFor(uint32_t ms) {
  em4SleepIrHeldLow(ms);
  while (true) {
  }
}

[[noreturn]] static void sleepAgain() { sleepFor(WAKE_POLL_MS); }

// Battery voltage from inside the poller (before begin() has run).
static float pollBatteryVolts() {
#ifdef PIN_VBAT_EN
  pinMode(PIN_VBAT_EN, OUTPUT);
  digitalWrite(PIN_VBAT_EN, HIGH);
  delayMicroseconds(500);
#endif
  float v = batteryVolts();
#ifdef PIN_VBAT_EN
  pinMode(PIN_VBAT_EN, INPUT);
#endif
  return v;
}

// Angle (deg) between the wand's long axis and "down". Rolling the wand
// around its own axis (a round wand rolling on a desk) doesn't change it.
static float axisPitchDeg(const wand::Vec3& g, const wand::Vec3& a) {
  float ng = sqrtf(g.x * g.x + g.y * g.y + g.z * g.z);
  float na = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
  if (ng < 0.1f || na < 0.1f) return 0.0f;
  float c = (g.x * a.x + g.y * a.y + g.z * a.z) / (ng * na);
  return acosf(fminf(1.0f, fmaxf(-1.0f, c))) * 57.2958f;
}

void earlyBoot() {
  quiet = LowPower.deepSleepMemoryRead(kQuietAddr) == kQuietMagic;
  // (Checked only when this isn't a wake-up from sleep, so a stale watchdog
  // flag can never stop the sleep checks from going back to sleep.)
  if (!LowPower.wokeUpFromDeepSleep() && WatchdogTimer.watchdogResetHappened()) {
    quiet = true;
    LowPower.deepSleepMemoryWrite(kQuietAddr, 0);
    uint32_t w = LowPower.deepSleepMemoryRead(kWhereAddr);
    snprintf(why, sizeof(why), "WATCHDOG: froze at %s", w < sizeof(kWhere) / sizeof(kWhere[0]) ? kWhere[w] : "?");
    return;
  }
  if (quiet) {
    LowPower.deepSleepMemoryWrite(kQuietAddr, 0);
    snprintf(why, sizeof(why), "bluetooth restart after disconnect");
    return;  // a plain restart, not a wake-up from sleep
  }
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

  // Low battery: don't keep waking up (each wake-up costs charge). Check the
  // battery every LOW_BAT_POLL_MS; carry on normally once it has been charged.
  // The button (above) still wakes the wand.
  if (LowPower.deepSleepMemoryRead(kLowBatAddr) == kLowBatMagic) {
    float v = pollBatteryVolts();
    if (v > 2.5f && v < LOW_BAT_RESUME_V) sleepFor(LOW_BAT_POLL_MS);
    LowPower.deepSleepMemoryWrite(kLowBatAddr, 0);
  }

  wand::Vec3 g;
  float shake;
  if (!imu::quickGravity(g, shake)) {
    snprintf(why, sizeof(why), "imu-check-failed");
    return;  // IMU trouble: boot normally
  }
  wand::Vec3 ref = {u2f(LowPower.deepSleepMemoryRead(kGxAddr)), u2f(LowPower.deepSleepMemoryRead(kGyAddr)),
                    u2f(LowPower.deepSleepMemoryRead(kGzAddr))};
  // Tilt = change of the wand's pointing angle (its long axis vs. "down"),
  // so rolling on a desk doesn't count. Axis = the app's calibration.
  wand::Vec3 ax = {u2f(LowPower.deepSleepMemoryRead(kAxAddr)), u2f(LowPower.deepSleepMemoryRead(kAyAddr)),
                   u2f(LowPower.deepSleepMemoryRead(kAzAddr))};
  if (sqrtf(ax.x * ax.x + ax.y * ax.y + ax.z * ax.z) < 0.1f) ax = {WAND_AXIS_X, WAND_AXIS_Y, WAND_AXIS_Z};  // not saved yet
  float tiltDeg = fabsf(axisPitchDeg(g, ax) - axisPitchDeg(ref, ax));

  // Wake sensitivity (SET wake / app slider, 1-63, default 3): higher = needs
  // a bigger tilt or shake, so table bumps don't wake it.
  uint32_t sens = LowPower.deepSleepMemoryRead(kSensAddr);
  if (sens < 1 || sens > 63) sens = 3;
  float tiltLimit = WAKE_TILT_DEG * sens / 3.0f;
  float shakeLimit = WAKE_SHAKE_G * sens / 3.0f;
  if (tiltDeg < tiltLimit && shake < shakeLimit) {
    LowPower.deepSleepMemoryWrite(kPollsAddr, LowPower.deepSleepMemoryRead(kPollsAddr) + 1);
    LowPower.deepSleepMemoryWrite(kPendAddr, 0);
    imu::powerOff();
    sleepAgain();  // still lying there: back to sleep, BLE never started
  }
  // Moved. Confirm with a second check WAKE_CONFIRM_MS later: a bump on the
  // desk is over by then, a hand picking the wand up is not.
  if (LowPower.deepSleepMemoryRead(kPendAddr) == 0) {
    LowPower.deepSleepMemoryWrite(kPendAddr, 1);
    imu::powerOff();
    sleepFor(WAKE_CONFIRM_MS);
  }
  LowPower.deepSleepMemoryWrite(kPendAddr, 0);
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
bool quietBoot() { return quiet; }

void quietRestart() {
  LowPower.deepSleepMemoryWrite(kQuietAddr, kQuietMagic);
  Serial.flush();
  delay(20);
  systemReset();
}
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
  WatchdogTimer.end();  // don't let it fire while the wand sleeps
  haptics::off();
  status::off();
  ir::stopLearn();
  ir::forceOff();  // strong LOW (WAKE_IMU_INT: the core's deepSleep() holds pins as they are)
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
    LowPower.deepSleepMemoryWrite(kAxAddr, f2u(settings.axis[0]));
    LowPower.deepSleepMemoryWrite(kAyAddr, f2u(settings.axis[1]));
    LowPower.deepSleepMemoryWrite(kAzAddr, f2u(settings.axis[2]));
    LowPower.deepSleepMemoryWrite(kPendAddr, 0);
    LowPower.deepSleepMemoryWrite(kMagicAddr, kMagic);
  }
  imu::powerOff();
  {
    float v = batteryVolts();  // VBAT_EN is still on from begin()
    bool low = v > 2.5f && v < LOW_BAT_SLEEP_V;  // < 2.5 V = no battery / bad reading: ignore
    LowPower.deepSleepMemoryWrite(kLowBatAddr, low ? kLowBatMagic : 0);
    if (low) sleepFor(LOW_BAT_POLL_MS);
  }
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
