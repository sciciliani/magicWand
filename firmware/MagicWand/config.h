// config.h — every knob for the wand in one place.
//
// Board:  Seeed Studio XIAO MG24 Sense (Silicon Labs EFR32MG24)
// Core:   "Silicon Labs" boards package
//         URL: https://siliconlabs.github.io/arduino/package_arduinosilabs_index.json
// IDE:    Tools > Board > Silicon Labs > Seeed Studio XIAO MG24 (Sense)
//         Tools > Protocol stack > BLE (Arduino)     <-- required (ArduinoBLE API)
#pragma once

#define FW_VERSION "0.3.0-mg24"
#define BLE_NAME_PREFIX "Wand"  // advertised as "Wand-XXXX"

// ------------------------------------------------------------------ pins
// Everything is wired straight to the XIAO (no transistors):
//
//   IR LED       D0 -> LED long leg; short leg -> GND
//                (a 100-200R resistor in series is strongly recommended; without
//                one, IR_DUTY_PERCENT keeps the pin's average current down)
//   Vibration    D2 -> motor; other wire -> GND
//   IR receiver  VS1838B OUT -> D3, VCC -> D7, GND -> GND
//   Button       D9 -> microswitch -> GND
//   Free         D4, D5, D6, D8, D10
//   D1           must stay FREE (see below)
//
// !! D1 (PC01): if it reads LOW at reset the MG24 bootloader waits for an
// !! upload instead of running the sketch. An LED on D1 pulls it LOW: the
// !! wand showed a solid yellow LED and never started. Keep D1 empty.
#define PIN_IR_LED D0
#define PIN_MOTOR D2
#define PIN_IR_RECV D3
// Receiver VCC on D7: the firmware powers it only while learning a code
// (clean supply, no motor at the same time, ~0 mA while the wand sleeps).
// If VCC is on 3V3 instead, this still works (D7 just isn't used).
#define PIN_IR_RECV_PWR D7
#define PIN_DOCK_SENSE D8   // unused (HAS_DOCK_SENSE 0)

#define HAS_IR_RECEIVER 1
#define HAS_MOTOR 1
// Optional microswitch between PIN_BUTTON and GND:
//   short press (< BUTTON_FACTORY_MS)  -> restart the wand (also wakes it up)
//   hold BUTTON_FACTORY_MS             -> factory reset (restores default_spells.h)
// Button legs: C (common) -> GND, NO (normally open) -> PIN_BUTTON.
#define PIN_BUTTON D9
#define HAS_BUTTON 1
#define BUTTON_FACTORY_MS 5000
#define HAS_DOCK_SENSE 0   // the MG24 can't see USB/5V by itself; see README

// On-board (XIAO MG24 Sense)
#define PIN_IMU_PWR PD5    // IMU is powered from this GPIO (Seeed wiki)
#define PIN_VBAT PD4       // battery / 2 (Seeed wiki: v = raw * 2 * 3.3 / 4095)
#define PIN_VBAT_EN PD3    // VERIFY on the schematic; harmless if unused
#define PIN_USER_LED PA7   // yellow, active LOW
#define HAS_STATUS_LED 1   // yellow LED shows wand state (see status.h)
#define LOW_BATTERY_PCT 15 // below this: 3 quick blinks every 4 s

// ------------------------------------------------------------------ IMU
#define IMU_I2C_ADDR 0x6A
#define SAMPLE_HZ 100
#define WAND_AXIS_X 1.0f   // IMU axis pointing handle -> tip (app: Calibrate)
#define WAND_AXIS_Y 0.0f
#define WAND_AXIS_Z 0.0f

// ------------------------------------------------------------------ power
// Deep sleep = EM4 (~2 uA). Waking from EM4 is a full reboot.
#define IDLE_SLEEP_MS 30000UL
#define STILL_ENERGY 25.0f

// How the wand notices it was picked up:
//   WAKE_POLL    (default, needs no extra wiring): sleep 1.2 s, wake for ~25 ms,
//                check whether the wand's tilt changed, go back to sleep if not.
//                Average ~40-120 uA -> weeks of standby on a small LiPo.
//   WAKE_IMU_INT (~2-5 uA): IMU wake-up interrupt on PIN_IMU_INT. Needs the
//                IMU INT1 line on an EM4-wakeup-capable pin: check the
//                XIAO MG24 Sense schematic first, then set PIN_IMU_INT.
#define WAKE_POLL 0
#define WAKE_IMU_INT 1
#define WAKE_MODE WAKE_POLL
#define WAKE_POLL_MS 1200
#define WAKE_TILT_DEG 12.0f      // tilt that counts as "picked up" at wake sensitivity 3 (scales with SET wake)
#define WAKE_SHAKE_G 0.12f       // or this much acceleration while checking
// #define PIN_IMU_INT PA5       // only for WAKE_IMU_INT, VERIFY first
#define WAKE_THRESHOLD 3         // IMU wake-up threshold (x31 mg), WAKE_IMU_INT only
#define CAST_WHILE_CHARGING 0    // only meaningful with HAS_DOCK_SENSE

// ------------------------------------------------------------------ IR
#define IR_CARRIER_KHZ 38
#define IR_DUTY_PERCENT 33   // LED on-time per carrier cycle (33% = gentler on a resistor-less LED)
#define IR_CAPTURE_EDGES 500   // learning buffer; a held button's extra repeats are ignored
#define IR_RAW_MAX 600         // longest raw list accepted by the CODE command
#define IR_MAX_CODES 12
#define IR_LEARN_TIMEOUT_MS 10000
#define IR_END_GAP_US 120000
#define IR_MARK_EXCESS_US 50

// ------------------------------------------------------------------ BLE
#define BLE_NOTIFY_CHUNK 20      // bytes per notification (safe for MTU 23)
// USB serial (debug). Set 0 for battery-only use: an open serial link to the
// on-board USB chip can make BLE and deep sleep misbehave once USB is unplugged.
#define USE_SERIAL 1
// Log Bluetooth events and every line the app sends to the Serial Monitor.
#define BLE_DEBUG 1
// Boot trace: blink + print each setup() step (adds ~2 s to each full boot).
#define BOOT_TRACE 0

// ------------------------------------------------------------------ storage
// NVM3 (the MG24's flash key-value store) is the default. If it doesn't
// compile with your core version, set 0 to use the EEPROM library instead
// (smaller, check the capacity the app shows under Settings > Device).
#define STORAGE_NVM3 0

// ------------------------------------------------------------------ gestures
#define GLOBAL_THRESHOLD 0.45f
#define COOLDOWN_MS 700
