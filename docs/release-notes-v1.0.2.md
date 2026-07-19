# 288r community firmware — v1.0.2

Released 2026-07-19. Assets (`.hex`/`.bin` + the one-click `288r-Flasher.zip`) are built and
attached by CI on this tag. **Updating from any earlier version preserves your saved presets**
— see "Persistence guarantee" below.

## Headline: the pitch mode grew up

Every change below is measured on hardware (SWD cycle counters + direct audio capture), with
host regression suites gating each one (29 suites total).

- **Transposed multitap** — the sliders are a *pitched echo pattern* now, stock semantics:
  each tap output carries the shifted voice at that tap's own delay time, so your TIME-mode
  echo rhythm survives into pitch mode, transposed. Cycle/octave rescale it identically.
  Feedback patching cascades per-echo (uneven tap spacings = tumbling H949-style spirals).
- **Anti-aliased up-shifts** — a ratio-tracked polyphase read replaces plain interpolation
  above unity: 70 dB alias suppression at +1 octave (measured against the old path), passband
  flat to 0.01 dB. Bright material stays clean at +2 octaves.
- **Clean bass down to ~30 Hz** — a background period estimator sizes the splice search to the
  material: deep-bass purity went 0.13 → 1.00 where the old engine's correlator couldn't see a
  full waveform period. Mid frequencies untouched.
- **No more grain "breathing"** — the crossfade now adapts to measured splice coherence:
  envelope ripple 0.03–0.33 dB on tones (was up to 1.9 dB), 0.5–1.2 dB on hardware captures.
- **Feel fixes**: raw-pot pitch depth (smooth through the whole rotation), ~15 ms ratio
  portamento (tracks the hand, no zipper), exact-unity snap at the knob's bottom, ratio hard-
  bounded to ±2 octaves (a railed CV can't drive it into garbage), sample-exact read positions
  (no more ¼-sample phase jitter deep in the buffer).
- **Performance**: pitch mode ran over the interrupt budget in v1.0.1 (audible glitching that
  feedback patches recirculated); it now peaks at 59–83% with cycle-counter telemetry built in.

## New: envelope → delay time ("all sounds" mode)

The 288's signature self-modulation, resurrected: with the red switch at **all sounds**, the
**sens knob sets envelope-modulation depth** — playing dynamics push the delay time (doppler
bends on attacks, drift back on decays). Knob CCW = off. In looper modes the sens knob remains
the capture threshold, unchanged. (Background: the signal-in jack that provided this on the
stock is electrically dead on the reference unit — see Known issues.)

## Persistence guarantee (hard requirement)

Presets — and future calibration records — now live in the **top flash sector**, structurally
out of reach of firmware updates: the build system refuses to produce an image large enough to
collide with the store, and all supported flash tools erase only image sectors. A one-time
migration carries saves from the old location (verified byte-exact on the reference unit).

## Known issues / notes

- Reference-unit hardware faults (not firmware): **slider 5** and the **signal-in jack** are
  electrically dead — two analog paths, possibly one shared cause; repair notes in the repo.
  The envelope feature above covers signal-in's role meanwhile.
- **Slider 0's routing** (master sum vs dry feed) is still being verified — the manual hedges
  accordingly.
- SWD debug/bench scaffolding is intentionally retained (invisible in normal use).

## Since v1.0.1 — for the record

16 commits: polyphase AA (redesigned after an adversarial review caught an ISR-budget blocker
in the first version), ISR overrun fix, transposed multitap ring, period-adaptive bass, wet-
sliver fix, envelope→time, persistence hardening + migration, 4-channel control-ADC probe,
mux-sweep bench tool, slider-map corrections, CI portability fix.
