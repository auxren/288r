# 288r community firmware — v1.2.2-rc5 (pre-release for field testing)

**Updating preserves your saved presets.** Supersedes rc1–rc4 (all their fixes included:
loop-aware pitch mode, snappier pitch controls, sens corroboration, envelope-master
store end). Bench-verified on the reference unit; 36 host regression suites.

## New features

- **The signal-in jack lives — and it's an FM input.** Board tracing + a live channel
  sweep proved the "dead" signal-in jack is actually a fully wired **codec ADC channel**
  with its own front-panel level pot. It now modulates **whatever the multiplier's domain
  is**: in TIME mode it frequency-modulates the **delay time** (chorus → tape-warble →
  metallic audio-rate FM); in pitch mode it modulates the **voice's pitch** (true vibrato
  at LFO rates — including on the dry path at zero depth — FM sidebands at audio rates).
  **The signal-in pot is the depth control** — all-analog, no menu, no mystery. Per-tap
  slew limiting keeps deep settings as Doppler warble instead of digital shred.
- **Slider 1 is the direct pitched voice** in pitch mode (zero echo delay — knob/CV
  changes are heard instantly); sliders 2–8 remain the transposed echo pattern, slider 0
  stays dry.

## Fixed

- **The "stepped/quantized multiplier knob" in pitch mode — root-caused and fixed.** The
  splice-alignment search could freeze the control loop for up to 3.6 seconds at a time;
  knob readings froze and leapt. The search now runs in bounded ~2 ms time slices (same
  audio quality, verified to the decimal) and pauses entirely while the knob is moving.
  Measured worst-case control freeze: 3634 ms → 101 ms, ~1 ms during actual knob motion.
- **Pitch knob response ~5× faster** (the old glide read as lag — audio-measured).
- **The "controls dead until reboot" wedge is dead**: the control-ADC driver had
  unbounded waits that could hang the panel loop forever on one glitched transfer. All
  waits bounded, plus a once-a-second self-healing re-sync and a diagnostic counter.
- **Envelope→time removed entirely** (#15): modulation now lives on c.v. in and the
  signal-in FM path — dedicated, labeled controls only. The sens. knob's one job is the
  looper threshold.
- 2-hour loop endurance soak: clean (zero clips, zero timeouts, ISR within budget).

- **rc4 artifact reports — both causes fixed**: the pitch-mode splice aligner was being
  starved under CV modulation (vibrato patches ran audibly grittier than rc3 — it now
  stays aligned during modulation and only pauses for big knob flicks), and the new FM
  input listened even to idle noise/bleed (it now engages only above a real-signal
  floor, ~−40 dBFS, with a smooth ramp).

## Known / open issues

- Deeper presets ([#5](https://github.com/auxren/288r/issues/5)) — design discussion.
- rc3 field verdicts still welcome: looper envelope-mastering (#10), presence LED (#21) —
  and if your **signal-in** ever "looked broken," please retest on this build: the old
  verdict was wrong on the reference unit and may be wrong on yours.
- FM depth/character constants are first-pass (feel-calibration input welcome).
- Reference-unit hardware (not firmware): control-board connector repair pending.
