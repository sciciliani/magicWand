# CLAUDE.md — handoff notes for the Magic Wand project

Owner: Santiago. DIY wizard-style wand in a 3D-printed body with a LiPo and an IR LED in the tip.
Moves are recognized in any grip → IR codes (TV/AC), configured from a Web Bluetooth page.
He works in VS Code + Arduino IDE on a MacBook, with Chrome for the app.
Explain hardware and power topics with concrete examples, not dense theory. He asked for that.

**Target board: Seeed XIAO MG24 Sense** (`firmware/MagicWand`). The first version was written for
the XIAO nRF52840 Sense (`firmware/MagicWand_nRF52840`). It's kept for reference but isn't the focus.

## Status

| Area | State | Verified how |
|---|---|---|
| Gesture engine (`firmware/common/gesture.*`) | Done | `make test`: hand simulator, 12 grips × 3 IMU mountings, ~99.9% recognized / 0.1% misfire; raw-axis baseline 26% upside down |
| IR preset encoders (`app/ir-presets.js`) | Done | `make test`: Samsung/LG decode to published hex, Sony timing |
| Web app (`app/`) | Done | Playwright in `?demo` mode (MockWand), desktop + 390 px, no console errors |
| MG24 firmware | Written, **never compiled for ARM, never run** | `make check-fw` only (host syntax check against hand-written stubs in `test/stubs_mg24/`) |
| nRF52840 firmware | Same status | `make check-fw` against `test/stubs_nrf52840/` |

**First job in a new session: install the Silicon Labs core, compile, fix what the real core disagrees with.**

## MG24 architecture

```
firmware/MagicWand/
  MagicWand.ino  100 Hz loop, modes (normal/recording/learning/calibrating), command handler
  gesture.*      copy of firmware/common (make sync). Pure C++, also built on the host
  imu.*          LSM6DS3TR-C via "Seeed Arduino LSM6DS3", power pin PD5, quickGravity() for the sleep poller
  power.*        EM4 deep sleep via ArduinoLowPower; WAKE_POLL (tilt check from backup RAM) or WAKE_IMU_INT
  ir.*           TX: software 38 kHz carrier timed with getCPUCycleCount(), IRQs off only during marks
                 RX: TSOP edge ISR -> raw durations
  storage.*      NVM3 key/value (one object per item, chunked at 700 B) or EEPROM fallback (STORAGE_NVM3 0)
  comm.*         NUS on the native Silicon Labs BLE stack (Protocol stack: BLE (Silabs)) + USB Serial, same text protocol; notifications sized to the MTU
  haptics.*      non-blocking vibration patterns
  config.h       all pins and knobs
```

Key decisions (agreed with Santiago, don't undo without asking):
- **No power switch.** Deep sleep + wake on motion. Battery pads are also the charge path.
- **Grip invariance by physics, not ML.** Tip-right / tip-up / twist / thrust in a gravity frame. TFLite dropped.
- **On-device training = storing templates** (DTW). Users add moves from the app, no reflashing.
- **Config over BLE from Chrome** (Web Bluetooth). iOS unsupported; accepted.
- **IR stored raw.** Presets are encoded in the app.
- **D1 (PC01) stays empty**: an IR LED on D1 kept the wand from booting (solid yellow, no BLE). IR LED is on D0.

## To verify on the real MG24 (rough order)

1. `arduino-cli board listall | grep -i xiao` → real FQBN (Makefile guesses `SiliconLabs:silabs:xiao_mg24`);
   `arduino-cli board details -b <fqbn>` → real protocol-stack option key (guess `protocol_stack=ble_arduino`).
2. Core API names used: `getCPUCycleCount`, `getCPUClock`, `getDeviceUniqueId`, `LowPower.deepSleepMemoryRead/Write`,
   `LowPower.wokeUpFromDeepSleep`, ArduinoBLE `BLE.setEventHandler`/`txChar.subscribed()`,
   `nvm3_default.h` reachable from a sketch (else `STORAGE_NVM3 0`), pin names `PD5 PD4 PD3 PA7`.
3. **Sleep poller**: boot-to-setup time from EM4 decides the average current. Measure. If the BLE
   stack starts before setup(), polling costs more: consider Protocol stack None for a poll build,
   or switch to WAKE_IMU_INT.
4. **IMU INT1 pin**: find it on the XIAO MG24 Sense schematic. If it's EM4-wake-capable, set
   `PIN_IMU_INT` + `WAKE_MODE WAKE_IMU_INT`. That path also needs EM4 GPIO retention so PD5 keeps
   the IMU powered (EMU->EM4CTRL EM4IORETMODE, set in power.cpp).
5. **IR TX**: check carrier frequency with a scope or a phone camera on D0; digitalWrite latency is
   absorbed by cycle-count scheduling. If BLE misbehaves during long codes, move to a TIMER PWM + PRS gating.
6. `PIN_VBAT_EN PD3` is a guess. The battery formula is from the Seeed wiki; check it with a meter.
7. Serial left open blocks EM4 wake-up (Seeed forum): comm::end() calls Serial.end() before sleep.
8. BLE writes from Chrome: app tries 180-byte chunks, falls back to 20 on error. Check that CODE uploads work.
9. Gesture thresholds with real data: `tools/wand_log.py --csv` + the app's Live tab.

## Next steps / ideas (not started)

- Gesture template export/import (`DUMPG` / upload).
- Multiple IR codes per gesture; repeat count for volume.
- Use the XIAO MG24's on-board SPI flash if NVM3 space is tight.
- BLE Battery Service.

## Commands

```
make sync       # after editing firmware/common/gesture.*
make test       # host tests: must stay green (also checks the sketch copies are in sync)
make check-fw   # host syntax check vs test/stubs_* (update stubs if you use new core APIs)
make setup      # arduino-cli: Silicon Labs core + LSM6DS3 lib
make compile / make upload / make monitor
make app        # http://localhost:8000  (?demo = no hardware)
```

Conventions: `gesture.*` has no Arduino headers. A protocol change goes in three places:
the `MagicWand.ino` handler, `MockWand` in `app/wand-link.js`, `docs/PROTOCOL.md`.

## Open questions for Santiago

1. ~~IR LED pin~~ D0 (answered; D1 broke booting).
2. Does he have a TSOP38238 and a coin motor?
3. TV and AC brands.

- UI/naming: no trademarked franchise names (spell suggestions use original Latin-ish names: Aperio, Lux, Umbra…).
