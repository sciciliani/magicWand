// config.h — every knob for the wand in one place.
//
// Board:  Seeed XIAO nRF52840 Sense
// Core:   "Seeed nRF52 Boards" (the NON-mbed one, FQBN Seeeduino:nrf52:xiaonRF52840Sense)
//         The mbed core does not support the deep-sleep / BLE APIs used here.
#pragma once

#define FW_VERSION "0.1.0"
#define BLE_NAME_PREFIX "Wand"   // advertised as "Wand-XXXX" (last 2 MAC bytes)

// ------------------------------------------------------------------ pins
// External parts (all optional except the IR LED):
//   IR LED       D0 -> 100R -> NPN/N-MOSFET gate/base; LED + resistor from 3V3/VBAT
//   IR receiver  TSOP38238 OUT -> D1, VCC <- D2 (powered from a GPIO so it
//                draws nothing while the wand sleeps; it needs < 1 mA)
//   Vibration    coin ERM motor, low side switched by an N-MOSFET on D3,
//                flyback diode across the motor
#define PIN_IR_LED D0
#define PIN_IR_RECV D1
#define PIN_IR_RECV_PWR D2
#define PIN_MOTOR D3

#define HAS_IR_RECEIVER 1   // set 0 if the TSOP isn't fitted
#define HAS_MOTOR 1         // set 0 if the vibration motor isn't fitted

// On-board parts (from the Seeed variant). Fallbacks are nRF pin numbers from
// the XIAO nRF52840 schematic — VERIFY if the build complains.
#ifndef PIN_LSM6DS3TR_C_INT1
#define PIN_LSM6DS3TR_C_INT1 (18)   // P0.11
#endif

// ------------------------------------------------------------------ IMU
#define IMU_I2C_ADDR 0x6A
#define SAMPLE_HZ 100

// Which direction (in IMU axes) points from the handle to the tip. Can be
// recalibrated at runtime from the app ("Calibrate: point the tip up").
#define WAND_AXIS_X 1.0f
#define WAND_AXIS_Y 0.0f
#define WAND_AXIS_Z 0.0f

// ------------------------------------------------------------------ power
// Deep sleep ("System OFF", ~5 uA) after this long without motion.
#define IDLE_SLEEP_MS 30000UL
// ...but stay awake longer while the config app is connected.
#define CONNECTED_SLEEP_MS 600000UL
// "Still" means motion energy below this (deg/s-ish).
#define STILL_ENERGY 25.0f
// Wake-on-motion threshold, in 1/64 of full scale (±2 g) => ~31 mg per step.
#define WAKE_THRESHOLD 3
// While the wand is on its charger (USB or wireless -> 5 V present) gestures
// are ignored, so bumping the stand doesn't turn the TV off. BLE still works.
#define CAST_WHILE_CHARGING 0

// ------------------------------------------------------------------ IR
#define IR_CARRIER_KHZ 38
#define IR_DUTY_PERCENT 33
#define IR_MAX_EDGES 600          // per code (AC remotes can be long)
#define IR_MAX_CODES 12
#define IR_LEARN_TIMEOUT_MS 10000 // wait this long for the user to press a button
#define IR_END_GAP_US 120000      // silence this long ends a capture
#define IR_MARK_EXCESS_US 50      // TSOPs stretch marks by ~50 us; corrected on capture

// ------------------------------------------------------------------ gestures
#define GLOBAL_THRESHOLD 0.45f
#define COOLDOWN_MS 700           // ignore motion right after a cast
