# Magic Wand ✦

A 3D-printed wand on a **Seeed XIAO MG24 Sense** that recognizes moves in **any grip**
and fires **infrared codes** (TV, air conditioner). You set it up from a web page in
Chrome over Bluetooth: no app to install.

```
 pick up ─► wake (buzz) ─► do a move ─► recognized (buzz, buzz, looong) ─► IR code ─► TV/AC
                                     ▲
           Wand Workshop (Chrome) ───┘  teach moves · learn/preset codes · bind them
```

## What's in here

| Path | What |
|---|---|
| `firmware/MagicWand/` | The wand firmware (Arduino sketch) |
| `firmware/MagicWand/config.h` | All pins and settings |
| `firmware/MagicWand/default_spells.h` | Factory spells/codes, generated with `EXPORT` |
| `firmware/common/` | Gesture engine source (`make sync` copies it into the sketch) |
| `app/` | Wand Workshop web app (Web Bluetooth, no build step) |
| `test/` | Host tests: gesture engine in a hand simulator, IR encoders, compile check |
| `tools/wand_log.py` | Terminal client: raw commands + record motion to CSV |
| `docs/` | [Protocol](docs/PROTOCOL.md) · [How gestures work](docs/GESTURES.md) |

## Parts

| Part | Notes |
|---|---|
| Seeed XIAO MG24 **Sense** | Motion sensor (IMU) and LiPo charger on board |
| 3.7 V LiPo | On the BAT+ / BAT− pads under the board |
| IR LED (940 nm) + **200 Ω resistor** | Sends the codes. 200 Ω = five 1 kΩ in parallel |
| Coin vibration motor (10 mm, 3 V) | Feedback buzzes |
| 3-pin IR receiver module (VS1838B / TSOP38238) | Learns codes from any remote |
| Microswitch (optional) | Restart / wake / factory reset |

## Schematic

Everything connects straight to the XIAO: no transistors, no MOSFETs.

```
                     ┌───────────────────────────┐
                     │      XIAO MG24 Sense      │
                     │                           │
   IR LED            │                           │
   long leg ─[200Ω]──┤ D0                    5V  ├── (not used)
   short leg ────────┤ GND                   GND ├───────────── GND rail
                     │                           │
   (leave EMPTY) ────┤ D1                    3V3 ├── (not used)
                     │                           │
   Vibration motor   │                           │
   red  ─────────────┤ D2                        │
   black ────────────┤ GND                       │
                     │                           │
   IR receiver       │                           │
   OUT ──────────────┤ D3                        │
   VCC ──────────────┤ D7  (powers the receiver) │
   GND ──────────────┤ GND                       │
                     │                           │
   Button (optional) │                           │
   NO leg ───────────┤ D9                        │
   C leg ────────────┤ GND                       │
                     │                           │
                     │   BAT+  BAT−  (underside) │
                     └─────┬─────┬───────────────┘
                           │     │
                        LiPo +  LiPo −
```

**Pin by pin:**

| XIAO pin | Goes to | Why |
|---|---|---|
| **D0** | IR LED long leg (a 100–200 Ω resistor in series is strongly recommended) | Sends IR codes |
| **D2** | Vibration motor red wire | Buzzes |
| **D3** | IR receiver OUT | Reads a remote when learning |
| **D7** | IR receiver VCC | Powered only while learning a code: clean supply (the motor is never on at the same time) and nothing drawn while the wand sleeps |
| **D9** | Microswitch **NO** leg (its **C** leg to GND; NC unused) | Restart / wake / factory reset |
| **GND** | IR LED short leg, motor black wire, receiver GND, button | Common ground |
| **BAT+ / BAT−** | LiPo | Battery. Charges through the XIAO's USB-C port |
| **D1** | **Nothing** | See below |
| **D4–D6, D8, D10** | Free | |

**About D1:** the MG24 checks D1 at reset, and if it reads LOW it waits for an upload instead of
running: solid yellow LED, no Bluetooth. Anything wired to D1 (an LED included) can pull it LOW.
Keep it empty.

**Things to know about wiring straight to pins:**
- **The IR LED must have its resistor.** Without it, the LED pulls far more current than a
  pin can give and damages the pin or the LED. 200 Ω gives about 10 mA: roughly **1–2 m**
  of range. Don't go below 150 Ω.
- **The motor is right at the pin's limit** (it wants ~60 mA, a pin is rated for much less).
  The firmware keeps every buzz short, which is what keeps this workable. Don't make the
  buzz patterns much longer, and don't change the motor for a bigger one.
- **Receiver pin order** varies by module. Follow the labels on its board (OUT/S, GND/−,
  VCC/+). A bare VS1838B, seen from the front (dome), legs down, is OUT – GND – VCC.
  Swapping VCC and GND can kill it. A 100 nF capacitor across its VCC–GND helps against noise.

## Arduino IDE setup

1. **Arduino IDE → Settings → Additional boards manager URLs**:
   `https://siliconlabs.github.io/arduino/package_arduinosilabs_index.json`
2. **Tools → Board → Boards Manager**: install **Silicon Labs**.
3. **Tools → Board → Silicon Labs → Seeed Studio XIAO MG24 (Sense)**.
4. **Tools → Protocol stack → BLE (Arduino)**. Required.
5. **Library Manager**: install **Seeed Arduino LSM6DS3** and **ArduinoBLE** (latest).
6. Plug in with a data USB-C cable, **Tools → Port → /dev/cu.usbmodem…**, open
   `firmware/MagicWand/MagicWand.ino`, Upload.

**If uploads stop working:** hold **D1 to GND** while pressing reset, then upload. Last resort:
Seeed's `xiao_mg24_erase.sh` (see the Seeed getting-started wiki).

## How the wand behaves

| Event | Buzz |
|---|---|
| Wakes up / ready | one clear buzz |
| Spell recognized | buzz, buzz, looong buzz (and sends its IR code) |
| Move not recognized | nothing |
| Recording a spell | tick, tick, go … then long-short when saved |

### Status light (yellow LED on the board)

| Yellow LED | Meaning |
|---|---|
| Solid ON | Sending IR, recording a spell, or calibrating |
| Fast blink (5 per second) | Learning: waiting for a remote button |
| 3 quick blinks every 4 s | Battery low (below `LOW_BATTERY_PCT`, 15%) |
| Slow blink (1 per second) | Waiting for the app to connect |
| Short blip every 5 s | Connected to the app |
| One long flash | Spell recognized |
| Two flashes | Saved |
| Five very fast flashes | Error or timeout |
| Off | Asleep |

- **Sleep:** after **30 s** without motion (10 min while the app is connected) the wand goes into
  deep sleep. Every 1.2 s it checks for a fraction of a second whether it has been moved;
  picking it up wakes it (one buzz). Change it with `SET sleep <seconds>` or in the app.
- **Button (optional, D9):** tap = restart (also wakes it up). Hold **5 s** = factory reset.

## Default spells (factory reset contents)

Spells you teach live in the wand's flash. To make the ones on a wand the **defaults** for
every flash and factory reset:

1. Serial Monitor (115200, Newline) → type `EXPORT`.
2. Copy everything between `===== BEGIN default_spells.h` and `===== END`.
3. Paste it over `firmware/MagicWand/default_spells.h` and upload.

## USB and Bluetooth

The board has a second small chip that handles USB. With USB unplugged it can stay half-powered
through the serial pin and confuse the board. The firmware restarts Bluetooth advertising every
5 s while nothing is connected. If it still misbehaves on battery, set `#define USE_SERIAL 0` in
`config.h` (the Serial Monitor stops working, everything else keeps working).

## Web app

```bash
make app     # then open http://localhost:8000 in Chrome
```

Web Bluetooth works in Chrome/Edge on macOS, Windows, Android and ChromeOS, not on iPhone.
Try the UI without a wand at `http://localhost:8000/?demo`.

## First session with the wand

1. **Connect** in Wand Workshop (it shows up as `Wand-XXXX`).
2. **Live** tab: wave the wand and roll it in your hand: a swish right should still show
   *tip right*. If directions look wrong, **Settings → Calibrate**.
3. **IR codes**: on a spell card press **📜 Library**: pick device, brand and button, press **Test**
   while pointing at the device, then **Save** to bind it. Or *Learn* by pointing the original remote
   at the receiver from about 5 cm and pressing the button.
4. **Spells**: name a slot (or use a suggestion), press **Record** 3 times, pick the IR code.
5. **Test mode**: cast for a while and check what it recognizes, then turn it off.

### IR codes by hex

Read a remote's hex with `tools/IrDump` (one line per button press, e.g. `0xB24D1FE048B7 x2`),
then store it from the Serial Monitor with the protocol it uses:

```
HEX 0 coolix AC_20  B24D1FE048B7      # Midea-type AC, sent 2x (coolix default)
HEX 1 coolix AC_off B24D7B84E01F
HEX 2 nec    TV_pwr 20DF10EF          # LG / most NEC TVs
HEX 3 samsung TV_pwr E0E040BF
HEX 4 sony   TV_pwr A90               # Sony, sent 3x
```

Add a number at the end to change how many times the frame is sent (1–4). `DUMPC <slot>` shows
what's stored.
