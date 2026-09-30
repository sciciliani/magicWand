# Wand protocol

Plain text lines ending in `\n`, UTF-8. Same protocol on:

- **Bluetooth LE**, Nordic UART Service `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
  (write to RX `…0002`, subscribe to TX `…0003`). The wand advertises as `Wand-XXXX`.
- **USB serial**, 115200 baud (`make monitor`).

Names never contain spaces. The app sends `Living_room_TV` and shows underscores as spaces.
Only `A–Z a–z 0–9 - _` survive; anything else becomes `_`. Max 15 characters.

## App → wand

| Command | Meaning | Reply |
|---|---|---|
| `HELLO` / `INFO` | Full state | `INFO {json}` (below) |
| `PING` | Liveness | `PONG` |
| `STREAM 1\|0` | Live motion features at 25 Hz | `OK STREAM n`, then `F …` lines |
| `REC <g> [name]` | Record one training sample into gesture slot g (0–7). The wand ticks twice and buzzes "go", then waits up to 5 s for a move | `REC g armed`, then `REC g ok <count> <threshold>` or `REC g timeout` |
| `GNAME <g> <name>` | Rename gesture | `OK GNAME g` |
| `GCLR <g>` | Forget gesture samples | `OK GCLR g` |
| `LEARN <c> [name]` | Capture a button from a remote into IR slot c (0–11), 10 s window. The capture is decoded (see below) | `LEARN c waiting`, then `LEARN c ok <bits>` + `IRCODE …` / `timeout` / `unsupported` |
| `CODE <c> <khz> <name\|-> <d1,d2,…>` | Upload a raw code: µs durations, mark first. Decoded on arrival | `OK CODE c <durations>` + `IRCODE …`, or `ERR CODE can't decode` |
| `HEX <c> <nec\|samsung\|coolix\|sony> <name> <hex> [repeats]` | Store a code from its hex value (as IrDump prints it) and a protocol's timings | `OK CODE c <durations>` + `IRCODE …` |
| `DUMPC <c>` | Read a code back (backup) | `CODE c khz name d1,d2,…` + `IRCODE …` |
| `CNAME <c> <name>` | Rename code | `OK CNAME c` |
| `CCLR <c>` | Delete code (and unbind it) | `OK CCLR c` |
| `SEND <c>` | Fire a code now | `OK SEND c` |
| `TRY <khz> <d1,d2,…>` | Send a raw code once without storing it (the app's Remotes tab uses it to test library codes) | `OK TRY` / `ERR TRY can't decode` |
| `BIND <g> <c\|->` | Gesture g sends code c (`-` = nothing) | `OK BIND g c` |
| `CAL` | Calibrate wand axis: hold the tip straight up, still, for 1 s | `CAL ok x y z` / `CAL fail` |
| `SET thr <0.1–2>` | Global recognition threshold (lower = stricter) | `OK SET …` |
| `SET sleep <5–3600>` | Seconds without motion before deep sleep | `OK SET …` |
| `SET wake <1–63>` | Wake-on-motion threshold (×31 mg) | `OK SET …` |
| `SET haptics 0\|1` | Vibration feedback | `OK SET …` |
| `SET name <name\|->` | Rename the wand (max 11 chars, `-` = factory name). The wand restarts and comes back as `Wand-<name>` | `OK SET name Wand-<name> (restarting)`, then the link drops |
| `BUZZ <ms>` | Test the motor | `OK BUZZ` |
| `BAT` | Battery | `BAT <pct> <volts> <charging>` |
| `SLEEP` | Deep sleep now | `SLEEP requested` |
| `RESET yes` | Factory reset (erase flash) | `OK RESET` |
| `EXPORT` | Print all spells, IR codes and bindings as a ready-to-paste `firmware/MagicWand/default_spells.h` (loaded on first boot and factory reset) | lines between `BEGIN`/`END` markers |

## Wand → app (unsolicited)

| Line | Meaning |
|---|---|
| `BOOT <name> <fw> wake=<0\|1>` | Just booted (`wake=1`: woke from deep sleep) |
| `F <right> <up> <twist> <thrust> <energy>` | Features ×100 (grip-independent), only while `STREAM 1` |
| `SEG <n>` | A motion segment of n samples was detected |
| `MATCH <g> <dist> <second> <accepted>` | Classifier result (g = −1 if nothing trained) |
| `CAST <g> <c> <fired>` | A spell was recognized; `fired` = 0 while charging or if unbound |
| `BAT <pct> <volts> <charging>` | Every 30 s while connected |
| `SLEEP <why>` | About to enter deep sleep (BLE drops) |
| `IRCODE <c> <name> <bits> bits x<repeats> <hex> (timings)` | Human-readable summary of a stored code |
| `ERR <text>` | Something went wrong |

## INFO json

```json
{"name":"Wand-A1B2","fw":"0.1.0","bat":76,"volts":3.92,"chg":0,"axis":[1,0,0],"thr":0.45,"sleep":30,"wake":3,
 "haptics":1,"motor":1,"irrx":1,"store":"nvm3",
 "g":[{"n":"Aperio","c":3,"t":0.52,"b":0}, … 8 entries],
 "c":[{"n":"Samsung_Power","k":38,"l":67}, … 12 entries]}
```

`g[].c` sample count, `g[].t` acceptance threshold, `g[].b` bound code (−1 none);
`c[].k` carrier kHz (0 = empty slot), `c[].l` number of durations.

## How IR codes are stored

Codes are stored **decoded**, not as raw pulses: header mark/space, bit mark, "1"/"0" timing,
the data bits as hex (in the order they're sent) and how many times the frame repeats, with the
gap between repeats. Example, a Midea/Coolix AC at 20°C:

```
IRCODE 0 AC_20 48 bits x2 B24D1FE048B7  (hdr 4692/4692 bit 552 one 1656 zero 552 gap 5244 pulse-distance 38kHz)
```

Learning keeps the first frame with at least 8 bits plus its identical repeats (up to 4). A
different frame after that (an NEC "repeat" burst, or the next part of a multi-part AC code) ends
the code. Pulse-distance (NEC, Samsung, LG, Coolix, most ACs) and pulse-width (Sony) are supported.
