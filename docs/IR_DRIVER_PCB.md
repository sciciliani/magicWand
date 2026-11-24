# Mini IR LED Driver (v3)

A 26 × 9.4 mm board that lets a microcontroller pin drive an infrared LED at the current it really needs — about **0.3 A per pulse** — safely, and at the same strength whatever the battery voltage. It works with any 3.3 V or 5 V board: Seeed XIAO (MG24, ESP32-C3/C6/S3, nRF52840, RP2040), ESP32 dev boards, Arduino Uno/Nano/Pro Micro and similar.

It was designed for IR LEDs (universal remotes, IR blasters, IR illuminators), but it fits any load that is best driven at a fixed current of a few hundred milliamps from one GPIO. Section 6 lists those.

| | |
|---|---|
| Size | 26.0 × 9.4 mm, 2 layers, all parts on one side (SMD, factory assembled) |
| Control input | one GPIO, 3.3 V or 5 V logic, on/off or PWM (tested at the 38 kHz IR carrier) |
| Power input | 3.0–5 V, straight from a battery or USB 5 V |
| Output | one LED (or load) on the low side, current-limited to ≈ 0.3 A |
| Parts | 1 MOSFET, 1 NPN transistor, 4 resistors, 1 capacitor |
| Connections | 1.0 mm plated wire pads: VIN, GND, IN, LED+, LED−, plus 2 spare GND |

---

## 1. The problem

### 1.1 A GPIO can't light an IR LED properly
An IR LED's range depends on the current through it. Microcontroller pins are rated for roughly 10–20 mA (some up to 40 mA absolute maximum). That's enough for an indicator LED, but an IR remote at 20 mA only reaches a metre or two. Commercial remotes drive their LED with **hundreds of milliamps in short pulses**: an IR code is bursts of a 38 kHz square wave, so the LED is only on a small fraction of the time, and it can take much more current than its continuous rating.

### 1.2 The usual shortcuts damage the board
Common ways people try to get more current, and why they hurt:

| Shortcut | What goes wrong |
|---|---|
| LED + resistor straight on a GPIO, small resistor | Pin overloaded; it degrades or fails. |
| Several GPIOs tied together for more current | Pins fight each other and still exceed their limits. |
| Transistor (2N2222, BC337…) with its base wired straight to a GPIO, **no base resistor** | A transistor's base behaves almost like a short to ground, so the pin pushes as much current as it can. This is a classic way to quietly damage a microcontroller: it may keep running but leak tens of mA from then on. |
| LED powered from the board's **3V3 pin** | Pulls 100–300 mA pulses through the board's small onboard regulator, which isn't sized for it. It browns out the microcontroller or stresses the regulator. |

### 1.3 Brightness follows the battery
With just a resistor in series, LED current depends on the supply. A Li-ion/LiPo cell goes from 4.2 V full to about 3.3 V empty, so the LED gets noticeably weaker as the battery drains. For a remote, that means it works across the room when charged and stops working from the sofa when half empty.

### 1.4 It has to be tiny and repeatable
In wearables, wands, toys and other small enclosures there's no room for a breadboard-style transistor circuit, and hand-wired versions differ from unit to unit. Ready-made LED driver boards exist but are bigger, more expensive, or hard to get locally.

---

## 2. The solution

A dedicated driver board that:

1. **Takes the LED current from the battery or 5 V directly**, never through the microcontroller or its regulator.
2. **Switches it with a logic-level MOSFET**, so the GPIO only charges a small gate through a resistor. The pin never carries the LED current.
3. **Holds the LED current at ≈ 0.3 A by itself** with a sense resistor and a second transistor. Brightness and range stay the same from full to nearly empty battery, and the LED can't be overdriven.
4. **Keeps the LED off whenever the pin isn't driving it:** during boot and reset, in deep sleep, or if the control wire comes loose.
5. **Has a reservoir capacitor on the board**, so the fast current pulses don't drag the supply down or reach the microcontroller.
6. **Is small and factory-assembled.** Building it into a project means soldering five wires.

### 2.1 Schematic

```
  VIN (battery / 5V) ──┬───────────────────────── LED+
                       │                            │
                      C1 22µF                   [IR LED]
                       │                            │
  GND ─────────────────┴── GND                    LED−
                                                    │
                          GATE                      │ D
  IN ──[R1 220Ω]──┬───────┬─────────────────────── Q1  AO3400A (N-MOSFET)
  (GPIO)          │       │                         │ S
            [R2 100k]     │ C                       │
                  │      Q2 MMBT3904 (NPN) ──B──[R4 1k]── SENSE
                 GND      │ E                       │
                         GND                    [R3 2.2Ω]
                                                    │
                                                   GND
```

In words: the LED sits between the supply and Q1, which connects it to ground through R3. The GPIO drives Q1's gate through R1, and R2 holds the gate low when the GPIO isn't driving it. Q2 watches the voltage across R3 (through R4) and pulls the gate down when the current gets too high.

| Net | Connects |
|---|---|
| **VIN** | VIN pad → C1 → LED+ pad (0.8 mm trace) |
| **LED−** | LED− pad → Q1 drain (0.8 mm trace) |
| **GATE** | R1 (from IN) → R2 (to GND) → Q2 collector → Q1 gate |
| **SENSE** | Q1 source → R3 → R4 → Q2 base |
| **GND** | GND pads, C1, R2, Q2 emitter, R3 (through the bottom ground plane) |

### 2.2 How it works

**Switching.** When the pin goes high, Q1's gate charges through R1 and Q1 turns on. Current flows from VIN through the LED, Q1 and R3 to ground. When the pin goes low, the gate discharges and the LED turns off. The board copies whatever the pin does (steady on, PWM, or the 38 kHz bursts of an IR code) onto the LED at full current. Switching takes a fraction of a microsecond, far faster than the 26 µs period of a 38 kHz carrier.

**Current limit.** The LED current also flows through R3, making a small voltage on the SENSE net. When it reaches Q2's base-emitter threshold (about 0.65 V), Q2 starts to conduct and pulls Q1's gate down. Q1 then turns on just far enough to hold the sense voltage there, so the current settles at:

```
I_LED ≈ V_BE(Q2) / R3 ≈ 0.65 V / 2.2 Ω ≈ 0.30 A
```

(V_BE drifts with temperature, roughly 0.6–0.7 V, so expect 0.27–0.32 A.)

It needs a little headroom to regulate: LED forward voltage (≈ 1.3–1.6 V for IR at this current), plus 0.65 V for R3, plus a few tenths for Q1. So an IR LED is held at 0.3 A from about **3.0 V up to 5 V**. Below that, the LED gets whatever the supply can give, still more than a resistor alone would.

**Why the GPIO is safe even while the limiter is active.** When Q2 pulls the gate down it also loads the pin. R1 caps that at 3.3 V / 220 Ω ≈ 15 mA (≈ 23 mA from a 5 V Arduino), within normal pin limits and only during the pulse.

**Off by default.** R2 holds the gate at ground whenever the pin floats. Many boards leave pins floating at reset, during boot or in deep sleep, and a floating gate can leave a MOSFET half-on: a drained battery and a hot LED.

**Clean supply.** Each IR pulse draws 0.3 A for a few microseconds. C1 (22 µF, right at the board's VIN pad) supplies those edges locally, so the supply wires' inductance doesn't turn them into dips the microcontroller would see.

---

## 3. Components and why each one

| Ref | Part | Package | Value | Why this part |
|---|---|---|---|---|
| **Q1** | AO3400A (Alpha & Omega) | SOT-23 | N-MOSFET, 30 V, 5.7 A | Logic-level: fully on from 2.5 V at the gate, so 3.3 V boards drive it hard. 5 V boards are fine too (gate rated ±12 V). Low on-resistance (tens of mΩ), so it barely heats when fully on. Huge margin over 0.3 A. One of the most common SOT-23 MOSFETs, cheap and always in stock (LCSC C20917). |
| **Q2** | MMBT3904LT1G (onsemi, any brand OK) | SOT-23 | NPN, 40 V, 200 mA | Only pulls Q1's gate down, a few mA at most. Its base-emitter voltage (≈ 0.65 V) is the reference that sets the current. The standard, cheapest small NPN (LCSC C20526). |
| **R3** | RC1206FR-072R2L (Yageo) | 1206 | 2.2 Ω 1% 1/4 W | **Sets the current.** 1% so every board matches. 1206 size for heat: 0.3² × 2.2 ≈ 0.2 W while on, far less on average for pulsed use. Swap only this part to change the current (table below). |
| **R1** | RC0603FR-07220RL | 0603 | 220 Ω | Protects the GPIO and softens the gate-charging spike. Small enough for sub-microsecond switching (220 Ω × Q1's ~600 pF gate ≈ 0.13 µs). |
| **R2** | RC0603FR-07100KL | 0603 | 100 kΩ | Gate pull-down: LED off at boot, reset, sleep, or if the IN wire is loose. Barely loads the pin (33 µA at 3.3 V). |
| **R4** | RC0603FR-071KL | 0603 | 1 kΩ | Feeds the sense voltage to Q2's base. Limits its base current and keeps the current-limit loop stable. |
| **C1** | CL21A226MAQNNNE (Samsung) | 0805 | 22 µF 25 V X5R | Local reservoir for the current pulses. X5R holds its value much better than Y5V. The 25 V rating leaves more real capacitance at 3–5 V (ceramics lose capacitance under voltage). |

### 3.1 Changing the current

Only R3 changes. I ≈ 0.65 V / R3:

| R3 | Current | Typical use |
|---|---|---|
| 6.8 Ω | ≈ 95 mA | Continuous light: IR illuminator, always-on LED |
| 3.3 Ω | ≈ 0.2 A | Pulsed IR, longer battery life |
| **2.2 Ω** | **≈ 0.3 A** | **Pulsed IR, long range (as built)** |
| 1.5 Ω | ≈ 0.43 A | Pulsed IR, maximum range (check the LED's pulse rating) |

### 3.2 Board

- **Size and layers:** 26.0 × 9.4 mm, 2 layers. All parts and traces on top; the bottom is a solid ground plane (short ground paths, some heat spreading).
- **Power traces** (VIN → LED+, LED− → Q1): 0.8 mm. Others: 0.4–0.6 mm.
- **Pads:** 1.0 mm plated holes for wires, no parts fitted.
  - One end: **VIN**, **GND**, **IN**.
  - Other end: **LED+**, **LED−**.
  - Middle: two spare **GND** pads, handy for grounding another module (for example an IR receiver) right there.

| Ref | X (mm) | Y (mm) | Rotation | Pins |
|---|---|---|---|---|
| Q1 | 12.00 | 3.80 | 0° | 1 (G) and 2 (S) left, 3 (D) right |
| Q2 | 7.60 | 3.80 | 180° | 1 (B) and 2 (E) right, 3 (C) left |
| C1 | 3.80 | 6.45 | 90° | |
| R1 | 4.30 | 2.40 | 0° | |
| R2 | 5.10 | 4.80 | 90° | |
| R3 | 15.20 | 1.60 | 0° | |
| R4 | 9.80 | 1.70 | 0° | |

---

## 4. Connecting it

| Board pad | Connect to |
|---|---|
| **VIN** | Battery + (1-cell Li-ion/LiPo, 3–4.2 V) or a 5 V supply. **Not** the microcontroller's 3V3 pin. |
| **GND** | Battery − and the microcontroller's GND (must be shared). |
| **IN** | Any GPIO. No extra resistor needed; R1 is on the board. |
| **LED+** | LED anode (long leg). |
| **LED−** | LED cathode (short leg, flat side). |

| Board | Notes |
|---|---|
| XIAO MG24 / ESP32 / nRF52840 / RP2040, ESP32 dev boards | 3.3 V logic, ideal. Power VIN from the battery. On XIAOs that means the battery pads, not 3V3. |
| Arduino Uno / Nano / Pro Mini (5 V) | Works as is. VIN from the 5 V rail or a battery. While the limiter is active the pin sees about 23 mA; raise R1 to 470 Ω if you want a bigger margin. |
| Arduino libraries | Works with IRremote, IRremoteESP8266 and similar: point the library's send pin at IN. |

Good practice:

- Never feed high-current LEDs from a board's 3V3 regulator.
- Never connect a transistor base or a low-value LED resistor directly to a GPIO.
- Use one GPIO per load; don't tie pins together.
- Disconnect the battery while soldering, and check VIN isn't shorted to GND before powering up.

## 5. Heat and limits

In current-limit mode Q1 absorbs the extra voltage:

```
P(Q1) ≈ (VIN − V_LED − 0.65 V) × I
```

| Use | Load on Q1 |
|---|---|
| **Pulsed (IR codes):** the LED is on only a small fraction of the time | Average power is tiny even at 5 V (≈ 0.9 W while on, a few tens of mW on average). No concern. |
| **Continuous at 0.3 A:** an SOT-23 MOSFET can only shed about 0.3–0.4 W | Keep the headroom small (for example a white LED at ≈ 3 V from a 3.7 V cell), or lower the current with R3. Don't run a 1.4 V IR LED continuously at 0.3 A from 5 V: Q1 would dissipate ≈ 0.9 W and overheat. |
| **LED rating:** typical 5 mm IR LEDs are rated about 100 mA continuous and much higher for short pulses | 0.3 A in 38 kHz bursts is within typical pulse ratings. Always check your LED's datasheet. |

## 6. Other loads this board suits

Anything that is best driven at a **fixed current up to a few hundred mA** from one GPIO, with the load between VIN and the board, and with enough voltage headroom:

| Load | Notes |
|---|---|
| IR LEDs | Remotes and IR blasters (pulsed); IR illuminators for night cameras (continuous: lower the current with R3, see section 5). |
| High-brightness visible or UV LEDs | Steady brightness from a draining battery. |
| Several IR LEDs in series | If VIN covers their total forward voltage + ~1 V (two IR LEDs from 5 V, for example). |
| Small resistive loads, buzzers, vibration motors | It switches them, with the limit acting as stall and short protection. A motor or buzzer also needs a flyback diode across it. |

Not a fit: loads that need more than ~0.5 A, need their own regulator (laser diodes, LED strips with built-in drivers), or run from more than ~5 V without rechecking Q1's heat.

## 7. Testing a board

1. **Before power:** continuity mode, VIN to GND must not beep. In diode mode, LED+ to LED− shows the LED's forward drop.
2. **Idle current:** with IN low, the board should add nothing measurable to the supply current. If it does, the LED is being held on: check R2 and that the pin is really low.
3. **Does it fire:** drive IN (an IR library send, or a simple blink) and look at the LED through a phone camera. IR shows up purple-white; front cameras usually show it best.
4. **Current:** a multimeter in series with VIN shows only the average. To see the ≈ 0.3 A peak, put an oscilloscope across R3: about 0.65 V during each pulse.
5. **Same strength at low battery:** compare range at full charge and at about 3.4 V. It should be the same.

## 8. Known issues and ideas for the next revision

- **C1 and R2 ground pads touch.** The assembler flagged that their ground pads merge into one copper area with no solder mask between them. Electrically fine (both ground), and v3 was assembled as is. Next revision: add a solder-mask dam.
- **Test pad on SENSE,** for checking the current with a scope without probing R3's pads.
- **Reverse-polarity protection on VIN.** A swapped supply would push current backwards through the LED via Q1's internal diode.
- **Second R3 footprint in parallel,** to raise the current by adding a part instead of desoldering one.
