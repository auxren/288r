# 288r community firmware — v1.2.2

Released 2026-07-28. **Updating preserves your saved presets.** This is the release where
the signal-in jack came back from the dead — and where a week of intense field
collaboration (thank you @twostroke-ux and @RECLee) rebuilt the auto-control detector,
fixed pitch mode over loops, and killed two long-standing gremlins. 36 host regression
suites; every change bench-verified.

## New features

- **The signal-in jack lives — as an FM input.** Board tracing on two units independently
  proved the "dead" signal-in jack is a fully wired **codec ADC channel** with its own
  front-panel level pot. It now modulates **whatever the multiplier's domain is**: in TIME
  mode it frequency-modulates **delay time** (chorus → tape-warble → metallic audio-rate
  FM); in pitch mode it modulates the **voice's pitch** (true vibrato at LFO rates — even
  on the dry path at zero depth — FM sidebands at audio rates). **The signal-in pot is the
  depth control**: all-analog, dedicated, labeled. Idle noise can't wobble anything (the
  input engages only above a real-signal floor), and per-tap slew limiting keeps extreme
  settings as Doppler warble rather than digital shred.
- **A rebuilt capture detector** (co-designed in the field): the sens. threshold is fully
  independent of the input-A level pot (matching the parallel panel wiring), immune to
  loop-playback bleed (a slow ambient baseline — triggers are sounds poking clearly above
  it), with full knob range at any input level. Sidechain patches work: input A's dynamics
  can gate what records from B/C.
- **Slider 1 is the direct pitched voice** in pitch mode (instant knob/CV response);
  sliders 2–8 remain the transposed echo pattern; slider 0 stays dry.
- **The input-mixer LED is a true input-overload indicator** (277-style): it lights only
  when the input hits the converter's rail. Internal levels no longer light the panel.

## Fixed

- **Pitch mode over loops** (#19): the voice's reads now follow the loop window — clean
  shifting across the seam at every ratio (was: an internal overrange storm, ~100 garbage
  events/second). The up-shift anti-alias filter rests during loop playback.
- **The "stepped/quantized multiplier knob" in pitch mode**: the splice-alignment search
  could freeze the control loop for up to 3.6 s; it now runs in ~2 ms time slices with
  identical audio quality. Worst-case control freeze measured 3634 ms → 101 ms, and the
  aligner stays active during CV modulation (an rc4 regression, caught by the field).
- **Pitch controls respond immediately**: ~5× faster knob glide, and the pitched echo
  pattern no longer stretches with the rear ×4 switch (loop length still does).
- **The "controls dead until reboot" wedge is dead**: bounded waits + a once-a-second
  self-healing re-sync in the control-ADC driver, with a diagnostic counter.
- **Envelope→time removed** (#15): modulation lives on c.v. in and the signal-in FM path —
  dedicated, labeled controls only. The sens. knob's one job is the looper threshold.
- Loop-seam clicks under varispeed (capture-time splice + interpolation guard samples);
  2-hour loop endurance soak clean.

## Known / open issues

- Deeper presets ([#5](https://github.com/auxren/288r/issues/5)) — design discussion open.
- The rebuilt capture detector and overload LED await final field verdicts
  ([#10](https://github.com/auxren/288r/issues/10),
  [#21](https://github.com/auxren/288r/issues/21)) — behavior was redesigned to field
  specification this cycle.
- FM depth/character constants are first-pass; feel-calibration input welcome.
- On the roadmap next: overdub / sound-on-sound looping, split-tap loop+live mode, a
  stereo master option, and knob-steered internal feedback (Verbos-style) pending a
  control-map bench session.
