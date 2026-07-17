# 5. Time & pitch modes

The **multiplier** scales every tap's delay time at once. A front-panel switch selects how it
behaves. This is the part of the module the community firmware most changes, so this chapter is
explicit about **stock behavior vs. the community firmware**.

## TIME mode

**What it's for:** varying the delay time — chorus, flanger, doppler, tape-wow, and simple "longer/
shorter echo" moves.

- **Community firmware:** the multiplier maps to a **continuous, smoothed** delay-time scale. Turning
  it (or modulating it with CV) sweeps all taps together *smoothly*, with fractional-sample reads, so
  short modulations give clean chorus/flanger and slow ones give clean pitch glides. There is **no
  clock retuning** — the audio always runs at a fixed sample rate; only the read positions move.
  *(Validated on hardware: TIME-mode modulation is smooth.)*
- **Stock firmware:** the same control was **hysteresis-quantized** and coarse changes **retuned the
  audio PLL in octave steps**. The result was a few discrete delay values rather than a continuous
  sweep — the widely-reported "it always did the same sound" limitation that made live time-modulation
  impractical. This is the priority bug the rewrite fixes.

### Slew / glide

Sudden multiplier changes (e.g. a sequenced/stepped CV) are **slewed** so the delay time glides to
the new value rather than jumping. Audio already in the feedback/loop keeps its original delay; only
newly-read taps take the new time. The glide amount is a setting (see
[Settings](07-fidelity-tone-settings.md)); set it to zero for instant, quantized-sounding changes or
higher for tape-like portamento.

## PITCH mode

**What it's for:** transposing the delayed signal (pitch-shift / harmonizer-style textures) as the
multiplier moves.

- **Stock firmware:** modulating pitch swept the read across the buffer, and at the **buffer-wrap
  point** the read position jumped — producing an audible discontinuity (heard on the bench as
  broadband static when the pitch CV was swept hard).
- **Community firmware:** PITCH mode uses a **dual-head crossfaded reader** — two read heads offset by
  half a window, each raised-cosine windowed so whichever head is crossing the wrap is faded out and
  hidden behind the other. This removes the wrap discontinuity. *(Under active development — the naive
  single-window version still adds mild comb coloration; the windowed dual-head design in the rewrite
  targets a clean result. See `firmware/tools/proto.c` and DESIGN.md.)*

## Which mode should I use?

| You want… | Mode |
|---|---|
| Longer/shorter echoes, chorus, flanger, tape wow, doppler | **TIME** |
| Transpose / pitch-shift the delayed sound | **PITCH** |

## Time-CV range

The stock firmware had a **narrow usable Time-CV range** — full CV swing only produced a small change
in delay time. The community firmware treats the CV input scaling as a **calibration** item so the
full modulation range is usable; the exact taper is set in the calibration routine
([Calibration](07-fidelity-tone-settings.md)) and confirmed against the panel on the bench.
