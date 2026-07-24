# 288r community firmware — v1.2.1

Released 2026-07-24. **Updating preserves your saved presets.** This release is the AUTO
CONTROL release: everything below came from community field reports on v1.2.0 (thank you
@twostroke-ux, @RECLee, and the ModWiggler thread), was root-caused, fixed, and verified on
hardware — including a full bench pass on the reference unit. 34 host regression suites.

## New features

- **Looper varispeed — the multiplier is the tape motor** (#9): while a loop plays in the
  looper positions, the time multiplier (knob or CV) changes playback speed *and* pitch
  together, ±2 octaves, gliding smoothly. Where you captured is always exact as-recorded;
  turn down from there = faster and higher, up = slower and lower. Confirmed stock 288v
  behavior by frame-and-audio analysis of the batchas video — the original's delay knob
  moves the sample clock itself, so loops repitch inherently; this firmware recreates it
  with a fractional-rate read head. *All sounds* keeps constant-pitch tap respacing (the
  chorus/flanger behavior) and pitch mode keeps its depth knob. Bench-measured headroom:
  70% of the ISR budget at maximum rate, 20 s soak flat; field-tested at ×1 and ×4.
- **Signal-gated store end** (#10, designed by field feedback): with an auto-triggered take
  in store end, **playing writes, stopping loops** — the capture ends ~120 ms after the
  input falls silent and the loop is exactly your phrase (cycle length = the cap). The
  staccato/phrase mode. A write-momentary take keeps the classic hold-and-recall.
- **The loop re-arms itself** (#10): a playing loop re-triggers on the next silence→onset
  above the sens. threshold (or an arm-jack pulse) — the batchas stutter, on your 288r.

## Fixed

- **Toggling the red switch or the store selector now resets the looper** (#13, #16): the
  documented stock reset gesture works — *all sounds* releases the loop to live delay,
  re-entering a looper position sits READY armed so a present signal captures immediately.
  Previously the loop state silently survived every switch flip.
- **Click at the loop wrap point** (#9 + bench): loop captures now splice the window seam
  (a ~10 ms crossfade into the loop start's lead-in) and write interpolation guard samples
  past the seam — varispeed's fractional playback crossed the un-guarded seam on every
  pass where the old integer playback could skip it. Owner-verified click-free.
- **Store end could get stuck writing forever** with signal present (rc3 regression,
  caught by field testing before it ever reached a full release).
- The AUTO CONTROL state machine now lives in its own host-tested module with a full
  transition-matrix regression suite — the class of bug behind #10/#13/#16 can't hide
  in untested code again.

## Known / open issues

- Deeper presets ([#5](https://github.com/auxren/288r/issues/5)) — design discussion open
  (interleaved 16-tap patterns are the leading idea).
- Envelope→time self-modulation has no headroom at multiplier full CW
  ([#15](https://github.com/auxren/288r/issues/15)) — bipolar-depth redesign under
  discussion in the thread.
- Percussive material recorded too hot clips at the input ADC (#9 discussion): watch the
  input-mixer LED — if it flashes while you record, back off the input mixer knob. The
  recording path itself has full headroom below the rail.
- Reference-unit hardware faults (slider 5) unchanged; SENS_REF / string-damping ranges
  remain calibrate-by-feel; SWD bench scaffolding intentionally retained.
