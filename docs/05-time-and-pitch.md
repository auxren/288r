# 5. Time & pitch modes

The **multiplier** scales every tap's delay time at once. A front-panel switch selects how it
behaves. This is the part of the module the community firmware most changes, so this chapter is
explicit about **stock behavior vs. the community firmware**.

## TIME mode

**What it's for:** varying the delay time — chorus, flanger, doppler, tape-wow, and simple "longer/
shorter echo" moves.

- **Community firmware:** the multiplier maps to a **continuous, smoothed** delay-time scale,
  calibrated so the **panel legend reads true** — 0.4× at the counter-clockwise stop, 1.0× at noon,
  1.6× at the clockwise stop, with every printed mark verified against the knob. Turning it (or
  modulating it with CV) sweeps all taps together *smoothly*, with fractional-sample reads, so
  short modulations give clean chorus/flanger and slow ones give clean pitch glides. There is **no
  clock retuning** — the audio always runs at a fixed sample rate; only the read positions move.
  *(Validated on hardware: TIME-mode modulation is smooth.)*
- **Stock firmware:** the same control was **hysteresis-quantized** and coarse changes **retuned the
  audio PLL in octave steps**. The result was a few discrete delay values rather than a continuous
  sweep — the widely-reported "it always did the same sound" limitation that made live time-modulation
  impractical. This is the priority bug the rewrite fixes.

### Slew / glide

Multiplier changes — from the knob, the CV, or anything else — **glide** to the new value over
about 10 ms rather than jumping. (v1.0.1 fix: earlier community builds effectively snapped the
taps to each control update, which put a broadband "zipper" on any modulation; the glide removes
it, and CV chorus/flanger modulation is now clean.) Audio already in the feedback/loop keeps its
original delay; only newly-read taps take the new time. Stepped CV still reads as steps — the
glide is short enough to track a sequencer, long enough to hide the control quantization.

## PITCH mode

**What it's for:** transposing the delayed signal — subtle detune thickening through obvious
downward shifts, following the stock panel semantics.

- **Stock firmware:** flipping the switch to *pitch* pinned the delay to its minimum and turned the
  multiplier knob into a pitch-down depth control. Modulating it swept the read across the buffer,
  and at the **buffer-wrap point** the read position jumped — producing an audible discontinuity
  (heard on the bench as broadband static when the pitch CV was swept hard).
- **Community firmware:** the same controls, on a crossfaded two-tap shifter that splices cleanly.
  All owner-tested on hardware:
  - **The sliders are a pitched echo pattern** (stock semantics): each tap output carries the
    shifted voice **at that tap's own delay time** — the same echo spacing you set in TIME mode,
    transposed. Cycle and octave rescale the pattern exactly as in TIME mode; slider 0's exact
    routing is still being verified on hardware (master sum vs dry feed). At zero depth the output is the clean dry signal.
  - **Knob = pitch-down depth**, smooth across the whole rotation (in this mode the knob uses its
    raw travel, not the 0.4×–1.6× legend marks — those are a time scale and don't apply). The
    bottom of the travel snaps to **exact unity**, so fully counter-clockwise is a clean bypass
    with no residual detune beating against the dry signal.
  - The **cycle switch sets the depth span**: *full* reaches about −1 semitone (−1.07 st) at full
    knob; *short* reaches about −4.75 semitones. These are the stock values.
  - **c.v. in adds bipolar pitch at 1.2 V/oct**, through the −/+ attenuverter (center detent =
    CV ignored; counter-clockwise inverts). The total shift is bounded to ±2 octaves, so a hot or
    railed CV can't drive the shifter into garbage.
  - Pitch changes **glide over ~15 ms** — fast enough to track your hand on the knob, slow enough
    that stepped CV doesn't zipper.
  - **Quality (measured):** pitch purity 0.992–1.000 from −1 semitone to +1 octave — clean on
    sustained tones and on chords. Splices are waveform-aligned and the crossfade adapts to the
    material, so there is no grain-rate pumping on tones and no wrap static.
  - Because every tap sits at a different echo time, the channels are naturally decorrelated —
    phase-inverted slider pairs comb into new colors instead of cancelling, and the individual
    outputs spread cleanly across a multichannel rig.

**Trick — pitch spirals:** in pitch mode, patch a tap output back into the input. Every pass
through the loop is shifted *again*, so each repeat steps further down (or up, with CV) — the
classic cascading-shift spiral. With the echo pattern in play, each tap spirals at its own
rhythm — set uneven tap spacings for tumbling, arpeggio-like cascades.

## Which mode should I use?

| You want… | Mode |
|---|---|
| Longer/shorter echoes, chorus, flanger, tape wow, doppler | **TIME** |
| Transpose / pitch-shift the delayed sound | **PITCH** |
| Cascading pitch spirals (with external feedback patching) | **PITCH** |

## Time-CV range

The stock firmware had a **narrow usable Time-CV range** — full CV swing only produced a small change
in delay time. In the community firmware the full CV range is usable, and the **−/+ attenuverter**
is live: the multiplier is *knob + CV × attenuverter*, so the attenuverter sets how much (and in
which direction) the CV moves the time. At the center detent the CV is ignored entirely;
counter-clockwise inverts it. This works from power-on in **both** modes — in PITCH mode the same
attenuverter scales the pitch CV.

**Envelope → delay time (the module's signature self-modulation).** With the red AUTO CONTROL
switch at **all sounds**, the **sens. knob sets envelope-modulation depth**: your playing
dynamics push the delay time — attacks stretch it (with a doppler pitch dip as it glides),
decays let it drift back. Fully CCW = off; the auto LED glows while the envelope is actively
pushing. In the looper modes (red switch center / next sound) the sens. knob returns to its
capture-threshold duty. *(The panel's signal-in jack was the stock's intended source for this;
on the reference unit that jack is electrically dead, so the feature runs from the sens
channel instead — musically equivalent, with a working depth knob.)*

**Pitch quality, measured on hardware:** up-shifts are anti-aliased (a ratio-tracked filter —
about 70 dB of alias suppression at +1 octave, so bright material stays clean), splices are
period-aware down to roughly **30 Hz** (deep bass shifts cleanly instead of thumping at the
grain rate), and the crossfade adapts to the material so sustained tones don't "breathe."

## String mode (Karplus-Strong)

A gesture-entered third mode: **hold the red switch's *next sound* side ~2 s** — the LEDs
twinkle and the 8 taps become **8 plucked strings** (the READY LED breathes while you're in the
mode; the same hold exits, and your delay is untouched underneath). Whatever enters the input
*plucks* the strings — percussive material plays them as notes; sustained material bows them.

- **The tap positions are the chord** — each tap's position sets its string's interval, so the
  A/B/C preset slots recall *chords*, and the default ramp pattern is a pure undertone series.
- **c.v. in transposes the whole chord at 1.2 V/oct** — always, in both TIME/pitch switch
  positions, and *directly* (the attenuverter is bypassed here: attenuation would break V/oct
  tracking). Sequence it like an oscillator bank.
- **The multiplier knob is damping/brightness**: clockwise = brighter, longer ring;
  counter-clockwise = dark and thuddy.
- Each string has its own output (its tap's jack), the sliders mix them, and the preset outs
  give four pre-composed voicings of the chord.

This is the one structure that cannot be patched externally (a string loop must close with
zero extra latency, and the codec round trip is ~1 ms), so it is the single deliberate
exception to the no-internal-feedback design — scoped entirely to this mode.
