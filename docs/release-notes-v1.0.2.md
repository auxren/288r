# 288r community firmware — v1.0.2

Released 2026-07-19. **Updating from any earlier version preserves your saved presets** (see
Fixed, below — this is now structurally guaranteed). Every change is measured on hardware and
gated by host regression suites (29 total).

## New features

- **Transposed multitap pitch mode** — the sliders are a *pitched echo pattern*: each tap
  carries the shifted voice at its own delay time, so your TIME-mode echo rhythm survives into
  pitch mode, transposed. Cycle/octave rescale it identically; feedback patching cascades
  per-echo (uneven tap spacings = tumbling H949-style spirals).
- **Anti-aliased up-shifts** — ~70 dB alias suppression at +1 octave (measured), passband flat
  to 0.01 dB; bright material stays clean at +2 octaves.
- **Envelope → delay time** — with the red switch at *all sounds*, the sens. knob sets the
  depth of the module's signature self-modulation: dynamics push the delay time. CCW = off;
  looper modes keep sens as the capture threshold.
- **Clean bass shifting to ~30 Hz** — period-aware splices (deep-bass purity 0.13 → 1.00 where
  the old correlator couldn't see a full waveform period).
- **ISR load telemetry** — cycle-counter instrumentation readable over SWD.

## Fixed

- **Pitch mode glitched under load** (worst with feedback patched — the loop recirculated the
  garbage): the audio interrupt ran at 104–110% of its budget; it now peaks at 59–83%.
- **Presets could not be guaranteed across updates**: the store sat 12.6 KB ahead of the
  growing image. Now in the top flash sector with a build-time collision guard, sector-scoped
  flash tools audited, and a one-time migration (verified byte-exact) carrying old saves over.
- **Grain-rate "breathing" on shifted tones**: crossfade now adapts to measured splice
  coherence — envelope ripple 0.03–0.33 dB on tones (was up to 1.9 dB).
- **Sub-sample phase jitter** on the pitch voice deep in the delay buffer (float position
  rounding — now sample-exact reads).
- **Residual detune at "knob zero"** (parked ~-44 cents, beating against dry): bottom of
  travel snaps to exact unity; the dry↔shifted transition sliver thinned 4× so CV offsets
  can't park in it.
- **Pitch response to the knob was uneven and laggy**: raw-pot depth (the time-legend
  calibration doesn't apply to pitch), ~15 ms portamento.
- **A railed CV could drive the shifter into garbage** (ratio seen at 16.6): hard ±2-octave
  bound at the voice entry.
- **CI was silently red** on Linux runners (`M_PI` is not C11) — fixed; releases now gate on
  green tests by construction.

## Known / open issues

- **Reference-unit hardware faults** (not firmware): **slider 5** and the **signal-in jack**
  are electrically dead — two analog paths, possibly one shared cause. Repair notes and live
  SWD verification tools are in the repo; the envelope feature covers signal-in's role.
- **Slider 0's routing** (master sum vs dry feed) is unverified — a clean mixed-out test is
  pending; the manual hedges accordingly.
- **Calibrate-by-feel constants**: envelope-modulation span (`ENV_TIME_DEPTH`), auto-trigger
  reference (`SENS_REF`).
- **Auto/looper mode** is a reconstruction of stock behavior; a formal test pass is pending.
- **SWD debug/bench scaffolding intentionally retained** (invisible in normal use); strip
  planned once the hardware repairs are verified.
- Stock front-DIP preset rows are never read — by design (save-chord presets replace them).
