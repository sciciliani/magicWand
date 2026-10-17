# Spell pictures

The app draws every spell itself (SVG: the path of the wand tip, a start dot and an arrow).
To use your own art, drop a square PNG with a transparent background here, named after the
spell's id. It replaces the drawing everywhere (Home, Spells, Devices, Knight, the setup).

| File | Spell | The move (from your point of view) |
|---|---|---|
| `zigzag.png` | ZigZag | Side to side, fast: right, left, right. Tip level. |
| `stroke.png` | Stroke | Tip up high, then one firm stroke straight down. |
| `tipup.png` | Tip up | Tip low, then lift it straight up in one quick move. |
| `circle.png` | Circle | One big round circle, clockwise, about one second. |
| `swishr.png` | Swish right | One smooth sweep of the tip to the right. |
| `swishl.png` | Swish left | One smooth sweep of the tip to the left. |
| `keyturn.png` | Key turn | Tip still, turn the wrist a quarter turn like a key. |
| `poke.png` | Poke | Stab straight forward toward the device and back. |
| `flick.png` | Flick | One quick snap: tip up and straight back down. |
| `swishflick.png` | Swish and flick | Sweep right, then flick the tip down at the end. |
| `counter.png` | Backward circle | One big circle counter-clockwise. |

Size: 256 × 256 px or larger. Keep the move readable at 28 px (the smallest place it's shown):
a thick path, one arrow, little detail. The list of spells lives in `app/spell-book.js`.
