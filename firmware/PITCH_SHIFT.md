# Crossfaded-tap pitch shifting on the fixed-rate delay line

> Engine design note (companion to **[DESIGN.md](DESIGN.md)**). Written against the
> existing `delay_line` / `taps` API in `src/` — it adds a **read mode**, not a new buffer.
> **Status: shipped.** The implementation lives in `src/pitch_shift.{c,h}` (core shifter +
> splice alignment) and `src/pitch_voice.c` (CV map + ratio glide), is wired into `main.c`
> as the pitch-mode voice, and is owner-tested on hardware (v1.0.1). This note is the
> design narrative: the base algorithm first, then the three subsystems the shipped code
> adds on top of it, with the measured numbers.

## Why this belongs here

DESIGN.md already lists *pitch effects* as a first-class goal of the fixed-rate rewrite, and
the hard part is already done: `dl_read(d, delay, interp)` reads a **fractional** tap into a
buffer whose write clock never moves. A pitch shifter is just that read with a **time-varying
delay** — plus one trick to keep it from running off the end of the buffer. So this is a small,
self-contained addition on top of the engine you already host-test, not another subsystem.

> **What this is and isn't:** the cheap, real-time delay-line shifter — same class as
> `multishift`/`moddetun`. The plain two-tap crossfade colors sustained tones in a
> frequency-dependent way (measured below); the shipped version adds correlation-aligned
> splices and a coherence-adaptive fade, and the result measures **purity 0.992–1.000 from
> −1 st to +1 oct** — H3000-class on tones, and it holds up on polyphonic material. It is
> still *not* a phase vocoder: formants shift with pitch (the vintage character), and `W`
> remains a deliberate choice (shipped default 60 ms).

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
is inaudible; tap B is then at mid-window carrying the signal, and vice-versa. (The
amplitude-complementary partition is exact only for **coherent** taps — the adaptive fade below
handles the incoherent case.)

## Implementation (`src/pitch_shift.{c,h}`)

Matches the engine's conventions: normalized `float32`, single-precision hardware float only (no
libgcc doubles), reads through the existing delay-line API. Use `DL_INTERP_HERMITE` for pitch
taps — interpolation error matters more here than on a static tap. The shipped state:

```c
typedef struct {
    float ratio;    /* pitch ratio: 2=+1oct, 0.5=-1oct, 1=unity (see gotcha)     */
    float window;   /* crossfade window length W in samples (grain length)       */
    float base;     /* base delay offset in samples; keeps both taps in range    */
    float phase;    /* fracA accumulator in [0,1)                                */
    /* --- de-glitch (H949-style correlation-aligned splices) ---------------- */
    float off[2];    /* per-tap splice offsets (>=0, deeper into the past)       */
    float rho[2];    /* per-tap splice coherence (NCC of the chosen offset)      */
    volatile float pend_rho;  /* coherence for the NEXT wrap                     */
    volatile float pend_off;  /* offset chosen by ps_service for the NEXT wrap   */
    volatile int   pend_tap;  /* which tap it is for (-1 = none)                 */
    int   next_tap;  /* which tap wraps next (0=A at phase 1->0, 1=B at 0.5)     */
} pitchshift_t;
```

`ps_process()` is the per-sample core exactly as derived above — two reads, one `sinf`, the
phase accumulator — plus three subsystems the sketch didn't have. They are what turned the
"vintage, artifacts included" baseline into the measured numbers at the top, and each exists
because a specific artifact was heard or measured on the unit.

### Exact-position reads: `ps_read` (the deep-buffer fix)

All pitch-path reads go through `ps_read()`, which splits the float **distance** into int+frac
*before* the write pointer enters the math and calls `dl_read_frac(d, di, frac, interp)`. The
plain float path computes `(float)wpos − delay`, and a float32 mantissa quantizes that to **1/4
sample** once `wpos` is millions of samples into the SDRAM buffer — on this voice's
continuously-ramping taps that is periodic phase jitter, and it also re-quantized the sub-sample
splice offsets the correlator refines. The distance itself is small (< ~16k samples), so the
split is exact to ~2⁻¹⁰.

Measured (deep-wpos regression, `test_precision.c`): at a **4.2M-sample-deep write pointer** the
shifted output holds **purity 0.999, frequency error 0.00%** — identical to the shallow-buffer
result. The same suite demonstrates the float path's expected coarseness at that position.

### Correlation-aligned splices: `ps_service` (H949-style)

Left alone, each wrap splices the incoming tap at whatever waveform phase it lands on. The fix
is the H949 move: just before a tap wraps, search the buffer near its landing point for the
offset at which the incoming material best matches what the *other* tap is playing, and start
the new grain there.

- **Placement:** `ps_service()` runs in the **main loop**, not the ISR — the search is a few
  hundred k MACs (~1–2 ms), trivial in the superloop, impossible at audio rate. It watches the
  phase, and when a wrap is imminent (within 6% of the window) it searches and posts the result
  for the ISR to consume at the wrap.
- **Ranking:** sign-preserving **NCC²** (as `acc·|acc|/ein`) — a plain `acc/ein` least-squares
  gain is biased toward quiet windows and dug 30 dB holes in enveloped material
  (adversarial-verify finding). Guards: incoming energy ≥ 1% of outgoing, best NCC ≥ 0.5;
  otherwise fall back to offset 0, which is exactly the plain crossfaded shifter.
- **Resolution:** coarse 4-sample grid over a 1200-sample lag range, ±3 fine search, then a
  parabolic peak fit for a **fractional** offset — directly usable because the reads are
  interpolated (and stay exact via `ps_read`). Honest low-frequency reach with the 768-sample
  correlation span: ~125 Hz.

### Coherence-adaptive crossfade + the ρ protocol (the AM fix)

The owner reported audible **amplitude modulation** at the grain rate on shifted material. Cause:
`wA + wB = 1` is flat only when the two taps carry **coherent** signal. When a splice fails to
align (noisy or non-periodic material → offset-0 fallback), the taps are uncorrelated and add in
**power**, so the amplitude-complementary fade dips −3 dB at every fade midpoint.

The fix makes the fade law follow the measured coherence. `ps_service` already computes the NCC
of the splice it chose, so it publishes it: `ρ ≈ 1` (coherent) keeps the amplitude-complementary
weights; `ρ ≈ 0` blends toward **power-complementary** (√) weights, which are flat for
uncorrelated grains:

```c
  c  = 0.5f * (rho[0] + rho[1]);
  gA = wA + (1 - c) * (sqrtf(wA) - wA);      /* c=1: wA (flat sum)      */
  gB = wB + (1 - c) * (sqrtf(wB) - wB);      /* c=0: sqrt (flat power)  */
```

The **ρ protocol** is the handoff: the service writes `pend_rho`/`pend_off`, a compiler barrier,
then `pend_tap`; the ISR reads `pend_tap` first and consumes all three at the wrap (a wrap with
no pending result gets offset 0, ρ = 0). The coherence **prior is 1** — before the first splice
the two taps read overlapping content and *are* coherent; a 0 prior would put a +3 dB power-law
bump on tones until the first service pass.

Measured (`test_am.c` gates these): sustained-tone envelope ripple **0.33 dB at −1.07 st,
0.03 dB at −4.75 st**; noise (worst case for alignment) **~1–2 dB** — down from the −3 dB
grain-rate pumping that motivated the fix.

## Control mapping (shipped: stock pitch-mode semantics)

The panel mapping reproduces the stock firmware's pitch mode (decompile-verified, owner-tested);
`src/pitch_voice.c` holds the CV map and glide, `main.c` the panel law.

- **Knob = pitch-down depth**, from **raw pot travel** — not the panel-legend curve. The legend
  (0.4×–1.6×) is a time-multiplier scale, meaningless for pitch, and its piecewise-anchor slope
  kinks made the response audibly uneven across the rotation (owner report); the raw pot is
  physically smooth. The bottom ~2% of travel **snaps to exact unity**: measured on the owner's
  recording, "knob at CCW" otherwise parked the shifter at −44 cents, which beats against the
  dry signal as audible AM. Snapped, the ratio converges into the `PS_UNITY_EPS` clean-bypass
  window.
- **Depth span = cycle switch:** FULL = **−1.07 st** max, SHORT = **−4.75 st** max (the stock
  values).
- **Pitch CV → ratio.** Buchla is **1.2 V/oct**, so `ratio = exp2f(volts / 1.2f)` (not 1 V/oct).
  The CV is bipolar and passes through the **same −/+ attenuverter** as the time CV (center
  detent = ignored, CCW inverts), multiplied onto the knob's depth ratio. The result is
  **hard-bounded to ±2 octaves at the voice entry** (`pv_set_ratio`) — an unbounded target
  poisons the wet-mix `|ratio−1|` math and makes the glide traverse garbage; a railed CV was
  seen live driving the target to ratio 16.6.
- **Ratio glide: ~15 ms portamento** (one-pole on the ratio, `PITCH_RATIO_SLEW`). Tuned by ear
  on the unit: 5 ms was steppy under CV, 35 ms lagged the hand; 15 ms tracks.
- **Window `W` ("xfadetime" / character).** Short `W` (~10–20 ms) = more frequent splices → a
  grainier, warbly shift with tighter transients; long `W` (~50–80 ms) = smoother, more
  transient smear (doppler-ish). This is Eventide's `xfadetime` knob. **Shipped default: 60 ms**
  (`PITCH_WINDOW_SAMPLES`) — see the baseline measurement below.
- **Detune/chorus for free.** A ratio a few cents off unity (CV near zero, tiny depth) *is*
  `moddetun` — the shimmer/detune flavor, alongside the LFO-on-delay flanging TIME mode already
  gives.

## How it composes with the 8-tap engine (shipped)

One **global pitch voice** reads the same SDRAM buffer the engine writes — no extra memory, and
it inherits the sample format and fidelity switch automatically. In pitch mode:

- The delay pins to the range minimum and the voice **replaces** the tap outputs — a crossfade
  `wet = min(1, 50·|ratio−1|)` per tap, so at unity the output is the clean dry feed and any
  real shift is full wet. (Replace, don't layer: the dry min-delay passthrough masked the voice
  — the owner could not hear the shift at all in the layered version. Stock semantics: the
  pitch-mode output *is* the shifted signal.)
- The voice is always processed while in pitch mode — `pv_process` is the only thing that slews
  the ratio, and gating the call on the ratio deadlocked the voice at unity forever
  (adversarial-verify blocker).
- **Per-tap decorrelation:** all 8 channels get the voice, each through its own micro-delay
  (0–9 ms, a small CCM ring), so the outputs are never bit-identical — a phase-inverted slider
  pair on the output mixer **combs instead of cancelling** (owner's phase test), and
  cross-channel AM coherence drops.
- **Feedback cascades the shift.** There is no internal feedback path (by design — feedback is
  external patching); patch a tap output back to the input in pitch mode and each pass is
  shifted again — H949-style pitch spirals.

## Measured behavior

Baseline first (host reference, `cc -O2`, 440 Hz sine, Hermite, Goertzel spectral peak): the
**rate math is exact** — a single ramping tap (no crossfade) tracks `ρ·f` within the ~1%
measurement floor. The **plain two-tap crossfade** (no alignment) adds frequency-dependent
coloration that depends on `W`:

| ratio | expected | W = 30 ms | W = 60 ms |
|-------|----------|-----------|-----------|
| 0.5   | 220 Hz   | 207 Hz (−6.1%) | 223 Hz (+1.5%) |
| 1.5   | 660 Hz   | 673 Hz (+2.0%) | 657 Hz (−0.5%) |
| 2.0   | 880 Hz   | 907 Hz (+3.0%) | 873 Hz (−0.8%) |

This is the classic delay-line-shifter artifact: the two half-window taps carry the same shifted
tone with a delay-dependent (hence frequency-dependent) phase offset, so the constant-amplitude
crossfade partially suppresses the carrier and pushes energy into sidebands at `ρ·f ± grain_rate`,
where `grain_rate = |1−ρ|·fs / W`. Short windows raise `grain_rate` and worsen it.

**Shipped result** (aligned splices + adaptive fade + exact reads): **purity 0.992–1.000 across
−1 st .. +1 oct** — H3000-class on tones, and it holds on polyphonic material; envelope ripple
0.33 dB (−1.07 st) / 0.03 dB (−4.75 st) on tones, ~1–2 dB on noise; purity 0.999 / 0.00%
frequency error at a 4.2M-deep write pointer.

## Alternatives measured (and where they landed)

- **Correlation-aligned splices** — **IMPLEMENTED** (`ps_service`, above). Originally deferred
  as out of scope; measured ~transparent once in, and it is the single biggest cleanup.
- **Bigger window** — the cheap lever on the plain crossfade (table above); still the character
  control. Shipped default 60 ms.
- **4× overlap** (four taps at quarter-window offsets, `Σw = 1`) — **MEASURED AND REJECTED
  (2026-07-18)**: with correlation-aligned splices in place, 2-tap+alignment beats 4-tap-plain
  decisively even on chords (3-note chord, poly-purity: 2tap 0.49 → 2tap+aligned **0.99** vs
  4tap 0.63 at −1 oct). The alignment locks to the composite waveform's repetition structure;
  overlap only averages misalignment. Not worth 2× the reads.
- A **phase vocoder** is the pristine (and formant-preserving) option and a different
  architecture entirely — not this engine.

## Gotchas / limitations (call these out in code review)

- **Unity is degenerate.** At `ratio == 1.0` the ramp is frozen and two static half-window-apart
  taps comb-filter the signal. The `PS_UNITY_EPS` bypass collapses to a single centered tap —
  don't remove it. The panel's 2% unity snap exists to *reach* this window: without it the knob's
  CCW stop left a residual −44 cent shift beating against the dry signal.
- **Range.** Both taps span `[base, base + W]`, and the de-glitch search reads to
  `base + W + ~1600`; keep `base ≥ 2` and `base + W + 1600 ≤ len − 3` (trivial at SDRAM sizes).
  Keep `base` a few ms so the pitch tap never laps `wpos`.
- **Not formant-preserving.** Like `multishift` (and unlike a phase-vocoder), this shifts
  formants with pitch — "chipmunk" at large up-shifts. That's the expected 288/Eventide
  character, not a bug.
- **Ramp magnitude.** `|1 − ratio|` is the delay change per sample; the ±2 oct clamp (applied at
  the voice entry *and* in `ps_set_ratio`) keeps the per-sample step well under a sample so
  interpolation stays valid.
- **Exactness is load-bearing.** Any new read on the pitch path must go through `ps_read` (or
  `dl_read_frac` directly) — a single `(float)wpos − delay` read reintroduces the 1/4-sample
  quantization at deep buffer positions.

## Host tests (in `test/`, run by `make test`)

1. **`test_pitch_shift.c`** — frequency tracking (`≈ ρ·f` across down/up ratios), click-free
   wraps, unity bypass, detune stability (the `wA + wB = 1` partition through wraps).
2. **`test_deglitch.c`** — splice alignment, measured as spectral purity on a sustained tone:
   aligned splices must track `ρ·f` at least as accurately as the plain shifter *and* raise the
   carrier-to-total ratio.
3. **`test_am.c`** — output-envelope flatness on coherent (tone) and incoherent (noise) material;
   gates the adaptive fade law's measured ripple.
4. **`test_precision.c`** — the deep-wpos regression: exact int+frac reads at multi-million-sample
   write positions vs. the (documented, expected-coarse) float path.
5. **`test_pitch_voice.c`** — 1.2 V/oct CV map, ratio glide, and integrated frequency tracking
   through the real `delay_line` + `pitch_shift` + `fast_math`.

## Provenance / IP

This is the textbook delay-line pitch shifter (DAFX; Dattorro). Eventide's H910/H949 heritage and
the H9000's `multishift`/`moddetun` are cited as **design confirmation and parameter naming**
(shift + crossfade time), consistent with this repo's clean-room-adjacent stance: no firmware code
— Eventide's or the stock 288r's — is copied. The implementation is written from the math, against
this project's own `delay_line` API.
