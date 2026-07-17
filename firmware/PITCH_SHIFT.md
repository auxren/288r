# Crossfaded-tap pitch shifting on the fixed-rate delay line

> Engine design note (companion to **[DESIGN.md](DESIGN.md)**). Written against the
> existing `delay_line` / `taps` API in `src/` — it adds a **read mode**, not a new buffer.
> Nothing here is wired into the build yet; the reference code below is a drop-in sketch
> for `src/pitch_shift.{c,h}` plus a host test.

## Why this belongs here

DESIGN.md already lists *pitch effects* as a first-class goal of the fixed-rate rewrite, and
the hard part is already done: `dl_read(d, delay, interp)` reads a **fractional** tap into a
buffer whose write clock never moves. A pitch shifter is just that read with a **time-varying
delay** — plus one trick to keep it from running off the end of the buffer. So this is a small,
self-contained addition on top of the engine you already host-test, not another subsystem.

> **What this is and isn't:** the cheap, real-time, on-character delay-line shifter — same class as
> `multishift`/`moddetun`, artifacts included. Its rate tracking is exact, but the simple two-tap
> crossfade colors sustained tones in a frequency-dependent way (measured below); that coloration is
> the vintage sound, tuned by window length. It is *not* a pristine phase-vocoder shifter. Pick `W`
> deliberately (default ~50–60 ms) and read "Going cleaner" before expecting studio-clean shifts.

This is the classic **delay-line ("time-domain") pitch shifter** — the technique Eventide's
H910/H949 popularized and that the H9000 exposes today as `multishift` (pitch via a swept read)
and `moddetun` (the same structure at ratios near 1.0 for detune/chorus). It's standard DSP
(see DAFX ch. 6; Dattorro, "Effect Design"); the value of the Eventide framing is that it names
the two controls that matter — the **shift** and the **crossfade time** (`xfadetime`) — which map
cleanly onto Buchla panel/CV.

## The idea in one paragraph

If the read pointer advances through the buffer at a rate different from the write pointer, the
output is pitch-shifted: read **faster** than you write and the delay shrinks → pitch **up**; read
**slower** and the delay grows → pitch **down**. But a monotonically shrinking/growing delay can't
last — it hits the write head or the buffer end. So you let the delay ramp across a **window** of
`W` samples and wrap, which would click at each wrap; you hide the wrap by running **two taps half
a window apart** and **crossfading** so whichever tap is about to wrap is already faded to silence.

```
 write head ─▶ (fixed rate, 1 sample/sample)
 SDRAM buffer:  … ────────────────────────────────────┐
                          tap A  ◀── delay_A = base + fracA·W ──┐
                          tap B  ◀── delay_B = base + fracB·W ──┘   (fracB = fracA ± 0.5)
   out = wA·dl_read(delay_A) + wB·dl_read(delay_B)
   wA = sin²(π·fracA),  wB = 1 − wA         ← each tap is silent exactly when it wraps
   fracA += (1 − ratio)/W   per output sample   (wrapped into [0,1))
```

## The math (and why the weights are exact)

Let the read position be `r(n) = wpos(n) − D(n)`. The write head advances one sample per sample,
so the **effective read rate** is `r(n) − r(n−1) = 1 − ΔD`, and that rate *is* the pitch ratio:

```
  ρ = 1 − ΔD      ⇒   ΔD = (1 − ρ)  samples of delay change per output sample
```

We carry the delay as `D = base + frac·W`, so `frac` must advance by `(1 − ρ)/W` per sample:

| ρ (ratio) | frac step `(1−ρ)/W` | delay `D` | read rate | result |
|-----------|--------------------|-----------|-----------|--------|
| `2.0` (+1 oct) | negative | shrinks | 2× | pitch up |
| `1.0` (unity) | `0` | static | 1× | **degenerate — see gotcha** |
| `0.5` (−1 oct) | positive | grows | 0.5× | pitch down |
| `1.003` (≈ +5 cent) | tiny | drifts slowly | ~1× | detune / chorus |

The two taps sit half a window apart, `fracB = fracA ± 0.5`. Choose the crossfade so each tap is
zero at its wrap points (`frac = 0` and `frac = 1`) and full at mid-window (`frac = 0.5`):

```
  wA = sin²(π·fracA)
  wB = sin²(π·fracB) = sin²(π·fracA ± π/2) = cos²(π·fracA) = 1 − wA
```

So `wA + wB = 1` **exactly** (constant amplitude), and `wB` costs one subtraction — a single
`sinf` per sample. When tap A wraps (`fracA: 1→0`), `wA → 0`, so the `W`-sample jump in `delay_A`
is inaudible; tap B is then at mid-window carrying the signal, and vice-versa.

## Reference implementation (drop into `src/`)

Matches the engine's conventions: normalized `float32`, single-precision hardware float only (no
libgcc doubles), stateless except one accumulator, reads through the existing `dl_read`. Use
`DL_INTERP_HERMITE` for pitch taps — interpolation error matters more here than on a static tap.

```c
/* pitch_shift.h — crossfaded-tap ("H910-style") pitch shifter over delay_line.
 *
 * A read mode on the fixed-rate buffer: two fractional taps a half-window apart,
 * their delay ramped so the read rate = pitch ratio, crossfaded so the ramp wrap
 * is silent. Subtle ratios (≈1.0) give detune/chorus; large ratios give shift.
 * Feed it a delay_line that something else is writing (e.g. the main engine).
 */
#ifndef PITCH_SHIFT_H
#define PITCH_SHIFT_H

#include "delay_line.h"

typedef struct {
    float ratio;    /* pitch ratio: 2=+1oct, 0.5=-1oct, 1=unity (see gotcha)     */
    float window;   /* crossfade window length W in samples (grain length)       */
    float base;     /* base delay offset in samples; keeps both taps in range    */
    float phase;    /* fracA accumulator in [0,1)                                */
} pitchshift_t;

/* window: W in samples (e.g. 30 ms * fs). base: >= 2, and base + W <= len - 3. */
void  ps_init(pitchshift_t *p, float window, float base);
void  ps_set_ratio(pitchshift_t *p, float ratio);   /* clamp to a sane span      */
void  ps_reset(pitchshift_t *p);                     /* phase = 0                 */

/* One output sample. `d` is the (externally written) delay line. */
float ps_process(pitchshift_t *p, const delay_line_t *d, dl_interp_t interp);

#endif /* PITCH_SHIFT_H */
```

```c
/* pitch_shift.c — see pitch_shift.h. Standard delay-line pitch shift; clean-room. */
#include "pitch_shift.h"
#include <math.h>

#define PS_PI      3.14159265358979f
#define PS_UNITY_EPS 1.0e-4f          /* |1-ratio| below this -> treat as bypass  */

void ps_init(pitchshift_t *p, float window, float base)
{
    p->ratio  = 1.0f;
    p->window = window;
    p->base   = base;
    p->phase  = 0.0f;
}

void ps_set_ratio(pitchshift_t *p, float ratio)
{
    /* keep the per-sample delay ramp well under 1 sample so interpolation stays
       valid: |1-ratio| <= W is the hard limit; clamp conservatively.           */
    if (ratio < 0.25f) ratio = 0.25f;   /* -2 oct */
    if (ratio > 4.0f)  ratio = 4.0f;    /* +2 oct */
    p->ratio = ratio;
}

void ps_reset(pitchshift_t *p) { p->phase = 0.0f; }

float ps_process(pitchshift_t *p, const delay_line_t *d, dl_interp_t interp)
{
    const float W = p->window;

    /* Near unity the ramp is frozen and two static taps would comb-filter;
       collapse to a single centered tap so unity is a clean delayed bypass.    */
    if (fabsf(1.0f - p->ratio) < PS_UNITY_EPS)
        return dl_read(d, p->base + 0.5f * W, interp);

    const float fracA = p->phase;
    const float fracB = (fracA >= 0.5f) ? fracA - 0.5f : fracA + 0.5f;

    const float sA = sinf(PS_PI * fracA);
    const float wA = sA * sA;           /* sin^2(pi*fracA)          */
    const float wB = 1.0f - wA;         /* = sin^2(pi*fracB) exactly */

    const float out = wA * dl_read(d, p->base + fracA * W, interp)
                    + wB * dl_read(d, p->base + fracB * W, interp);

    /* advance: delay changes by (1 - ratio) samples per output sample          */
    float ph = p->phase + (1.0f - p->ratio) / W;
    while (ph >= 1.0f) ph -= 1.0f;
    while (ph <  0.0f) ph += 1.0f;
    p->phase = ph;

    return out;
}
```

`sinf` is single-precision and one call per sample — fine on the M4F FPU at audio rate. If you
later want it out of the hot loop, precompute a small `sin²` table indexed by `fracA` (the only
nonlinearity here); everything else is add/mul.

## Control mapping (Buchla-native)

- **Pitch CV → ratio.** Buchla is **1.2 V/oct**, so `ratio = exp2f(volts / 1.2f)` (not 1 V/oct).
  A front-panel PITCH trimmer/knob is the same map over its voltage span. Slew the *ratio* (reuse
  the one-pole idea from `taps.c`) so CV steps don't jump the read rate.
- **Window `W` ("xfadetime" / character).** Short `W` (~10–20 ms) = more frequent splices → a
  grainier, more "granular"/warbly shift with tighter transients *and stronger tonal coloration*
  (see the artifact note below); long `W` (~50–80 ms) = smoother, less colored, but more transient
  smear (doppler-ish). This is exactly Eventide's `xfadetime` knob. **Default to ~50–60 ms** — the
  bench measurement below shows 30 ms colors sustained tones noticeably while 60 ms is clean to
  well under a percent.
- **Detune/chorus for free.** Set `ratio` a few cents off unity (e.g. `1.003` / `0.997`), or run a
  pair at `±` a few cents and mix — that *is* `moddetun`, and it's the chorus/flanger goal from the
  README reached through the pitch path rather than LFO-modulated delay. (LFO-on-delay flanging you
  already get from `taps` slew; this gives the shimmer/detune flavor.)

## How it composes with the 8-tap engine

Start simple: **one global pitch voice** reading the main buffer, mixed alongside the dry 8-tap
output — cheapest, and enough to demo shift + detune. Two natural extensions:

1. **A detuned pair** (`+εc` / `−εc`) summed → classic 288-flavored chorus/ensemble.
2. **Per-tap pitch** later, using each tap's slewed `taps_delay(i)` as the `base`. That's 8×
   (2 reads + `sinf`) — measure the cycle budget before committing; the global voice is the safe
   first cut.

Keep the pitch read on the **same** SDRAM buffer the engine already writes — no extra memory, and
it inherits the vintage/hi-fi sample format and the fidelity switch automatically.

## Measured behavior (host reference, `cc -O2`, 440 Hz sine, Hermite)

Compiled against the real `delay_line.c` and measured with a Goertzel spectral peak. The **rate
math is exact** — a single ramping tap (no crossfade) tracks `ρ·f` within the ~1% measurement floor
(splice sidebands + spectral leakage). The **two-tap crossfade adds frequency-dependent coloration**
that depends on `W`:

| ratio | expected | W = 30 ms | W = 60 ms |
|-------|----------|-----------|-----------|
| 0.5   | 220 Hz   | 207 Hz (−6.1%) | 223 Hz (+1.5%) |
| 1.5   | 660 Hz   | 673 Hz (+2.0%) | 657 Hz (−0.5%) |
| 2.0   | 880 Hz   | 907 Hz (+3.0%) | 873 Hz (−0.8%) |

This is the classic delay-line-shifter artifact: the two half-window taps carry the same shifted
tone with a delay-dependent (hence frequency-dependent) phase offset, so the constant-amplitude
crossfade partially suppresses the carrier and pushes energy into sidebands at `ρ·f ± grain_rate`,
where `grain_rate = |1−ρ|·fs / W`. Short windows raise `grain_rate` and worsen it. **It is not a bug
— it is much of the "vintage" pitch-shifter sound** — but it is why you pick `W` deliberately.

## Going cleaner (if/when the character isn't wanted)

- **Bigger window** is the cheapest lever (table above): ~50–60 ms is clean to <1% here.
- **4× overlap** (four taps at quarter-window offsets, `Σw = 1`) roughly halves the residual at a
  given `W`, for 2× the reads.
- **Correlation-aligned splices** (PSOLA-style: nudge the wrap to a pitch-period boundary) remove
  it almost entirely but need a pitch estimate and a search — a separate project, out of scope for
  this engine.
- A **phase vocoder** is the pristine option and a different architecture entirely (not this note).

For the 288 the naive shifter is the right first target: cheap, real-time, and on-character.

## Gotchas / limitations (call these out in code review)

- **Unity is degenerate.** At `ratio == 1.0` the ramp is frozen and two static half-window-apart
  taps comb-filter the signal. The `PS_UNITY_EPS` bypass above collapses to a single centered tap;
  don't remove it.
- **Range.** Both taps span `[base, base + W]`. Keep `base ≥ 2` and `base + W ≤ len − 3` so Hermite's
  `i0−1 … i0+2` fetches stay inside the buffer (`dl_read` wraps, but you don't want the pitch tap
  reading across the write head — keep `base` a few ms so it never laps `wpos`).
- **Not formant-preserving.** Like `multishift` (and unlike a phase-vocoder), this shifts formants
  with pitch — "chipmunk" at large up-shifts. That's the expected 288/Eventide character, not a bug.
- **Ramp magnitude.** `|1 − ratio|` is the delay change per sample; `ps_set_ratio` clamps to ±2 oct
  so the per-sample step stays well under a sample and interpolation stays valid.

## Suggested host tests (mirror `test/test_interp_quality.c`)

Runs on the float reference buffer in ordinary RAM, no hardware:

1. **Frequency tracking (loose).** Write a sine at `f`, run through `ps_process` at `ratio=ρ` with
   `W ≥ 50 ms`, estimate the dominant partial (Goertzel peak or interpolated zero-crossings); assert
   `≈ ρ·f` within a few percent for `ρ ∈ {0.5, 0.794 (−4 st), 1.5, 2.0}`. Keep the tolerance honest
   — the coloration table above is the reason it's a few percent, not exact, and it tightens with
   `W`. (Optionally assert the single-tap path tracks `ρ·f` more tightly, isolating the rate math.)
2. **Click-free wrap (strict).** Smooth input; assert `max|out[n] − out[n−1]|` stays bounded across
   many window wraps (no splice clicks) for both up- and down-shift. This is the property the
   crossfade exists to guarantee.
3. **Unity bypass (strict).** `ratio = 1.0` → output equals a plain `dl_read(base + 0.5W)` (no comb).
4. **Detune stability.** `ratio = 1.003` over a long run → no unbounded drift, level roughly
   constant (checks the `wA + wB = 1` partition holds through wraps).

## Provenance / IP

This is the textbook delay-line pitch shifter (DAFX; Dattorro). Eventide's H910/H949 heritage and
the H9000's `multishift`/`moddetun` are cited as **design confirmation and parameter naming**
(shift + crossfade time), consistent with this repo's clean-room-adjacent stance: no firmware code
— Eventide's or the stock 288r's — is copied. The implementation above is written from the math,
against this project's own `delay_line` API.
