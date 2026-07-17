# 9. Firmware & contributing

## The community-firmware project

The 288r's original firmware was abandoned and its source was never released. This repository is an
open, community effort to:

1. **Reverse-engineer** the shipped `.hex` (done — see `re/notes/`),
2. Ship an **experimental patch** that fixes the headline bug on the *stock* firmware (done —
   fractional interpolation for smooth TIME modulation), and
3. Build a clean, readable, **rebuildable community firmware** that keeps the full feature set and
   fixes what the original couldn't (in progress — `firmware/`).

**Design philosophy: clone first.** Faithfully reproduce the 288r's panel and behavior on a better
engine, *then* add new features. The rewrite runs the audio in single-precision float on the F429's
hardware FPU, at a fixed sample rate (no clock retuning), with fractional/interpolated tap reads.

House style mirrors the author's **MARF 248r** (`github.com/auxren/marf`): StdPeriph (not HAL), a
`Makefile`, host unit tests, GitHub Actions CI, numbered `docs/`, and MARF-style checksummed
persistence.

## Firmware status

| Area | Status |
|---|---|
| Reverse engineering | **Done.** Target STM32F429, delay engine fully traced. |
| Interpolation patch (stock fw) | **Done, statically verified.** Smooth TIME modulation. Experimental image in `Compiled FW/`. |
| Community DSP engine | **Written & host-tested.** `firmware/src/` + `firmware/test/` — `make test` green, `make engine` cross-compiles for the F429. |
| Hardware init (StdPeriph) & flashable release | **In progress / bench-gated.** Needs pinout + calibration constants from the bench. |

The definitive architecture is in `firmware/DESIGN.md`; the bench findings are in
`re/notes/bench-session-1.md` / `bench-session-2.md`.

## Building & testing

```bash
cd firmware && make test     # host unit tests (all pass)
cd firmware && make engine   # cross-compile the DSP engine for STM32F429
```

Offline audio proofs (hear the fixes before hardware):

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
