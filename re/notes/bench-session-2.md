# Bench session 2 — flash + audio validation of Patch 1 (2026-07-16)

ST-Link flashing + Saleae Logic Pro 8 capture (ch0 = In A, ch1 = Mixed Out, analog 1.5625 MS/s),
driven headless via openocd + the Logic 2 automation API. All spectrograms in scratchpad.

## What we flashed and heard
- **Patch 1 (interpolated delay tap) flashed & verified** on the real unit. Confirmed in flash: the
  `b.w` detour at `0x08001aa6`, the cave at `0x08007000`, audio streaming after reset.
- **Time (normal multiplier) mode: WORKS.** Interpolation is clean — no static; the delay-time
  modulation is smooth (the chorus/flanger goal). Validated by ear + spectrogram (output keeps clean
  harmonic structure + dark HF space, like stock). **This is the headline: the fix works on hardware.**
- **Pitch mode: STATIC.** In pitch mode the delay time sweeps continuously, so the read pointer
  crosses the buffer-wrap/write-head discontinuity; linear interpolation smears across it →
  broadband static (worse than stock, which clicks once per wrap). Confirmed by A/B: flipping the
  multiplier switch out of pitch → static gone.
- **Residual "digital aliasing / ringing" in time mode** (mild): the time-multiplier's own
  hysteresis + PLL octave stepping (`sub_2030`), which is upstream of the read and NOT addressed by
  the read-interpolation patch.

## Diagnosis / conclusions
- The interpolation approach is validated for the **delay-time-modulation** case.
- **Pitch mode is the known "buffer-wrap glitch"** — plain interpolation can't fix it (there's a real
  discontinuity). Needs the **dual-head crossfade** (already built: `firmware/src/crossfade.{h,c}`,
  the "pitch-wrap fix" in DESIGN.md). This is rewrite-level (2nd read head + per-tap fade state +
  RAM/compute — too much for a code cave).
- **The residual ringing** = the time-control quantization → fixed by the rewrite's continuous,
  slewed, non-PLL time control.
- Earlier "path-B detours are buggy" was a **confound** — the mode (pitch vs normal) changed between
  captures; the full 6-detour patch is NOT catastrophically broken in normal mode.

## Live hardware facts captured (complement bench-session-1)
- RDP Level 0; unit fw == our reference (4-byte unused-vector diff); saved the unit's 512 KB dump as
  the golden restore (`288r-unit-dump.bin`).
- Verified the full flash→capture→analyze→restore loop works headless (openocd + Saleae automation).

## Decision (from this session)
→ **Commit to the clean rewrite.** The patch proved the fix on hardware; the remaining wins (pitch
via crossfade, the ringing via continuous time control, highest fidelity, analog tone) are all
rewrite-level. See DESIGN.md.
