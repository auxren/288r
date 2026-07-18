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
- **Eight taps.** Their relative positions come from the per-tap **phase** values (0–160) of the
  active **preset** (A / B / C); the overall time is scaled by the **multiplier** and the
  **short/full cycle** switch.
- **Delay or looper.** The transport can keep recording (delay) or **recirculate** a captured region
  (looper). See [Operation](04-operation.md).
- **Fidelity.** Rear-panel DIP switches select the stored bit depth (a **vintage** lo-fi mode down to
  12-bit, or higher-fidelity modes), read at boot. See [Fidelity & tone](07-fidelity-tone-settings.md).
- **Pulse I/O.** Pulse inputs/outputs interact with the transport (record/recirc, sync).

## What the community firmware changes

The original firmware was abandoned. This project reverse-engineered it and built an open-source
replacement (v1.0.1, running on hardware) that keeps the full feature set while fixing what the
original couldn't:

- **Smooth delay-time modulation** — the original stepped the delay in whole samples, so sweeping the
  time "zippered." The community firmware reads the taps at fractional positions and glides the
  control (~10 ms), so time sweeps are clean — usable chorus, flanger and tape-style pitch effects
  from the knob or CV. *(Validated on hardware.)*
- **Cleaner control** — the original changed coarse delay by re-tuning the audio clock in octave
  steps; the rewrite uses a continuous, smoothed time control. The multiplier knob is calibrated to
  the panel legend (0.4 at the CCW stop, 1.0 at noon, 1.6 at the CW stop), and CV follows the stock
  law: `multiplier = knob + CV × attenuverter`.
- **A working PITCH mode** — a crossfaded pitch shifter feeds all eight taps, with the stock depth
  spans (cycle FULL/SHORT) and bipolar 1.2 V/oct CV. See [Time & pitch](05-time-and-pitch.md).
- **Savable presets** — hold the red **write** switch ~2 s to save the current tap phases and
  multiplier to the selected A/B/C slot, persisted in internal flash. See
  [Presets](06-presets-taps-mixers.md).
- **Auto/looper capture from the sens. knob**, a **clip-indicator LED** for gain staging, and a
  **soft-knee output limiter** so external feedback patching blooms instead of clipping.
- **Higher fidelity** and settings on four rear DIP switches — see
  [Fidelity & tone](07-fidelity-tone-settings.md).

## Hardware at a glance
STM32F429 (Cortex-M4F) · Cirrus CS42448 4-in/8-out codec · 8 MB SDRAM delay buffer · 24-bit audio.
Presets are saved from the panel with a hold-write gesture — there is no menu system
(see [Presets](06-presets-taps-mixers.md)).
