# 1. Overview

The **288r Time Domain Processor** is a Buchla-format digital recreation of the Buchla 288 — an
**8-tap, voltage-controlled looping delay**. Where a plain delay gives you one echo, the 288r gives
you eight independently-placed taps of the recorded audio, each with its own level, phase and output,
so you can build combs, chords of echoes, rhythmic multitaps, chorus/flanger textures, and loops.

## Signal flow

```
 input ─▶ [input mixer] ─▶ write into delay memory (with optional "vintage" bit-depth reduction)
                                   │
        8 taps ─▶ each tap reads the memory at:  base_time · phase[tap] · multiplier
                                   │
        ─▶ [output mixer]  per-tap level (sliders) + phase-invert + mute
                                   │
        ─▶ 8 individual TAP OUTPUTS   and   the MIXED OUT (analog sum)
```

- **Delay memory** is a large circular buffer (external SDRAM). Audio is written at a moving *write
  head*; each tap reads *behind* the head by a distance set by its phase and the multiplier.
- **Eight taps.** Their relative positions come from the **phase-select** value (0–160) of the active
  **preset bank**; the overall time is scaled by the **multiplier** and the **short/full cycle** switch.
- **Delay or looper.** The transport can keep recording (delay) or **recirculate** a captured region
  (looper). See [Operation](04-operation.md).
- **Fidelity.** A switch selects the stored bit depth (a **vintage** lo-fi mode down to 12-bit, or
  higher-fidelity modes). See [Fidelity & tone](07-fidelity-tone-settings.md).
- **Pulse I/O.** Pulse inputs/outputs interact with the transport (record/recirc, sync).

## What the community firmware changes

The original firmware was abandoned. This project reverse-engineered it and is building an
open-source replacement that keeps the full feature set while fixing what the original couldn't:

- **Smooth delay-time modulation** — the original stepped the delay in whole samples, so sweeping the
  time "zippered." The community firmware reads the taps at fractional positions, so time sweeps
  **glide** — usable chorus, flanger and tape-style pitch effects. *(Validated on hardware.)*
- **Cleaner control** — the original changed coarse delay by re-tuning the audio clock in octave
  steps; the rewrite uses a continuous, smoothed time control.
- **Higher fidelity, optional analog tone**, a **settings/calibration mode**, and pitch-mode fixes —
  see the relevant chapters.

## Hardware at a glance
STM32F429 (Cortex-M4F) · Cirrus CS42888 4-in/8-out codec · 8 MB SDRAM delay buffer · 24-bit audio.
The panel presets are **read live from physical trimmers and DIP switches** — there is no menu system
to store them (see [Presets](06-presets-taps-mixers.md)).
