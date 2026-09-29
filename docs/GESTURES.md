# How the wand recognizes moves

## The problem you hit

The IMU measures motion along its **own** axes. If you swish right with the wand held
normally, the gyro sees rotation around, say, its Z axis. Hold the wand upside down and
the same swish shows up on Z with the **opposite sign**. Roll it 90° and it moves to Y.
A model trained on one grip sees a different move in every other grip.

Our simulation shows the same thing: with raw IMU axes, the same classifier gets
**95%** right in the training grip and **26%** upside down.

## The fix: describe the move from the room's point of view

We don't need to know how the wand sits in your hand. We only need two things
that grip doesn't change:

1. **Which way the wand points** (handle → tip). Fixed by how the board sits in the wand.
2. **Which way is up.** The accelerometer always feels gravity.

From those, every sample becomes four numbers:

| Signal | Plain words | Example move |
|---|---|---|
| **tip right** | Is the tip moving left or right? | Swish, "Voco" |
| **tip up** | Is the tip moving up or down? | Flick up "Lux", slash down "Umbra" |
| **twist** | Is the wand spinning like a key? | "Aperio" |
| **thrust** | Is it being poked forward/back? | Stab "Tacet" |

Roll the wand in your hand as much as you like: the tip still moves right when you swish
right, so the numbers stay the same. That's what the **Live** tab plots.

With these signals: **99.9%** recognized across 12 grips (every 30°, upside down included),
**0.1%** misfires, and only 0.5% of random handling (waving it around, picking it up) was
taken for a spell. Run `make test` to reproduce. Caveat: that's a physics simulation
of a hand with noise and human variation, not your real hand. Real numbers will be
lower, and the app's Test mode is how you find out.

## From motion to spell

1. **Cut out the move.** When motion gets brisk the wand starts recording. It stops after
   0.12 s of calm. Very short bumps (<0.14 s) and very long waving (>2.5 s) are ignored.
2. **Normalize it.** The move is stretched to 32 steps and scaled, so fast vs slow and
   gentle vs vigorous versions look alike.
3. **Compare with what it learned.** Dynamic time warping (DTW) measures how far the move
   is from each recorded sample while allowing for sections done faster or slower.
   The closest spell wins if:
   - it's close enough (under that spell's threshold), and
   - it's clearly closer than the runner-up (15% margin), so similar spells don't trigger each other.

No neural network and no retraining: recording a sample just stores it (128 bytes). Up to
8 spells × 4 samples each.

## Suggested spells

All built from the four signals, so all grip-independent:

| Spell | Move | Main signal | Good for |
|---|---|---|---|
| Aperio 🔑 | Twist like turning a key | twist | TV power |
| Lux ⤴ | Quick flick up, then back | tip up | Volume up / AC on |
| Umbra ⤵ | Firm slash down | tip up (negative) | Volume down / AC off |
| Tacet ➶ | Stab forward | thrust | Mute |
| Orbis ◯ | Draw a circle | right + up | Input / source |
| Ventus 〰 | Swish right, then flick down | right, then up | AC power |
| Voco → | Sweep right | tip right | Channel up |
| Zigzag ϟ | Draw a Z | right, up | Fun extra |

Tips:
- Record 3 samples and **change grip between them**. Some variety makes the spell more forgiving.
- Don't train two moves that differ only in size (small circle vs big circle): size is normalized away.
- Mirror moves (swish right vs swish left) are fine: direction is kept.
- If a spell fires by accident, lower **Strictness** in Settings. If it's hard to trigger, record another sample first, then raise it.

## Knobs (firmware/MagicWand/config.h and firmware/common/gesture.h)

| Knob | Default | Effect |
|---|---|---|
| `SegParams.startEnergy` | 120 | Motion needed to start a move (roughly °/s) |
| `SegParams.stopHold` | 12 | Calm samples (×10 ms) that end a move |
| `GLOBAL_THRESHOLD` | 0.45 | Base strictness (app: Settings → Strictness) |
| `COOLDOWN_MS` | 700 | Dead time after a cast |
| `WAND_AXIS_*` | +X | Which IMU axis points at the tip (app: Calibrate) |
