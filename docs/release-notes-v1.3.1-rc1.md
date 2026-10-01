# 288r community firmware — v1.3.1-rc1 (pre-release for field testing)

**Updating preserves your saved presets.** This rc is the first cut after v1.3.0. It
carries two fixes that came out of a literature pass (seventeen DAFx papers read against
the firmware's open problems), two more that were found live on the bench while verifying
those, and the first build of clocked mode. Every change below was wire-proven on the
reference unit on 2026-09-30 except clocked mode, which has not yet seen a clock.

## New features

- **Clocked mode (first build, field-untested).** Patch one clock into **both** the write
  and recirc pulse jacks at the same time. Three coincident pulses in a row enter the mode;
  the delay's window length becomes the clock period, and the multiplier knob becomes an
  integer ratio from ÷8 to ×8 with ×1 sitting on the printed "1". Octave, CYCLE and the
  extend DIP still multiply on top. Unplug either jack, or let the clock stop for 2 s, and
  the knob resumes control. TIME mode only. The arm jack stays free for loop capture. The
  clock is timed from pulse edges inside the audio interrupt, so period accuracy is one
  sample, not one panel tick. This is the MARF 248r's clock-sync idiom brought over;
  please report lock behaviour, dropout handling, and anything that moves when the knob
  crosses a zone boundary.

## Fixed

- **Overdub no longer writes flat-tops into the loop on transients.** The sound-on-sound
  write limiter has a 5 ms lookahead now: it sees each peak before writing it and settles
  the gain in time, so the hard clamp behind it never engages (counter stayed at zero
  through every hot session on the bench, including 3.5 s parked at the ×4 varispeed
  rail). Stacked layers keep the 0.02% THD figure. Before this, the first milliseconds of
  every loud onset went in over the knee and were clamped, permanently.
- **Pitch-mode bass splices no longer lock a period off on bright material.** The splice
  search's coarse pass now low-passes before it correlates. On a 35 Hz tone carrying a
  6 kHz partial the old search aligned a whole period wrong (bass purity 1.00 → 0.17); the
  new one aligns correctly (1.00). Pure bass and mid-range material are unchanged.
- **The "quantized knob" in pitch mode is gone for good.** The background period scan was
  stalling the control path for 115–150 ms, continuously, whenever pitch mode was active
  with a signal present. The scan now runs from fast internal RAM: control-tick gaps
  dropped from 340–460 blocks to 50–100 (mean 92), period estimates complete every half
  second instead of every two, and the scan no longer competes with the audio interrupt
  for the external memory bus.
- **Overdub release now completes when the looper re-arms.** Letting go of recirc while
  the input is still playing drops the transport straight back into WRITE (auto re-arm),
  and the overdub's release ramp lived only in the recirc branch, so its gain froze at 1.0
  for the whole WRITE period. Side effect of that freeze: every tap sat on the lower
  interpolation grade until the next loop. Fixed; the limiter and lookahead state are also
  reset cleanly at the end of each session, so the next overdub starts at unity gain.

## Known / open issues

- **Clocked mode has not been bench-tested with a real clock** (the follower and entry
  gesture are host-tested). Treat it as experimental in this rc.
- **A tap patched back into the mixer and driven into the rail costs CPU.** Eight tap
  outputs above the output limiter's knee on every sample plus a saturated input runs the
  audio interrupt at 76–88% of its deadline, and each loop capture in that state adds one
  overrun block (a tick). v1.3.0 was worse in the same patch (109–117%, no governor);
  this build's load governor keeps it to the one block per capture. Being worked on.
- Pitch mode still has 17–33 ms control gaps around each grain wrap (the search's one-shot
  read and alignment passes). Splitting those finer risks a documented stale-geometry
  hazard at high ratios, so they stay for now.
- If captures sound crunchy, check the **input clip LED** while recording. Hot modular
  sources rail the ADC on crest peaks well before they sound hot (manual ch. 6).
- #24 deep-sweep verdict, #25 pulse-out jacks, #23 micropitch spread, #5 deeper presets:
  unchanged from v1.3.0.

## For the nerds — what changed on the DSP side, how, and why

### 1. Lookahead write limiter (overdub)

*Paper:* P. Hämäläinen, "Smoothing of the Control Signal without Clipped Output in
Digital Peak Limiters," DAFx 2002.

*What was there.* The sound-on-sound write is `v = old·decay + input`, limited at a
0.75 FS knee by a peak-hold envelope (instant attack, ~50 ms release) driving a gain that
slews with a 5 ms time constant, and a hard ±1.0 clamp behind it as a backstop. The slow
gain was deliberate: a fast-attack follower had gain-rippled at twice the signal frequency
and written harmonic distortion into the loop. The clamp was deliberate too: content above
full scale is baked in forever. The combination is the paper's textbook failure case: a
smoothed gain with no lookahead *always* overshoots on a transient, so the clamp is the
limiter for the first few milliseconds of every onset, and the clamp is the distortion.
Field forensics had shown loop content peaking at 1.092 after an overdub session; the host
test reproduces it exactly (burst onset: written peak 1.000, 75 clamp engagements).

*What it is now.* The limiter runs ahead of the audio by N = 480 samples (5 ms at 96 kHz).
The conditioned layer input goes through an SRAM ring and is written 480 samples late.
Each output sample, the control looks at the loop cell that *this* input will land in,
`c = |buf[pos_ahead]·decay + input_now|`, where `pos_ahead` is the cell the head reaches
after ~N·rate advances inside the loop window (one extra SDRAM load per frame). That
control value goes through an exact running max over the window (eight sub-block maxima of
60, so each value lives in the max for at least N samples) and then into the same peak-hold
envelope as before. The gain slew coefficient rose from 0.002 to 0.0095 per sample so that
the gain closes 99% of the way to its target within N samples: (1−a)^481 ≈ 0.01, which is
the paper's α_max = 1.01 bound on written overshoot (0.75 × 1.01 = 0.7575). The clamp is
still there but is now a backstop with a counter (`od_clamp_hits`), and the host test and
the bench both hold it at zero.

*Why this shape.* A conventional lookahead limiter delays the signal and looks at the
undelayed signal; here the "signal" is the sum of a stored loop cell and a live input, and
the stored half is already known N samples ahead because it is sitting in the loop. So the
cheap version is to delay only the input and read the future cell. At varispeed rates
above 1 the head visits several cells per output sample and the control samples one of
them; the running max plus the peak-hold release cover the rest, and the bench at the ×4
rail agrees (written peak 0.64, no clamp). The ring drains for 5 ms after release so the
last few milliseconds of a layer still land.

*Cost.* ~1.9 KB SRAM, one SDRAM load and ~20 ALU cycles per output sample while
overdubbing. The ring is never cleared by a loop: a fill counter makes it read as zero
until it has refilled after a session, so no 480-store burst exists on the interrupt path
(the WCET contract in `tools/wcet.py` rejects undeclared loops there, and did, in CI). Measured on the unit: overdub at rate 1 = 82–84%, identical to the loop
playing without overdub; ×4 rail = 88%. The host ISR model had predicted 111% for that
case; it is pessimistic about this path.

### 2. Box-low-passed, SRAM-resident splice search (pitch mode)

*Paper:* A. Haghparast, H. Penttinen, V. Välimäki, "Real-Time Pitch-Shifting of Musical
Signals by a Time-Varying Factor Using Normalized Filtered Correlation Time-Scale
Modification," DAFx 2007.

*What was there.* The pitch voice is a two-grain crossfaded delay-line shifter. When a
grain is about to wrap, a background search finds the splice offset that best aligns the
incoming grain to the outgoing one, by a sign-kept normalized cross-correlation over a
window sized from a background period estimate (bass-adaptive, up to ~2 periods). To keep
the search cheap on bass windows it decimated its correlation grid by up to 8: take every
8th sample, skip the rest. That is decimation without a low-pass. A strong partial above
the decimated Nyquist (fs/16 = 6 kHz at 96 kHz) does not disappear; in the lag domain it
aliases into a slow cosine whose "period" has nothing to do with the fundamental, and whose
amplitude is comparable to the fundamental's own correlation peak. The coarse search then
picks the wrong maximum, and the fine pass (±3 lags) cannot recover a whole-period error.
The paper's recipe, verified on a 63 Hz piano where raw correlation clicked and filtered
correlation did not, is to correlate a normalized, low-passed copy with the cutoff just
above the highest fundamental of interest.

*What it is now.* The coarse pass is split into two phases. Phase one reads the search
region once, contiguously, and stores two decimated sequences in CCM: A over
`[dIn, dIn + ML + N)` and B over `[dOut, dOut + N)`, where each grid point is the **box
average of the `kstep` consecutive samples** it replaces. A box of width k is a low-pass
with its first null at fs/k, so at k = 8 the 6 kHz partial is ~46 dB down before anything
is correlated. Phase two scores every lag from SRAM: because the lag step is a multiple of
the sample step, lag `i` is just `Σ A[i·m + k]·B[k]`, the same NCC² score as before on the
same grid. The 7-point fine pass and parabolic refine are untouched and still read the
delay line directly, so sub-sample alignment is unaffected. The default mid-range search
has k = 2, whose box is spectrally almost flat, so the field-proven purity gates on
mid-range material (`test_deglitch`, `test_am`, `test_aa`) pass unchanged.

*Why it matters beyond the low-pass.* The old coarse pass issued two SDRAM reads per
multiply-accumulate across ~170k MACs, resumed over ~40 service calls, and every one of
those reads competed with the audio interrupt for the FMC. Now the whole coarse pass is one
~0.6 ms read and ~1 ms of SRAM arithmetic, and the search finishes before the grain
geometry it froze at the start goes stale. (The old raw-stride path improved from 0.17 to
0.58 purity on the test signal just from finishing faster; the box takes it to 1.00.)

*Also from the same paper:* the read-head proximity floor, d_min = (α_max − 1)·L_xfade +
N/2. The firmware's base offset of 256 samples minus the full negative FM swing (96) minus
the anti-aliasing kernel's half-length (8) leaves 152; a new `min_dist` telemetry field
tracks the smallest grain read distance ever issued, and the host suite sweeps ratio
0.25–4 with FM at full swing over a wrapped loop window to prove it never goes below.

### 3. Period scan from SRAM (pitch mode)

Found on the bench while verifying #2. Forcing the ratio to exact unity over SWD (no
search, no scan) gave a control-tick gap of 8 blocks; ratio 0.971 gave 340–460 blocks,
and it made no difference whether the splice search used the new box fill or the old
stride. So the 115–150 ms stalls were not the search: they were the **background period
scan**, which scored 40 lags per service call straight from SDRAM, 1024 window-mapped reads
per lag, each with two integer divisions in loop mode, while the audio interrupt owned 77%
of the processor. The July fix for the "quantized knob" chunked the *search* and never
touched this chunk size; the 101 ms peak gap that fix signed off on was this scan.

The scan now uses the same machinery as the search: the span (2048 + 3400 samples) is read
once in chunks of 350 grid points, box-8 averaged into CCM, and all 395 lags plus the
subharmonic disambiguation are scored from SRAM, about 100k MACs in total. Box-8 is the
scan's own lag grid, so no resolution is lost, and it fixes the scan's own lag-domain
alias too (the test signal read 1492 for a true 1371-sample period; now 1368). Measured:
tick gap 340–460 → 50–100 blocks, scans complete every ~0.5 s. A box of 4 was tried first
and rejected: its null sits at 12 kHz and it only attenuates the 6 kHz partial by 3.7 dB.

### 4. Overdub release ramp in both transport branches

Found on the bench: after letting go of recirc the log showed the overdub gain parked at
1.00 for four seconds, then dropping to 0.76 during a 3 ms recirc blip. The engage/release
ramp was inside the recirc branch of the per-sample engine, and the auto re-arm had moved
the transport to WRITE. Consequences: the gain never released in WRITE, the Hermite
interpolation gate (which waits for the gain to reach zero) kept every tap on the linear
grade, and the limiter envelope carried over into the next session. The ramp now runs
every sample in both branches; when it reaches zero in RECIRC the lookahead ring drains for
5 ms into the loop, in WRITE the session ends immediately; and the session end resets the
envelope, gain, ring, running max and resampling state. The reset is ordered *after* the
final drain sample's control update, because doing it before let the loop's own content
re-raise the envelope (hardware read 0.705 instead of 0 the first time).

### Verification

Host: 43 suites green (`make test`), `make cachecheck` bit-identical, `make wcet` (every
ISR loop bounded; static ceiling 2.9% under the recorded baseline), golden fingerprint
re-baselined only in the two overdub phases with phases 0–2 bit-exact. Bench (reference
unit, 2026-09-30, owner live): a 0.5 s SWD poll of transport, overdub, limiter, clamp
counter, ISR peak (latch reset per sample), control-tick gap, pitch ratio, period estimate,
and read-distance floor through every test above; two A/B reflashes under a fixed patch
to separate this build from v1.3.0 and from the first build of the evening.
