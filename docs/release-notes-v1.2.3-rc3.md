# 288r community firmware — v1.2.3-rc3 (pre-release for field testing)

**Updating preserves your saved presets.** Supersedes rc1/rc2 — this is the big
stability cut of the overdub cycle, driven by a marathon owner bench session.

## New features

- **Overdub / sound-on-sound, production quality**: hold the recirc momentary while a
  loop plays — everything at the input layers in for as long as you hold. This rc
  rebuilds the write path end-to-end: a slew-gain write limiter lets dense stacks bloom
  with tape-style compression (no flat-topping, no distortion baked into layers —
  measured 0.02% THD on a worst-case 30-layer stack), layered input is properly
  resampled onto the loop's varispeed clock (overdub onto a slowed/sped loop is clean
  at any rate), and a gentle high-cut on the layered input (tape-natural) blocks
  ultrasonic feedback squeal through sound-on-sound mixer patches.
- **The multiplier is a true tape motor on playing loops**: turning it varispeeds the
  loop *without* moving the tap pattern (fixed play heads, exactly like the original's
  PLL varispeed) — no more transient zipper while the knob moves. Live delay keeps the
  classic respacing behavior (that's your chorus/flanger). Applies to any playing loop,
  including a manual recirc in *all sounds*.
- **Overload self-protection**: sound-on-sound at extreme varispeed rates now degrades
  gracefully (the overdub releases itself if the processor saturates) instead of
  freezing the module. Tap reads drop one interpolation grade only while the overdub is
  physically held, buying the headroom back.

## Fixed

- **The per-wrap "zipper/fuzz" in captured loops**: punching recirc used to run the
  seam crossfade as one large memory burst while the write head sat exactly at the
  seam — the resulting audio tear was *recorded into the loop* and replayed every
  pass. The splice is now amortized over ~1.3 ms of normal processing; punches are
  clean and nothing is baked at the seam.
- **LED/pulse noise bleed**: hard edges on the DSP-driven indicator pins couple into
  the audio path (a hardware-layout trait of the 288r). The AUTO/presence LED no
  longer chatters at its threshold (~100 ms dwell hysteresis), string-mode breathing
  no longer runs a 3 kHz software PWM, and the end-of-cycle blip is a single short
  pulse. Diagnostic per-pin mute masks remain available over SWD.
- **Above-full-scale content**: overdub writes are hard-clamped at full scale — brief
  limiter-settling overshoot can no longer bake >0 dBFS samples into a loop.
- rc1's min-statistics capture floor carries forward (staccato keeps full
  sensitivity, loop-playback bleed absorbed — #13 verdict pending).

## Known / open issues

- Loop playback runs the processor near its ceiling (~93%); an optimization pass is
  the next structural item before further features land.
- If captures sound crunchy, check the **input clip LED** while recording — hot
  modular sources rail the ADC on crest peaks well before they *sound* hot (likely
  also the story behind #9's percussive clipping report).
- A forgotten source patched into **signal in** (the FM input) will "FM-garble" loops
  by design — pot down when not in use (manual ch. 5 now covers this).
- Deeper presets (#5); #10/#13 detector verdicts pending.
- Next up this cycle: split-tap loop+live, stereo master option, knob-steered
  internal feedback.
