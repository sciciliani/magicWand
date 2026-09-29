#include "power.h"

#include <Arduino.h>
#include <bluefruit.h>

#include "config.h"
#include "haptics.h"
#include "imu.h"
#include "ir.h"

namespace power {

static bool wokeFromOff = false;

void begin() {
  // Must run before the SoftDevice starts (Bluefruit.begin()).
  uint32_t reas = NRF_POWER->RESETREAS;
  wokeFromOff = reas & (POWER_RESETREAS_OFF_Msk | POWER_RESETREAS_VBUS_Msk);
  NRF_POWER->RESETREAS = 0xFFFFFFFF;

#ifdef VBAT_ENABLE
  // Enables the battery voltage divider. NEVER drive this pin HIGH: with the
  // divider disconnected, the ADC pin sees the raw battery (Seeed warning).
  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, LOW);
#endif
#ifdef PIN_CHARGING_CURRENT
  pinMode(PIN_CHARGING_CURRENT, OUTPUT);
  digitalWrite(PIN_CHARGING_CURRENT, LOW);  // LOW = 100 mA charge, HIGH = 50 mA
#endif
}

bool wokeFromSleep() { return wokeFromOff; }

float batteryVolts() {
#ifdef PIN_VBAT
  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += analogRead(PIN_VBAT);
  analogReference(AR_DEFAULT);
  // 1M / 510k divider on the XIAO nRF52840. VERIFY against a multimeter once.
  return (sum / 8.0f) / 4095.0f * 3.0f * (1510.0f / 510.0f);
#else
  return 0;
#endif
}

uint8_t batteryPercent() {
  float v = batteryVolts();
  // Piecewise-linear LiPo discharge curve (resting voltage).
  static const float V[] = {3.30f, 3.60f, 3.70f, 3.80f, 3.90f, 4.00f, 4.20f};
  static const uint8_t P[] = {0, 10, 25, 45, 65, 80, 100};
  if (v <= V[0]) return 0;
  for (int i = 1; i < 7; i++)
    if (v < V[i]) return P[i - 1] + (uint8_t)((v - V[i - 1]) / (V[i] - V[i - 1]) * (P[i] - P[i - 1]));
  return 100;
}

bool onCharger() {
  uint32_t status = 0;
  sd_power_usbregstatus_get(&status);
  return status & POWER_USBREGSTATUS_VBUSDETECT_Msk;
}

void deepSleep(uint8_t wakeThreshold) {
  haptics::off();
  ir::stopLearn();
  digitalWrite(PIN_IR_LED, LOW);

  if (Bluefruit.connected()) Bluefruit.disconnect(Bluefruit.connHandle());
  Bluefruit.Advertising.stop();

#ifdef LED_RED
  digitalWrite(LED_RED, HIGH);  // on-board LEDs are active LOW
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_BLUE, HIGH);
#endif

  imu::armWakeOnMotion(wakeThreshold);

  // Wake when IMU INT1 goes HIGH. GPIO states (IMU power, LED pins) are kept
  // in System OFF, which is why the IMU stays alive and watching.
  nrf_gpio_cfg_sense_input(g_ADigitalPinMap[PIN_LSM6DS3TR_C_INT1], NRF_GPIO_PIN_PULLDOWN,
                           NRF_GPIO_PIN_SENSE_HIGH);
  delay(20);
  sd_power_system_off();
  NRF_POWER->SYSTEMOFF = 1;  // in case the SoftDevice is not enabled
  while (true) {
  }
}

}  // namespace power
