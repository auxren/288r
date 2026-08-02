# 288r community firmware — v1.2.3-rc5 (pre-release for field testing)

**Updating preserves your saved presets.** Supersedes rc4 (which superseded rc3 — all
their fixes included). This rc is entirely about **#24**: pitch-mode modulation is now
continuous through unity.

## New features

- (None — see rc3/rc4 notes for the overdub v2 line, tape-motor multiplier, seam and
  LED fixes, and the unity-departure knob fix.)

## Fixed

- **#24, the full story — pops/clicks/dropouts when pitch-mode modulation crosses
  unity** (bipolar CV vibrato, LFOs, envelopes through the attenuverter). This turned
  out to be three stacked causes, each isolated by a field repro:
  1. the dry→voice crossfade snapped when the ratio glide crossed its band (fixed in
     rc4 — knob departures);
  2. the anti-aliasing filter's engagement at the up-shift boundary was a hard kernel
     swap with a CPU spike (now: hysteresis, a ~5 ms crossfade, and the coefficient
     tables pre-published to fast memory so engagement is instant);
  3. **the core bug**: the exact-unity bypass *jumped* the grain read to the window
     center every time modulation grazed unity — several pops per second under
     vibrato, immune to all smoothing above it. There is no special unity path
     anymore: the grain engine plays continuously through unity, drifting gently to a
     single-grain state only when you *park* there (~1 s), and the true-dry output
     engages only after the ratio has **settled** at unity for ~300 ms — so static
     zero-depth stays bit-exact clean dry, while modulation never suffers a source
     swap. Verified live on the reference unit with a bipolar LFO: clean vibrato,
     no pops, no dropouts.

## Known / open issues

- If captures sound crunchy, check the **input clip LED** while recording (hot
  sources rail the ADC before they sound hot). Sens-knob range guidance is now in
  manual ch. 6 (#13, closed with field measurements).
- Deeper presets (#5). Next up: split-tap loop+live, stereo master option,
  knob-steered internal feedback.
