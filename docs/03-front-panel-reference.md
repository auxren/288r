# 3. Front-panel reference

This chapter lists every control and jack and what it does. Control **functions** are documented from
reverse-engineering the firmware and the module BOM; a few exact panel *labels* are still being
confirmed against the physical unit and are noted where relevant.

## Mode / selector switches

| Control | Type | Function |
|---|---|---|
| **cal. / pre-set** | latching toggle | Selects **preset** (normal) or **cal.** In *cal.*, the module runs a live setup mode: shorter buffers, ×2 time multiplier, and the taps follow the **raw** trimmer values instead of the hysteresis-committed preset. |
| **SHORT / FULL cycle** | latching toggle | Scales every tap position 4:1 — **FULL** = long delay window, **SHORT** = quarter-length (per-unit ≈ 44 vs 11 samples). Sets the overall time range. |
| **A / B / C** (preset select) | 3-position toggle | Chooses which stored **phase row** feeds the taps: a default even spacing, or bank A / B / C. See [Presets](06-presets-taps-mixers.md). |
| **resolution** (fidelity) | 2-bit selector | Stored bit depth: **20-bit** (hi-fi), **16-bit**, or **12-bit vintage**. Fixed at boot; sets the SDRAM layout. See [Fidelity](07-fidelity-tone-settings.md). |
| **multiplier: TIME / PITCH** | mode switch | Selects how the multiplier behaves — **TIME** varies delay time (chorus/flanger), **PITCH** tracks pitch. See [Time & pitch](05-time-and-pitch.md). |

## Momentary switches (transport / gestures)

Two panel switches are spring-return (momentary): **SW14** `(ON)-OFF-(ON)` and **SW16**
`ON-OFF-(ON)`. In normal use they act as manual **write / recirculate / pulse** triggers for the
transport. In the community firmware, **holding a momentary gesture at power-up** is how you enter
settings/calibration mode — see [Settings](07-fidelity-tone-settings.md).

## Continuous controls

| Control | Count | Function |
|---|---|---|
| **Output-mixer sliders** | 9 × 45 mm | Per-tap output level in the analog **mixed** sum (8 taps + master). The taps also always appear at their individual outputs regardless of slider position. |
| **Rotary pots** | 7 | Input mixer / time / level controls (POT1–5 log, POT6–7 linear). Exact per-knob assignments are being confirmed on the unit. |
| **Preset trimmers** | 36 (4 banks × 9) | The stored **tap positions** for each preset bank, set to the printed **0–160** scale. Live-read — there is no "save," the trimmer *is* the stored value. |
| **Tap-time DIPs** | 6 × 8-position | Binary tap-time presets in **10 ms** steps. |
| **Phase-invert DIPs** | 4 × 5-position | Per-tap phase inversion in the mix. |
| **Mute DIPs** | 4 × 4-position | Per-tap mute in the mix. |
| **Mode/config DIP** | 1 × 4-position | Mode/config bits. |

## Jacks & indicators

- **Input(s)** — audio (and CV) into the input mixer. The codec provides 4 inputs.
- **Tap outputs** — **8 individual outputs**, one per tap (each tap has its own DAC channel).
- **Mixed out** — the analog op-amp sum of the taps, scaled by the output-mixer sliders.
- **Pulse in / Arm pulse in** — trigger/sync inputs for the transport (record/recirc, cycle sync).
- **Banana jacks** — Buchla-format CV/pulse interconnect.
- **LEDs** (5) — status/activity indicators.

> **Note on "no menu."** The presets are *physical*: the trimmers and DIP switches are read live every
> scan, so what you see on the panel is the current state. There is no preset memory to save or recall
> in the stock firmware. The community firmware adds a small persistent store for *settings/calibration
> only* (not presets) — see [Settings](07-fidelity-tone-settings.md).
