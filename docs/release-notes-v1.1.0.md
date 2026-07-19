# 288r community firmware — v1.1.0

Released 2026-07-19. **Updating preserves your saved presets** (structural guarantee since
v1.0.2). Everything below is measured on hardware and gated by 30 host regression suites.

## New features

- **String mode (Karplus-Strong)** — hold the red switch's *next sound* side ~2 s (LEDs
  twinkle; READY LED breathes while active; same hold exits). The 8 taps become 8 plucked
  strings: the input excites them, **the tap positions are the chord** (preset slots recall
  chords; the default ramp is a pure undertone series), **c.v. in transposes at 1.2 V/oct**
  (direct, always), and **the multiplier knob is damping/brightness**. Each string has its own
  output; the preset outs give four pre-mixed voicings. This is the one structure external
  patching cannot build (string loops must close at zero latency), and it is the single
  deliberate exception to the no-internal-feedback design — scoped entirely to this mode.
  Pitch accuracy measured <1%; ~50% CPU — the lightest mode on the module.
- **Boot health alarm** — if the codec ever fails its verified initialization, every panel
  LED flashes for ~3 s at power-on. Silent audio death is no longer possible.

## Fixed

- **Silent codec failure (concert-grade reliability, owner requirement)**: a reset landing
  mid-I²C-transaction could lock the bus; the next boot then ran with dead or channel-scrambled
  audio I/O until a power cycle. Now: automatic bus recovery at every boot, configuration
  *verified by register readback* with up to five hardware-reset retries, and an SWD-readable
  status. Proof: 15 automated reboot cycles, 15/15 verified on the first attempt.
- **String-mode indicator could be fought by the looper's LED writes** — string mode now owns
  the lamps; the breathing is clean. (The first attempt at this fix taught a lesson in humility:
  a "trivial" brace-wrapping edit produced a dangling-else scope bug — the shipped fix is an
  identifier-level substitution that cannot alter control flow.)
- Manual corrections: preset trimmers documented as the four all-analog preset-out mix banks
  (the MCU never reads them); ×10-extend interaction notes; jack-identity guidance
  ("which output am I monitoring?") added to troubleshooting after it explained three
  separate mysteries.

## Known / open issues

- **Reference-unit hardware faults** (not firmware): slider 5 and the signal-in jack are
  electrically dead (possibly one shared cause; repair notes + live SWD verification tools in
  the repo). Envelope→time on the sens knob covers signal-in's role.
- **Slider 0's routing** (master sum vs dry feed vs hybrid) remains unverified — the clean
  mixed-out discrimination test is still pending; the manual hedges.
- **The multiplier knob serves three modes** (time / pitch depth / string damping); exiting a
  mode leaves the knob wherever that mode needed it. Preset knob-pinning covers recalls; a
  per-mode "catch" memory is designed and slated for v1.2.
- **Calibrate-by-feel constants**: `ENV_TIME_DEPTH`, `SENS_REF`, string feedback/damping
  ranges. Auto/looper mode still awaits a formal test pass.
- SWD debug/bench scaffolding intentionally retained (invisible in normal use).
