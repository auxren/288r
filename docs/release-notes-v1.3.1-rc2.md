# 288r community firmware — v1.3.1-rc2 (pre-release for field testing)

**Updating preserves your saved presets.** Supersedes rc1 (everything in rc1 is in here).
Two fixes from the same bench evening, both found while testing rc1 on the unit: clocked mode
could not see the owner's clock, and every loop capture cost one torn audio block.

## New features

- (None beyond rc1. Clocked mode is still: one clock into **both** the write and recirc
  pulse jacks, window = clock period, multiplier = integer ÷8…×8 with ×1 on the printed "1",
  TIME mode only. It now actually locks, see below.)

## Fixed

- **Clocked mode locks and stays locked on a real clock.** The rc1 build only saw a pulse
  when both jacks rose inside the same 0.33 ms audio block, and only by polling the jacks
  once per block. A typical modular trigger is tens of microseconds wide, so most pulses were
  simply missed; the follower accepted 2× and 4× intervals, the lock timed out every few
  seconds, and the window flipped between the knob setting and the clock period on every
  re-entry (the "weird shifts"). Rising edges on all three pulse jacks are now captured by
  hardware interrupt, and the two jacks are paired with a 5 ms tolerance. Bench: 20 s on a
  steady clock, every pulse counted, period jitter ±3 samples, no lock loss.
- **Loop captures no longer flush the delay-line cache.** Each capture used to cost one
  over-budget audio block (a tick, worst on dense patches): the seam-splice job switched the
  read cache off for its whole ~7-block window and the capture flushed all eight cache lanes
  twice. The splice now invalidates only the handful of samples it actually writes each frame,
  and a transport entry flushes nothing (it changes which samples the taps read, not what is
  in the buffer). Host accounting: first capture block 768 → 572 memory words, later splice
  blocks ~770 → ~360. **Bench gate still open**, see below.

## Known / open issues

- **Capture fix is host-proven, not yet bench-proven.** The overrun counter must stay flat
  across repeated captures on the unit; three monitor windows totalling fifteen minutes never
  coincided with a capture. The firmware now records a snapshot of the engine state at any
  block that misses its deadline (`g_dbg_panel.ovr_*`), so the next bench session settles it
  in one read. If you flash this rc and still hear a tick on each capture, say so.
- A tap patched back into the mixer and driven into the rail still runs the audio interrupt
  hot (76–88%); the governor keeps it to single blocks.
- Pitch mode keeps 17–33 ms control gaps around each grain wrap (by design, see rc1).
- #24 deep-sweep verdict, #25 pulse-out jacks, #23 micropitch spread, #5 deeper presets:
  unchanged.

## For the nerds

### 1. Clocked mode: from polled coincidence to hardware edge capture

*What the trace showed.* Over SWD, with a steady clock patched into both jacks: pairs of
"coincident" edges arrived in short bursts a few seconds apart; the follower's `since`
counter ran up to the 2 s timeout, the period reset to 0, clocked mode disengaged (base
41,440 → 24,000), re-paired on the next burst and re-engaged. Once the pairing window was
widened the picture did not change and the single-edge counters stayed at zero, which meant
the jacks were not producing rising edges at the clock rate at all. Sampling the GPIO
register at ~1.5 kHz for 10 s caught two rises on each jack, both one sample wide, against
~46 expected: the trigger is narrower than the 0.64 ms sampling interval, and narrower than
the ISR's 0.33 ms block poll, so the poll only catches it when the pulse happens to straddle
a poll instant.

*What changed.* `bsp_pulse_exti_init()` routes PG10/11/12 to EXTI lines 10–12, rising edge
only, with the handler at a lower priority than the audio DMA interrupt so it can never
preempt the audio path; the handler accumulates the edge bits and the block ISR drains them
with `bsp_pulse_take_rises()` and ORs them into its own polled edge detection, so gates and
long pulses keep working through the poll. Timestamps stay block-granular (0.33 ms), which is
what the follower wants.

*Pairing.* One clock on two input comparators does not produce two rises in the same
0.33 ms block. `cm_pair_block()` (pure, host-tested) remembers each jack's last rise, declares
a clock pulse when the other jack rises within `CM_PAIR_WINDOW_BLOCKS` (15 blocks, 5 ms),
stamps the pulse at the *earlier* edge so a constant skew does not jitter the period, and
expires an unpartnered edge as a single. The singles are what the "one cable unplugged" exit
counts; in rc1 the write and recirc flags fed to the mode detector were the same variable, so
that exit could never fire. It is written without loops so the ISR's WCET manifest did not
need a new entry.

### 2. The loop-capture one-shot

*Evidence.* The audio ISR's deadline-miss counter incremented by exactly one on every loop
capture, in every patch state, on every image back to v1.3.0. A host probe counting SDRAM
words per 16-frame block around a capture, split by mechanism, showed steady blocks at ~200
words (the delay-line cache serving nearly every tap read) and the seven blocks of the seam
splice at 768: 512 words of direct 4-word tap reads plus 256 words of splice
read-modify-write. The rule at the top of the frame said "while the splice job is writing,
trust no cached line", and dropped all eight lanes every frame to protect against a write
that touches at most one of them. Separately, the capture performed two full cache flushes
(guard samples in `splice_arm`, transport entry in `declick_arm`), and the eight-lane refill
tail that followed was hidden behind the splice bypass until the bypass was removed.

*Fix, in three steps, each measured.* (1) `dc_invalidate_range()` drops only lanes whose
96-word line overlaps a written range; `splice_service` calls it for exactly the samples it
just wrote, before that frame's tap reads. First attempt made things worse (912 words/block)
because the one lane the splice walks through refilled its line every frame as the write
advanced 8 samples inside it; the lane is now parked on direct reads (`life = 0`) and comes
back through the cache's normal re-probe after the write has moved on. (2) `splice_arm`
range-flushes the four guard samples instead of everything. (3) `declick_arm` flushes
nothing: a transport entry changes which buffer *indices* the taps read, not the buffer's
contents, and the cache keys its lines by index; the write-head clearance rule is enforced at
read time, so a tap landing near the new head is refused rather than served stale.

*Proof.* `make cachecheck` (the golden scenario rendered with the cache compiled in and out
must hash identically, and it includes captures, releases and re-splices) stays
bit-identical through all three steps. `test_capture_budget` pins the invariant "a splice
block costs steady traffic plus the splice's own RMW plus at most two rate-limited refills".
The WCET static ceiling fell 14% below the recorded baseline, since the bypass sat on the
ISR's longest path. The bench half, the counter staying flat through real captures, is the
open item above.

### Verification

Host: 44 suites green, `make cachecheck` bit-identical, `make wcet` OK. Bench (reference
unit, 2026-09-30/10-01): clock follower traced 20 s on a steady clock before and after; GPIO
pulse-width sampling over SWD; the capture fix flashed and running, gate pending.
