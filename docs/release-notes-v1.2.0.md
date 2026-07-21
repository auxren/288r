# 288r community firmware — v1.2.0

Released 2026-07-21. **Updating preserves your saved presets.** Everything below is
field-reported, root-caused, fixed, and verified on hardware — most of it driven directly by
community testing of the v1.1.1 release candidates. 30 host regression suites.

## New features

- **Deterministic rear DIPs** — the headline discovery: the rear DIP switches are wired
  *through the scanned panel matrix*, so every earlier firmware read them on an electrically
  disconnected (floating) pin — long-range mode was a per-boot coin flip. All three functions
  (range, resolution, bandwidth) now latch reliably a few milliseconds after the panel
  initializes, every single boot.
- **DIP 1 = "+2 octaves" (×4)**, replacing the stock's ×10 table: ×4 composes in pure octaves
  with the ×1/×2/×4 switch (every combination fully delivered; DIP+×4 reaches the full ~19 s
  memory cleanly), where ×10 saturated the octave switch, clamped pitch-mode echoes, and made
  looper cycles take up to 40 s. Field-verified: "a good compromise, behaves well."
- **Self-sufficient flasher** — if no flashing tool is installed, it offers to install one
  (or a no-admin pip stack), bundles a driver for the old ST-Link dongles that ship in 288r
  kits, survives folders with spaces, and resets the board correctly after flashing. Every
  piece bench-proven, including a full flash through a kit dongle.

## Fixed

- **Momentaries/looper dead in pitch mode**: the panel tick was pass-counted and pitch-mode
  DSP stretched it to ~0.4 s per read — flicks fell between samples. Ticks are now clocked
  from the audio block clock (~5 ms, identical in every mode). Bonus: preset gestures, LEDs,
  and control smoothing in pitch mode were all quietly degraded by the same bug.
- **Pulse CV inputs ignored short triggers** (281e/251e): now edge-latched at ~3 kHz.
  Field-verified.
- **cal. left the multiplier pinned** after preset recall: cal. now releases all pins
  immediately. Field-verified.
- **Pitch + knob full CCW** "cuts the output": confirmed on the bench as the designed clean
  dry bypass (unity snap) — documentation clarified rather than behavior changed.
- **Long-mode clicks investigation closed**: units that click in long-delay mode (with a clip-
  LED flash, also on stock firmware) have marginal delay RAM at deep addresses — the
  reference unit soaked 5 hours in ×4 with zero events. Documented; despike mitigation
  available on request.

## Known / open issues

- **Presets cannot recall sliders/mixer knobs/phase switches** — analog controls with no
  path into firmware, on any firmware ever. Now stated emphatically in the manual. The
  proposal to make presets deeper (interleaved half-step tap patterns → 16-tap presets) is
  the leading design discussion ([#5](https://github.com/auxren/288r/issues/5)).
- **Looper varispeed question** ([#9](https://github.com/auxren/288r/issues/9)): should the
  multiplier repitch loop playback tape-style? Awaiting reference comparisons.
- **Auto mode re-arm** ([#10](https://github.com/auxren/288r/issues/10)): whether LOOP should
  re-capture on new sounds — formal auto-mode pass pending.
- Reference-unit hardware faults (slider 5, signal-in jack) unchanged; slider-0 routing
  still under verification; SENS_REF / ENV_TIME_DEPTH / string-damping ranges remain
  calibrate-by-feel; SWD bench scaffolding intentionally retained.
