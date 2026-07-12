# Contributing to the 288r community firmware

Thanks for helping! This is a reverse-engineering + community-firmware project for the Buchla-format
288r "Time Domain Processor." Start with [`CLAUDE.md`](CLAUDE.md) (project state), the README, and
[`firmware/DESIGN.md`](firmware/DESIGN.md).

## How to collaborate

- **External contributors:** **fork** the repo, push to a branch on your fork, and open a **pull
  request** against `main`. You don't need write access.
- **Regular collaborators:** ask to be added as a repo collaborator (see below).
- Discussion/design chat: use **Issues** (and Discussions if enabled). Hardware/bench findings are
  especially valuable — see the bench checklist in [`re/notes/hardware.md`](re/notes/hardware.md).

## Collaborating with Claude Code (AI-assisted)

This repo is set up for AI-assisted work. The root **[`CLAUDE.md`](CLAUDE.md)** is a living project
handoff that **Claude Code loads automatically** — so cloning the repo and opening Claude in it starts
any session with full context: current status, the engine architecture, the switch/RAM maps, what's
done, and what's blocked on the bench.

**Getting started:**
1. Install **Claude Code** — <https://docs.claude.com/en/docs/claude-code> (or use the web app at
   <https://claude.ai/code>).
2. **Fork** this repo, clone your fork, `cd` into it, and run `claude`.
3. It reads `CLAUDE.md`, `firmware/DESIGN.md`, and `re/notes/` on its own. Then hand it a task, e.g.:
   - *"Add an all-pass interpolation option to `delay_line.c` with a host test."*
   - *"Trace `sub_XXXX` in `re/binja/` and document it in `re/notes/`."*
   - *"Build the `calib` module per DESIGN.md (SW14/16 power-up entry, min/max capture)."*
   - *"Run `cd firmware && make test` and fix any failures on my branch."*
4. Keep `make test` green, follow the ground rules below, and open a **PR from your fork**.

**Please keep the context current:** when you make a substantive change, update
`CLAUDE.md` / `firmware/DESIGN.md` / `re/notes/` so the next session — yours or a collaborator's —
picks up cleanly. `CLAUDE.md` is written for Claude Code but also works as context for other coding
agents.

## Build & test (no hardware needed)

```bash
cd firmware
make test      # host unit tests — must pass before you open a PR
make engine    # cross-compile the DSP engine for STM32F429 (needs arm-none-eabi-gcc)
```

CI runs both on every PR. The full flashable image is not buildable yet (the StdPeriph init layer is
bench-gated); the DSP engine and tooling are fully host-testable today.

## Ground rules

- **Clone-first.** Match the stock 288r behavior; add features only after the clone is solid. Don't
  invent precise constants — parameterize and mark `calibrate on hardware`.
- **Tests.** Add/extend a `firmware/test/test_*.c` for engine changes; keep `make test` green.
- **Style.** Match the surrounding code; keep the engine hardware-independent and host-testable.
- **RE conventions.** Binary Ninja `sub_X` == flash `0x08000000 + X`. Switch/RAM maps live in
  `re/notes/`. Don't commit large binaries or the venv (see `.gitignore`).
- **Don't redistribute vendor source.** Contribute original, independently-written reconstructions.
  The stock `.hex` is the vendor's (see [`LICENSE`](LICENSE) scope note); this project's original
  work is MIT.

## Flashing & safety (read before loading anything)

**You cannot permanently brick the unit by flashing firmware** — the STM32F429 has a mask-ROM
bootloader and an SWD port, so you can always *connect under reset*, mass-erase, and reflash the
stock image. Keep [`Compiled FW/B288-REV1.0.hex`](Compiled%20FW) as your golden restore.

- **Never touch the option bytes / RDP.** Setting RDP Level 2 permanently disables debug — that is
  the *only* way to truly brick it, and it takes a deliberate action. Don't.
- **Experimental firmware can output loud/DC signal.** Monitor at **low levels** and protect your
  ears, speakers, and downstream gear when testing.
- **Restore:** reflash the stock `.hex` over SWD (OpenOCD / STM32CubeProgrammer / ST-Link).
- Any release of experimental firmware should be a GitHub **pre-release**, bundle the **stock restore
  `.hex`**, and carry these warnings. Don't publish firmware that hasn't been run on real hardware.

## Good first tasks

- Verify a function/switch label in `re/binja/rename_288r.py` / `re/notes/` against the disassembly.
- Add host tests for the engine (e.g. an all-pass interpolation option, edge cases).
- If you have a 288r: board photos, the **F429 flash suffix**, the **codec control bus (I²C vs SPI2)
  + TDM slot map**, **HSE frequency**, and SWD register dumps — the highest-value contributions.
