# 288r community firmware — v1.2.1-rc1 (pre-release for field testing)

Release candidate addressing the AUTO CONTROL field reports
([#13](https://github.com/auxren/288r/issues/13),
[#14](https://github.com/auxren/288r/issues/14)). **Updating preserves your saved presets.**
Not yet bench-verified on the reference unit — please test and report in the issue threads.

## New features

- (none — this RC is a targeted fix release)

## Fixed

- **Red-switch toggle now resets the looper** (#13): moving the AUTO CONTROL switch resets
  the capture state machine. Flipping to *all sounds* releases the loop back to live delay;
  flipping back to center sits READY **armed**, so a signal already above the sens. threshold
  captures immediately — the stock reset gesture from the ModWiggler thread now works as
  described. Previously the loop state silently survived switch flips, which is why only the
  sens.-knob sweep (fully CCW and back) could re-trigger.
- **READY LED during capture documented** (#14): the LED going dark for exactly one cycle is
  the write pass recording your loop — its dark time *is* the loop length. The manual now has
  a "reading the LEDs during a capture" section plus a precise three-position table for the
  red switch.

## Known / open issues

- Envelope→time self-modulation is additive-upward, so it has no headroom at multiplier full
  CW ([#15](https://github.com/auxren/288r/issues/15)) — design discussion open in the thread.
- Deeper presets ([#5](https://github.com/auxren/288r/issues/5)), looper varispeed
  ([#9](https://github.com/auxren/288r/issues/9)), auto re-arm behavior
  ([#10](https://github.com/auxren/288r/issues/10)).
- Reference-unit hardware faults (slider 5, signal-in jack) unchanged; SENS_REF and
  string-damping ranges remain calibrate-by-feel.
