# 6. Presets, taps & mixers

The 288r's "presets" are not saved files — they are **physical trimmers and DIP switches read live**.
What's on the panel *is* the state. This chapter explains how the taps are placed and mixed.

## The four preset banks

There are four columns of nine trimmers each — the **preset banks** (a default plus A / B / C). Each
trimmer sets one tap's **phase** on the printed **0–160** scale. The **A / B / C** selector chooses
which bank feeds the taps right now:

| Selector | Tap placement source |
|---|---|
| default | Even spacing (20, 40, 60 … 160) |
| **A** | Bank A trimmers |
| **B** | Bank B trimmers |
| **C** | Bank C trimmers |

Because banks are read live, you "store" a pattern simply by setting the trimmers — and you switch
between four patterns instantly with the selector. There is nothing to save and nothing to lose.

## Tap placement math

Each tap reads the delay memory at:

```
tap distance = base_time × (phase[tap] / 160) × multiplier
```

- **phase[tap]** — the active bank's trimmer for that tap (0–160).
- **base_time** — the **tap-time DIPs** (binary, 10 ms per step) and the **SHORT/FULL cycle** switch
  (FULL = full window, SHORT = quarter).
- **multiplier** — the front-panel multiplier (see [Time & pitch](05-time-and-pitch.md)).

So the *bank* sets the pattern shape; the *cycle switch and multiplier* scale the whole pattern in
time.

## Phase-invert & mute

Per-tap DIP switches shape how the taps combine at the **mixed** output:

- **Phase-invert DIPs** (4 × 5-position) — flip a tap's polarity. Inverting some taps turns a plain
  multitap into a comb/allpass-like timbre and changes how taps reinforce or cancel.
- **Mute DIPs** (4 × 4-position) — drop a tap out of the mix entirely.

These affect the analog **mixed** sum. The individual tap outputs still carry every tap.

## The output mixer

Nine **45 mm sliders** set the level of each tap (8 taps + a master) in the analog **mixed** output.
The mix is a real op-amp sum of the eight DAC channels — there is no digital master bus — so the
"analog tone" coloration in the community firmware is applied **per tap** before the sum (see
[Fidelity & tone](07-fidelity-tone-settings.md)).

**Two ways to use the outputs:**

- **Mixed out** — the summed, slider-balanced blend. Best for a single stereo/mono delay voice.
- **8 tap outs** — each tap on its own jack, full level, independent of the sliders. Best for
  multichannel spatialization, feeding taps to different processors, or Buchla-style patch-programming.

## AUTO CONTROL

The firmware includes an envelope-following **AUTO CONTROL** that adjusts gain based on input/output
level (to keep the multitap sum from clipping as feedback builds). Its exact behavior and threshold
are calibration items confirmed on the bench.
