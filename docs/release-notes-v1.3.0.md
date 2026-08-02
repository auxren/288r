# 288r community firmware — v1.3.0

**Updating preserves your saved presets.** This release graduates the entire v1.2.3-rc
line (rc1–rc6) — a release cycle driven almost entirely by field reports, bench sessions,
and tester repros. Thank you to everyone who flashed an rc and reported back; nearly every
fix below traces to a specific report.

## New features

- **Overdub / sound-on-sound.** Hold the red recirc momentary while a loop plays:
  everything at the input layers into the loop for as long as you hold. Existing material
  fades gently (~5%/pass) under new layers; a smooth level-riding limiter lets dense
  stacks bloom with tape-style compression (no flat-topping, no distortion baked into
  layers — 0.02% THD on a worst-case 30-layer stack); each layer softens a whisper of top
  end, tape-style (which also blocks ultrasonic squeal through feedback patches). Layered
  audio is properly resampled onto the loop's varispeed clock, so overdubbing a slowed or
  sped loop is clean at any rate. Write + recirc LEDs light together while held; auto
  re-arm is suspended mid-gesture; if the processor is ever pushed past its limits, the
  overdub releases itself instead of hanging the module.
- **The multiplier is a true tape motor on loops.** Turning it varispeeds any playing
  loop (looper captures and all-sounds manual recircs alike) while **the tap pattern
  holds its capture-time spacing** — fixed play heads, exactly like the original's PLL
  varispeed. Clean pitch bends, no zipper. Live delay keeps the classic respacing
  (chorus/flanger); pitch mode keeps the knob as depth.
- **Pitch modulation is continuous through unity.** Bipolar vibrato (LFO/envelope through
  the attenuverter) sweeps through and across the zero-shift point with no clicks, drops,
  or steps: the unity point, the anti-aliasing filter's engagement, and its internal band
  edges are all continuous now (the filter's coefficients *morph* with the ratio instead
  of switching — measured 50× finer transitions). Parked at zero depth, the output still
  settles to the bit-exact clean dry feed.

## Fixed

- **Loops no longer record a tear at the seam**: punching recirc used to run the seam
  crossfade as one memory burst while the write head sat exactly at the seam — the torn
  audio was recorded into the loop and replayed every wrap ("zipper once per pass"). The
  splice is now amortized into normal processing; punches are clean.
- **Indicator-LED noise bleed** (a board-level trait: hard edges on the indicator pins
  couple into the audio path): the AUTO/presence LED no longer chatters at its threshold
  (~100 ms dwell), string-mode breathing no longer runs an audible-rate PWM, and the
  end-of-cycle blip is a single short pulse.
- **Pitch-mode unity-departure spike** (#24, knob or CV, deterministic): fixed across
  every layer — the dry↔voice crossfade can't snap, the unity bypass can't jump the
  grain read, and the AA engage is hysteretic, crossfaded, and pre-published.
- **Overdub distortion and mid-overdub knob zipper**: the write limiter's gain now moves
  far below audio rate (the old design wrote gain-ripple distortion into the loop), and
  layered input is box-filtered/interpolated across varispeed writes instead of
  zero-order-held.
- **Processor headroom** improved ~17 points (loop playback ~93% → ~76% of the audio
  deadline); high-rate overdub drops one interpolation grade only while physically held.
- Above-full-scale samples can no longer be baked into loop content (hard clamp behind
  the write limiter).

## Known / open issues

- **#24 final verdict pending**: the deep-sweep band-edge fix (rc6/this release) is
  host-verified; the reporter's deterministic 281e repro is the definitive field test.
  Reopen-worthy if any click survives a deep attenuverter sweep.
- If captures sound crunchy, check the **input clip LED** while recording — hot modular
  sources rail the ADC on crest peaks well before they sound hot. Sens-knob range
  guidance (field-measured) is in manual ch. 6.
- A forgotten source patched into **signal in** (the FM input) will FM-garble loops by
  design — pot down when not in use (manual ch. 5).
- Deeper presets (#5) remains open by design discussion. Roadmap next: split-tap
  loop+live mode, stereo master option, knob-steered internal feedback.
