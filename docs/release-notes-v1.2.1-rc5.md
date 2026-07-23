# 288r community firmware — v1.2.1-rc5 (pre-release for field testing)

Release candidate addressing the AUTO CONTROL field reports
([#13](https://github.com/auxren/288r/issues/13),
[#14](https://github.com/auxren/288r/issues/14),
[#16](https://github.com/auxren/288r/issues/16),
[#10](https://github.com/auxren/288r/issues/10),
[#9](https://github.com/auxren/288r/issues/9)). Supersedes rc1–rc4. **Updating preserves your saved presets.**
Not yet bench-verified on the reference unit — please test and report in the issue threads.

## New features

- **Looper varispeed** (#9): while a loop plays, the **time multiplier (knob or CV) is the
  tape motor** — playback speed and pitch move together, ±2 octaves, gliding smoothly.
  Confirmed stock 288v behavior by audio forensics of the batchas video (the loop ramps
  +8→+18 semitones with the hand on the multiplier): the original's delay knob moves the
  sample clock itself, so loops repitch inherently. Recreated here with a fractional-rate
  read head; *all sounds* keeps constant-pitch respacing and pitch mode keeps its depth
  knob, so nothing else changes.
- **store end is now signal-gated** (#10, designed by field feedback): with an
  auto-triggered take in store end, **playing writes, stopping loops** — the capture ends
  ~120 ms after the input falls silent and the loop is exactly your phrase (cycle length =
  the cap). Previously store end went to a silent hold that the re-arm immediately punched
  a new take out of — audibly "stuck in write forever." store beg. keeps the cycle-quantized
  capture; a write-momentary take in store end keeps the classic hold-and-recall.
- **The loop auto re-arms** (#10): a playing loop now re-triggers on the next onset — when
  the input dips to silence and a new sound crosses the sens. threshold (or a pulse hits the
  arm jack), a new capture punches over the old loop. This is the stock behavior shown in the
  batchas 288v video (AUTO CONTROL cycling write/recirc with the playing — the stutter
  effect), surfaced by Mixcatonic on the ModWiggler thread. The sound that triggered the
  current loop can't re-trigger it; silence-then-onset is required, so a sustained drone
  holds the loop steady.

## Fixed

- **Red-switch toggle now resets the looper** (#13): moving the AUTO CONTROL switch resets
  the capture state machine. Flipping to *all sounds* releases the loop back to live delay;
  flipping back to center sits READY **armed**, so a signal already above the sens. threshold
  captures immediately — the stock reset gesture from the ModWiggler thread now works as
  described. Previously the loop state silently survived switch flips, which is why only the
  sens.-knob sweep (fully CCW and back) could re-trigger.
- **store beg./store end toggle now resets the looper** (#16, same root cause as #13): the
  selector was only consulted at the instant a write pass completed, so flipping it while a
  loop played did nothing — the documented stock reset gesture (store beg. -> store end)
  couldn't work. Any physical movement of the selector now resets to READY armed, exactly
  like the red-switch toggle; its resting position still chooses the policy for the next
  capture (store beg. = auto-loop at the cycle boundary, store end = hold until recirc).
- **READY LED during capture documented** (#14): the LED going dark for exactly one cycle is
  the write pass recording your loop — its dark time *is* the loop length. The manual now has
  a "reading the LEDs during a capture" section plus a precise three-position table for the
  red switch.

## Known / open issues

- Envelope→time self-modulation is additive-upward, so it has no headroom at multiplier full
  CW ([#15](https://github.com/auxren/288r/issues/15)) — design discussion open in the thread.
- Deeper presets ([#5](https://github.com/auxren/288r/issues/5)).
- Varispeed ISR headroom is host-proven but not yet bench-measured under worst case
  (rate 4.0 + all taps); telemetry is in place for the next bench session.
- Reference-unit hardware faults (slider 5, signal-in jack) unchanged; SENS_REF and
  string-damping ranges remain calibrate-by-feel.
