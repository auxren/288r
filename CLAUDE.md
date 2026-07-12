# CLAUDE.md — project memory & handoff

Read this first. It orients a Claude Code session on this repo and says where we are and what's
next. Deeper detail lives in `re/notes/` and `firmware/DESIGN.md`; this file is the index + status.

## What this project is
Reverse-engineering the shipped `.hex` firmware for the **Buchla-format 288r "Time Domain
Processor"** (Roman Filippov / Black Corporation clone of the Buchla 288), then building a
**community firmware** that fixes what the abandoned original never did. Vendor source was promised
but never released → we work from the binary. The owner has the unit **and an SWD/JTAG debugger**
(bench session pending, ~days out as of this writing).

**Primary goal:** make delay-time modulation **smooth** so the module can do chorus/flanger. The
stock firmware steps the delay in whole samples and changes delay time by retuning the audio PLL.

**Scope decision (locked): CLONE FIRST.** Faithfully reproduce the 288r's behavior/panel on a
better engine; add new features/controls/modulation only *after* the clone is nailed.

## Current status
- **RE: done.** Target = **STM32F429** (Cortex-M4F). Delay engine fully traced. See
  `re/notes/architecture.md` and `re/notes/delay-engine.md`.
- **Binary Patch 1: complete + statically verified** (not yet flashed) — `re/patches/`. Adds a
  fractional interpolated tap to the *stock* firmware via flash code-cave detours. Covers **both
  audio paths** (`sub_1968` + `sub_1c98`) and both `mode==6` recirc fetches — 6 detours, 184-B cave.
- **Community firmware engine: written + host-tested** — `firmware/src/` (delay_line, taps,
  time_control, transport, mixer, engine, envelope) + `firmware/test/` (5 suites). `make test` green;
  `make engine` cross-compiles for the F429 (~1.9 KB). Interpolation fidelity measured
  (`test_interp_quality.c`): Hermite ~2.4× better than linear at ½ Nyquist.
- **BLOCKED on the bench/SWD session** for a flashable image and exact constants (see below).
  The no-hardware DSP/patch work is essentially exhausted.

## Key technical facts
- MCU **STM32F429Z** (LQFP144), 192 KB SRAM (SP `0x20030000`) + 64 KB CCM. **Confirm flash suffix
  `ZE`(512 KB) vs `ZI`(2 MB)** — image reads like `ZET6`; if 512 KB, set `STM32F429.ld` FLASH=512K.
- **Codec = Cirrus Logic CS42888** (48-TQFP, the chip by the STLINK header — the earlier "second ST
  QFP" was a misread Cirrus logo; there is NO second MCU). **4 ADC-in / 8 DAC-out, 24-bit, TDM/I²S**,
  control over I²C or SPI2. → the **8 taps each get their own DAC output**; the F429 drives it via
  **SAI2 multichannel TDM** (hence the firmware's A/B paths). audio_io/engine output should be
  **8-channel TDM**, not one mixed output.
- **SDRAM = ISSI IS42S16400 (8 MB, 4M×16, 16-bit)** @ `0xC0000000` via FMC → use an **int16** buffer
  (float32 won't fit 40 s). Audio **24-bit / 96 kHz** (vendor "196KHz" = typo). Stock image 27,912 B.
- Panel = **74HC595/74HC4051 hardware scan** (DIP-binary tap times 10 ms steps, phase/mute DIPs, 36
  trimmers muxed to ADC) → presets live-read, **likely no NVM**. Full board brief: `re/notes/hardware.md`.
- SWD open (RDP-0 expected); ST-Link/V2 ships with the kit. **External SPI EEPROM: probably NOT
  present.** BOM lists a `25AA512` ambiguously, but a static scan of the stock `.hex` found no
  SPI-EEPROM usage (only SPI2 = codec; no 25xx opcodes/driver) → leans no-NVM (matches the live-read
  design). Still a board-check item (SOIC-8), not proven. Board photo confirmed F429/CS42888/SDRAM.

## House style — mirror the MARF 248r (github.com/auxren/marf)
Same author, same F4 family. **Align the 288r firmware to it:** **StdPeriph** (not CubeMX/HAL) —
reuse its `Libraries/` (CMSIS + StdPeriph); a **Makefile** (`make`, `make test` host tests, size,
hw-rev variants); **GitHub Actions** CI (host tests + arm build + tagged `.hex`/`.bin` release);
numbered **docs/** + PDF manual. **Persistence pattern** (backing-store-agnostic —
default to **F429 internal-flash emulation** since the stock shows no external EEPROM; use the
external 25512 only if the board turns out to have one): `eprom` layout + **versioned/checksummed
`storage.h` records** (`{magic,version,crc16,payload}`, refuse invalid) + **control-pinning on
recall** (live trimmer ignored until it sweeps through the stored value). Full plan: DESIGN.md
"Persistence & recall".
- **Address mapping:** Binary Ninja `sub_X` (in `re/binja/`, loaded at base 0) == our flash address
  `0x08000000 + X`. Verified.
- **Two root causes of no chorus/flanger** (both confirmed in code):
  1. Read tap is integer — fractional distance truncated by `vcvt.s32.f32` @ `0x08001aa6`, single
     fetch `bank[wp-dist]` @ `0x08001ae8` in `sub_1968` (and twin `sub_1c98`).
  2. Coarse delay retunes the SAI PLL (`RCC 0x40023888/0x4002388c`) in octave steps w/ hysteresis.
- Control math in the stock fw uses **software double-precision** (`__aeabi_dadd/dmul/ddiv`) on a
  single-precision-HW FPU — the rewrite uses hardware single-precision float (efficiency win).

## Repo map
```
Compiled FW/B288-REV1.0.hex   stock firmware (read-only, golden restore image)
re/notes/                     architecture.md, delay-engine.md (root causes + anchors), hardware.md (board)
re/binja/                     Binary Ninja disasm/decompile/rename map — by @Mixcatonic (ModWiggler)
re/scripts/                   analyze.py (capstone map), apply_patch1.py (splice+verify Patch 1)
re/patches/                   patch1_interp.s, patch1.ld, README (code-cave interpolation patch)
firmware/                     community firmware: DESIGN.md, src/, test/, Makefile, STM32F429.ld
```
Python tooling: `re/.venv` (capstone). Keystone won't load on arm64 → assemble patches with
`arm-none-eabi-as`. `.venv/` and build outputs are gitignored.

## How to build / test / verify
```bash
cd firmware && make test     # host unit tests (all pass)
cd firmware && make engine   # cross-compile engine for STM32F429
re/.venv/bin/python re/scripts/apply_patch1.py   # (re)generate + verify Patch 1 -> re/patches/patched.hex
```

## What's next
**Blocked until the bench/SWD session** (needs the board). Now-known from the board brief: MCU
F429Z, 24/96, 74HC595/4051 panel scan. Still needed: **pinout** → StdPeriph init, **SDRAM density**
(caps max delay + picks int16 vs float32 buffer), **codec part**, **HSE freq**/clock tree, the
**second MCU** role + its own dump, **25AA512 EEPROM** presence, the **mainboard (PCB3) BOM**, and
calibration constants (TIME taper/CV range, tap-time 10 ms decode, slider gain law, AUTO CONTROL,
pulse thresholds). Markers: `TODO(bench)`/`TODO(cube)` in `main.c`+`STM32F429.ld`; full checklist in
`re/notes/hardware.md` and `firmware/README.md` "Blocked on hardware".

**Doable now without hardware (mostly done):** ✅ Patch 1 both paths + mode6, ✅ one-pole envelope
followers, ✅ interp-quality measurement. Remaining optional/speculative: an all-pass fractional
interpolation option (good for flanger, but its modulation transients can't be A/B'd without audio
hardware — defer to bench), and more host tests. Further substantive progress needs the board.

**When the bench session happens:** flash `re/patches/patched.hex`, breakpoint `0x08001aa6`, confirm
the read pointer stair-steps on the stock fw and is continuous after the patch; then start
the StdPeriph init layer (reusing MARF's `Libraries/`) and calibrate constants against the real panel.

## Conventions
- Clone-first; don't invent precise constants — parameterize and mark `calibrate on hardware`.
- **Buffer/fidelity (decided):** SDRAM stores **int16 (vintage) / int32 (hi-fi)** — NOT float32;
  fidelity is a live front-panel switch (3 levels 12/16/20-bit in stock) that also sets the SDRAM
  layout (int16 → two ~20 s banks; int32 → one ~20 s bank), fixed at boot. Full spec: DESIGN.md
  "Memory & fidelity — SDRAM buffer layout". Bank_B = recirc/loop path (stock).
- Keep `Compiled FW/B288-REV1.0.hex` untouched (golden). BOOT0 ROM bootloader is the recovery path.
- Attribution: `re/binja/` analysis is @Mixcatonic's (see README Credits) — preserve it.
- Personal machine notes for the original author live outside the repo (`~/.claude/.../memory/`);
  this file is the shared, in-repo handoff.
