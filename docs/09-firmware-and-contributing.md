# 9. Firmware & contributing

## The community-firmware project

The 288r's original firmware was abandoned and its source was never released. This repository is an
open, community effort to:

1. **Reverse-engineer** the shipped `.hex` (done — see `re/notes/`),
2. Ship an **experimental patch** that fixes the headline bug on the *stock* firmware (done —
   fractional interpolation for smooth TIME modulation), and
3. Build a clean, readable, **rebuildable community firmware** that keeps the full feature set and
   fixes what the original couldn't (done — `firmware/`, released as **v1.0.1** and running on
   hardware).

**Design philosophy: clone first.** Faithfully reproduce the 288r's panel and behavior on a better
engine, *then* add new features. The rewrite runs the audio in single-precision float on the F429's
hardware FPU, at a fixed sample rate (no clock retuning), with fractional/interpolated tap reads.

House style mirrors the author's **MARF 248r** (`github.com/auxren/marf`): no HAL (the hardware
layer ended up bare-metal, direct-register against CMSIS — no libc), a `Makefile`, host unit tests,
GitHub Actions CI, numbered `docs/`, and MARF-style checksummed persistence.

## Firmware status

| Area | Status |
|---|---|
| Reverse engineering | **Done.** Target STM32F429, delay engine fully traced. |
| Interpolation patch (stock fw) | **Done, statically verified.** Smooth TIME modulation. Experimental image in `Compiled FW/`. |
| Community firmware | **Released — v1.0.1, runs on hardware.** Feature-complete against the stock (delay, looper, presets, pitch mode) plus the fixes; `firmware/src/` + `firmware/test/`, 26 host suites green. |
| Hardware layer & release | **Done.** Bare-metal BSP in `firmware/src/bsp/`; `make firmware` links the flashable image; tagging a release makes CI build the `.hex`/`.bin` and a one-click flasher zip. |

The definitive architecture is in `firmware/DESIGN.md`; the bench findings are in
`re/notes/bench-session-*.md`.

## Building & testing

```bash
cd firmware && make test     # host unit tests (26 suites, all pass)
cd firmware && make engine   # cross-compile the DSP engine for STM32F429
cd firmware && make firmware # link the flashable image -> build/fw/b288-community.hex
```

Some of the suites worth knowing about (each gates a specific owner-heard defect or measured
behavior):

- `test_am` — pitch-voice output-envelope flatness: the coherence-adaptive crossfade must stay flat
  on both coherent and uncorrelated material (measured ripple 0.33 dB at −1.07 st, 0.03 dB at
  −4.75 st on tones).
- the deep-wpos regression (`test_precision`, plus a 4M-deep case in `test_deglitch`) — fractional
  reads must stay exact when the write pointer is millions of samples into SDRAM, where the float
  path quantized to ¼ sample (purity 0.999, 0.00 % frequency error at a 4.2M-deep write pointer).
- `test_knobcurve` — the multiplier knob must reproduce the owner-measured panel-legend anchors
  (0.4–1.6) exactly, monotonically, over the full ADC range.
- `test_softknee` — the output limiter must be transparent below 0.75 FS and self-limit a
  greater-than-unity feedback loop with no flat-topped samples.

Offline audio proofs (hear the fixes without hardware):

```bash
cd firmware/tools
mkdir -p out && cc -std=c11 -O2 -I../src render.c ../src/delay_line.c -o /tmp/render -lm && (cd out && /tmp/render)
```

## Contributing

See **[CONTRIBUTING.md](../CONTRIBUTING.md)** for the full workflow. In short:

- Fork, branch, and open a **pull request**; keep host tests green (`make test`) and don't break
  `make engine`.
- **Clone-first**: match stock behavior before adding features; parameterize any constant you can't
  yet measure and mark it `calibrate on hardware`.
- Don't modify the golden image `Compiled FW/B288-REV1.0.hex`.
- v1.0.1 **intentionally retains SWD-only debug scaffolding** (`g_dbg_panel` telemetry, `g_dac_solo`,
  `sdram_memtest`) — invisible in normal use, documented in `docs/bench-runbook.md`, and kept until
  the reference unit's slider-5 repair is verified. Don't strip it in a PR.
- This repo is set up to be worked on with **Claude Code** — `CLAUDE.md` is the machine-readable
  handoff that orients an AI session on the project's status and conventions.

## Credits & license

- Binary reverse-engineering / control mapping: **@Mixcatonic** (ModWiggler forum) — see the repo
  README credits; the analysis in `re/binja/` is preserved as theirs.
- Firmware, DSP, and documentation: **Oren Levy (auxren)**.
- Licensed under **PolyForm Noncommercial 1.0.0** (see `LICENSE`).

## Disclaimer

The community firmware and experimental patches are provided as-is, for a module whose original
firmware source was never released. Flashing is at your own risk; the stock image is always
restorable over SWD ([Troubleshooting](08-troubleshooting.md)). This project is not affiliated with
or endorsed by the original manufacturer.
