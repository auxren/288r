# 288r community firmware — v1.0.1

Released 2026-07-18. Attach the built `.hex`/`.bin` and the one-click flasher zip (CI produces
these on the `v1.0.1` tag), plus the **stock restore** image `Compiled FW/B288-REV1.0.hex`.

**Flash at your own risk.** This firmware is developed and tested on one unit (owner-in-the-loop
bench sessions, July 2026). The factory firmware is preserved in this repo as
`Compiled FW/B288-REV1.0.hex`; reflashing it restores stock behavior at any time. See
[`02-installation-and-flashing.md`](02-installation-and-flashing.md).

## What this is

A complete community rewrite of the firmware for the Buchla-format 288r Time Domain Processor.
The vendor's source was never released, so the shipped binary was reverse-engineered and the
engine rebuilt from scratch on the module's own hardware (STM32F429, 8 MB SDRAM delay memory,
8-channel TDM codec). The goal was to reproduce the stock panel behavior faithfully and fix what
the stock firmware never did — above all, smooth delay-time modulation. v1.0.1 is the first
feature-complete release against the stock firmware, bench-verified on hardware.

## Headline features

- **Smooth delay-time modulation.** The stock firmware stepped the delay in whole samples and
  changed delay time by retuning the audio PLL, so modulation zippered and chorus/flanger were
  impossible. The delay taps now read at fractional positions (Hermite interpolation) and the
  multiplier glides over 10 ms, so Time-CV modulation is clean — chorus and flanger work.
- **Savable presets.** Hold the write switch for 2 s to save to the selected A/B/C slot (the LED
  twinkles to confirm). Presets persist in internal flash across power cycles. On recall the
  multiplier control is pinned until the knob sweeps through the stored value, so nothing jumps.
- **Working pitch mode.** Stock semantics on a crossfaded shifter: the knob sets pitch-down depth
  (max −1.07 st with the cycle switch at FULL, −4.75 st at SHORT), the bottom 2% of knob travel
  snaps to exact unity for a clean bypass, and the pitch CV is bipolar 1.2 V/oct through the
  attenuverter (ratio bounded to ±2 octaves). All 8 tap outputs carry the shifted voice, each
  through its own 0–9 ms decorrelation delay; ratio changes glide over ~15 ms. Measured quality:
  purity 0.992–1.000 across −1 st to +1 oct; tone envelope ripple 0.33 dB at −1.07 st and
  0.03 dB at −4.75 st; ~1–2 dB on noise; usable on polyphonic material. Patching an output back
  to the input cascades the shift on every pass (H949-style spirals).
- **Sens-thresholded auto capture.** The sens. knob is an analog attenuator feeding a dedicated
  codec ADC input; the firmware envelope-follows it against a fixed reference (`SENS_REF`,
  0.02 FS). With the red switch in its center (auto) position, capture triggers when incoming
  audio crosses the threshold; raising sens makes quieter material trigger, and full CCW
  disables auto-triggering. The AUTO CONTROL LED lights only while audio is above the threshold
  — the LED and the trigger use the same comparison, so you can see exactly what will capture.
- **Clip indicator.** The INPUT MIXER LED is now a whole-chain clip indicator: it lights for
  about 1/4 s when the input ADC rails or any tap output would exceed full scale before the
  limiter. Raise mixer A until it lights, back off until it goes dark — that is clean gain
  staging. (The stock half-scale comparator behavior is still available as a build option,
  `LED_INPUT_CLIP_MODE 0` in `board.h`.)
- **Soft-knee output behavior for patched feedback.** The output limiter is transparent below
  0.75 FS and rounds through a C1-continuous knee toward full scale, so an external feedback
  patch above unity gain blooms and settles instead of hard-clipping — a 1.15× loop settles at
  0.877 FS with zero flat-topped samples (tape-style bloom). Feedback is external patching only,
  by design; there is no internal feedback path. The loop round trip through the codec and block
  buffering is about 1 ms — irrelevant for delay regeneration, but it sets a roughly 1 kHz comb
  floor on extremely tight flanger loops (physics, same as stock).

## What changed since the last internal builds

All of the following was flashed and owner-tested in the final bench session unless marked
[BENCH].

- **Multiplier zipper fixed.** The tap slew time constant was 67 µs — it snapped to control
  ticks and produced broadband zipper from the knob, CV, or any source. It is now a 10 ms glide;
  chorus/flanger CV modulation is clean.
- **Multiplier knob calibrated to the panel legend** (TIME mode) from a 7-point owner-measured
  curve: 0.4 at the CCW stop, 0.6 / 0.8 / 1.0 at noon / 1.2 / 1.4, 1.6 at the CW stop, each mark
  reading exactly true. The pot taper is non-linear versus the print; the old firmware ate the
  bottom fifth of the travel.
- **Attenuverter law live from boot in both modes:** mult = knob + CV × attenuverter, with a
  center-detent dead zone (CV ignored) and inversion CCW. It was previously dead in pitch mode
  until TIME had been visited once.
- **LED changes from stock (owner-directed):** the INPUT MIXER LED became the whole-chain clip
  indicator and the AUTO CONTROL LED became the sens-thresholded presence indicator, both
  described above.
- **sens. knob mechanism (new).** Proven by an owner knob-sweep to be an analog attenuator
  feeding a codec ADC input; auto/looper capture now keys off it.
- **"signal in" jack (TIME section) understood.** It is not a codec channel: it reaches the
  multiplier through the analog Time-CV net, summed with c.v. in and scaled by the attenuverter.
  Signal-in envelope modulation of delay time works acoustically through the CV path — keep the
  attenuverter up to hear it. [BENCH]: the exact summing point is unconfirmed.
- **Pitch mode heavily fixed.** The shifted voice now replaces the tap outputs (was layered
  under dry; at zero depth the output is the clean dry signal) and runs on all 8 taps. Depth
  comes from raw pot travel (smooth — the panel-legend curve kinked the response), with the
  unity snap killing residual −44 cent detune-beating. The CV path is hard-bounded at ±2
  octaves (a railed CV had driven the ratio to 16.6). Ratio glide settled at ~15 ms (5 ms was
  steppy, 35 ms lagged the hand). The crossfade is now coherence-adaptive — coherent grains fade
  amplitude-complementary, incoherent grains power-complementary — which fixed audible AM. All
  pitch-path reads are now exact int+frac: the float path had quantized to 1/4 sample deep in
  SDRAM (regression test: purity 0.999, frequency error 0.00% at a 4.2-million-sample-deep write
  pointer). Per-tap decorrelation micro-delays were added so phase-inverted slider pairs comb
  instead of cancelling.
- **Slot-to-slider map owner-verified** via an SWD solo walk: slider 0 is the always-dry input
  feed (analog); sliders 1–8 are the taps. This walk found the dead slider 5 (see Known issues).
- **Settings policy locked (owner decision).** Only the 4 rear DIP switches are read — sw1 ×10
  extend, sw2 bandwidth limit, sw3/sw4 resolution — as boot-time straps; power-cycle to apply.
  The front DIP matrix (tap times / phase / mute rows) is never read; everything it did on the
  stock is covered by the save-chord presets.
- **Soft-knee output limiter added** (described above).
- **26 host test suites green** (`make test`), including new suites for pitch envelope flatness
  (`test_am`), the deep-write-pointer regression, the knob curve against the panel-legend
  anchors (`test_knobcurve`), and the soft-knee (`test_softknee`).

## Known issues

- **Slider 5 is dead on the development unit — hardware, not firmware.** Codec output slot 4 is
  live on the TDM bus but reaches no slider in any phase-switch position, so the analog path on
  that board is broken (check that AOUT net: solder joint, coupling cap, buffer op-amp section).
  The firmware keeps the identity mapping (slider N = tap N) so the panel legend stays honest.
  If your unit behaves the same, check the analog path before suspecting firmware; the SWD solo
  tool (`g_dac_solo`) remains in the build for post-repair verification.
- **Two values are single-unit calibrations.** The auto-capture reference `SENS_REF` (0.02 FS)
  was calibrated by feel on one unit and may want adjustment on yours. The exact analog summing
  point of the "signal in" jack into the Time-CV net is unconfirmed [BENCH] — the behavior works
  acoustically, but the wiring has not been traced. Both are marked in the source.
- **Debug scaffolding is intentionally retained** in v1.0.1: `g_dbg_panel` telemetry,
  `g_dac_solo`, and `sdram_memtest`. All are SWD-only and invisible in normal use; they are
  documented in the bench runbook and will be stripped in a future release once the unit's
  slider-5 repair is verified.

## Install

The easiest path is the one-click flasher zip attached to the release: unzip, double-click the
launcher for your OS, and follow the prompts — see [`flasher/README.md`](../flasher/README.md).
For manual SWD flashing (ST-Link + OpenOCD), backing up your unit first, and restoring the stock
image, see [`02-installation-and-flashing.md`](02-installation-and-flashing.md). Keep a copy of
`Compiled FW/B288-REV1.0.hex` — reflashing it returns the module to factory behavior.

## Credits

Reverse engineering and the community firmware are the work of the community. The Binary Ninja
disassembly and decompile analysis in `re/binja/` is by @Mixcatonic (ModWiggler) — see the repo
credits. The factory firmware is preserved unmodified in this repo as the restore image.
