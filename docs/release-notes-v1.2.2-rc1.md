# 288r community firmware — v1.2.2-rc1 (pre-release for field testing)

Release candidate for the pitch-mode reports on v1.2.1
([#19](https://github.com/auxren/288r/issues/19),
[#20](https://github.com/auxren/288r/issues/20)). **Updating preserves your saved
presets.** Bench-verified on the reference unit (objective clip-counter and ISR-load
telemetry); field ears welcome. 34 host regression suites.

## New features

- (none — targeted fix release)

## Fixed

- **Pitch mode now works over loops** (#19): the pitch voice's reads follow the loop
  window, so a recirculating loop shifts cleanly across the seam. Previously every wrap
  broke the grain geometry — bench-measured 100+ internal overrange events/second with a
  silent input (the input-mixer LED flashing over silence was this bug, not your levels);
  after the fix the counter reads zero at every ratio, in both loop-window orientations.
  The up-shift anti-alias filter rests during loop playback and re-engages live.
- **Pitch-mode controls respond immediately in ×4 long-range mode** (#20): the pitched
  echo pattern no longer stretches with the rear extend switch (stock never had one, and
  scaling the pinned minimum made every knob/CV change take seconds to be heard). Loop
  *capture length* still composes with ×4 as before.
- String mode no longer engages the looper tape motor (varispeed) behind the scenes.

## Known / open issues

- Deeper presets ([#5](https://github.com/auxren/288r/issues/5)); env→time headroom at
  full CW ([#15](https://github.com/auxren/288r/issues/15)); input-stage clipping on hot
  percussive sources is gain staging — watch the clip LED while recording (#9 discussion).
- Reference-unit hardware faults (slider 5) unchanged; SWD scaffolding retained.
