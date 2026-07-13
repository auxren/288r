# Buchla 288r — Firmware Reverse-Engineering & Community Firmware

[![CI](https://github.com/auxren/288r/actions/workflows/ci.yml/badge.svg)](https://github.com/auxren/288r/actions/workflows/ci.yml)
[![License: PolyForm Noncommercial](https://img.shields.io/badge/License-PolyForm%20Noncommercial%201.0.0-blue.svg)](LICENSE)

Open reverse-engineering of the released `.hex` firmware for the **Buchla-format 288r
"Time Domain Processor"** (Roman Filippov / Black Corporation), with the goal of producing
**community firmware updates** that fix bugs and add features the original firmware never got —
starting with **smooth, interpolated delay-time modulation to enable chorus and flanger effects.**

The module shipped, its promised source was never released, and support was dropped. This repo
is a clean-room-adjacent effort to understand the shipped binary well enough to safely patch and
extend it, and to serve as a learning resource for embedded audio / STM32 reverse engineering.

## Credits

- **[@Mixcatonic](https://www.modwiggler.com/) (ModWiggler Forum)** — for digging into the shipped
  `.hex`, disassembling/decompiling it, and mapping the front-panel controls to the firmware. That
  work (preserved in `re/binja/`) confirmed the STM32F429 target and gave the function/panel map the
  delay-engine analysis is built on. Thank you.
- The STM32F429 delay-engine data-flow analysis, the interpolation patch, and the community-firmware
  rewrite in this repo build directly on that foundation.

> **Status:** delay engine fully mapped; hardware identified (STM32F429 · Cirrus **CS42888** codec ·
> ISSI **8 MB** SDRAM · no EEPROM) and the panel switch→GPIO map traced; **Patch 1 (interpolated delay
> tap) complete + statically verified** for both audio paths (`re/patches/`); **clone-first community
> firmware engine written and host-tested** (`firmware/` — delay line + interpolation, taps, TIME
> control, transport, mixer, envelope, crossfade, integration; 6 test suites green, cross-compiles for
> the F429). Now **blocked on the bench/SWD session** for the StdPeriph init from the pinout, the codec
> control bus/TDM map, and calibration constants (see `firmware/README.md`).

---

## 1. What is the 288 / 288r?

- The original **Buchla 288 "Time Domain Processor"** was an 8-stage voltage-controlled looping
  digital delay for the Buchla 200 series. It never reached production (two prototypes existed);
  Mark Verbos later built an alternative ("288v").
- The **288r** is Roman Filippov's modern digital recreation: Buchla format, 24-bit/192 kHz
  (switchable 12-bit "vintage" mode), up to ~40 s loop buffer, presets set via rear-panel
  trimmers and DIP switches, shipped with a USB programmer for firmware updates.

### Community-reported issues this project aims to address
| # | Issue | Where it lives |
|---|-------|----------------|
| **0** | **Delay-time modulation is stepped/zippered — no clean chorus/flanger** (our first target) | read path `sub_1968`/`sub_1c98`, time path `sub_2030` |
| 1 | Looper and delay are mutually exclusive (can't run both as demoed) | transport state machine (`transport_mode` @0x200000d0) |
| 2 | Pulse outputs weak: ~5 ms / 5 V vs. 288v's 14.5 V | TIM1/TIM3 setup (`sub_3edc`) + output driver (partly HW) |
| 3 | Manual mode: cycle switch ignored, write/recirc stuck ~3 s | `main_init_and_run_loop` (`sub_2508`) mode logic |

---

## 2. The firmware at a glance

Derived entirely from `Compiled FW/B288-REV1.0.hex` (Intel HEX, 27,912 bytes of image):

- **MCU:** STM32F429 (Cortex-M4F, Thumb-2, hardware single-precision FPU). Flash @ `0x08000000`,
  192 KB contiguous SRAM (SP init `0x20030000`), entry `0x08004341`.
- **Audio:** SAI2 codec, full-duplex, ping-pong **DMA2 Stream1/Stream4**; 24-bit samples.
- **Delay/loop buffer:** external **SDRAM via FMC** (SDCR @ `0xA0000140`), circular, length chosen
  by cycle/cal switches (90,400 / ~22,600 / 225,488 / **903,232** samples).
- **Control:** ADC1/2/3 for CV, pots, trimmers; I²C bank for the output-mixer sliders; GPIO for
  mode/DIP switches; TIM1/TIM3 timing; USART1 serial.
- **Size:** ~130 functions total; the audio engine is a handful of them. Very tractable.

Full details: [`re/notes/architecture.md`](re/notes/architecture.md).

### The delay engine (signal flow, per sample)

```
 SAI2 in ─▶ WRITE  delay_tap_service_A/B (sub_1250/sub_15dc)
              • 24-bit sign-extend, 256-tap envelope follower
              • delay_ram_bank[write_ptr] = sample ; sample >>=12/16/20 (vintage bit-depth)
              • advance circular write_ptr ; write→recirc→loop state machine
          ─▶ TIME   time_multiplier_and_envfollow (sub_2030)
              • TIME MULT ADC → coarse delay via SAI-PLL octave retune (RCC 0x40023888/8c) + hysteresis
              • tap targets = roundf(preset_phase[i] × multiplier)   ← integer
          ─▶ READ   tap read + mixer (sub_1968/sub_1c98)   [rename mislabels these "auto_control"]
              • dist  = tap_pos + corr + tap_pos·scale   (FLOAT, has fractional part)
              • dist  = (int)dist                        ← *** zipper: fraction discarded ***
              • out   = delay_ram_bank[write_ptr − dist] ← single integer fetch, NO interpolation
              • out <<= 12/16/20 ; stage to SAI DMA out buffer
 SAI2 out ◀──┘
```

**Root cause of no chorus/flanger:** the read tap is quantized to whole samples. The fractional
delay distance is computed in `s15` and then truncated by `vcvt.s32.f32` at **`0x08001aa6`**, and
the sample is a single integer-indexed fetch at **`0x08001ae8`**. Sweeping the delay time therefore
steps one whole sample at a time instead of gliding. Full analysis + the fix:
[`re/notes/delay-engine.md`](re/notes/delay-engine.md).

---

## 3. Repository layout

```
Compiled FW/B288-REV1.0.hex     original shipped firmware (Intel HEX) — treat as read-only
288-v1-alpha.png.webp           panel/board photo used for panel↔code mapping
firmware/                       Phase 3: rebuildable, readable C reconstruction (started)
  README.md                     regenerate-boilerplate + reconstruct-DSP strategy, binary↔source map
  src/delay_engine.c            reconstructed write + interpolated read, compiles for Cortex-M4F
re/
  patches/                      binary code-cave patches (Patch 1 = interpolated tap) + splicer output
  B288-REV1.0.bin               raw image @0x08000000 (objcopy from the hex)
  notes/
    architecture.md             MCU, memory map, peripheral & function map
    delay-engine.md             full delay data-flow, root causes, exact patch anchors
    fw-map.txt                  analyzer output
  scripts/
    analyze.py                  capstone-based literal/function/peripheral tagger
  disasm/full.thumb.asm         full arm-none-eabi-objdump Thumb-2 listing
  binja/                        Binary Ninja artifacts — by @Mixcatonic (ModWiggler), see Credits
    rename_288r.py              sub_XXXX → semantic names + panel mapping (offset base 0)
    288r_decompiled_abridged.txt
    288r_full_linear.txt
  .venv/                        Python venv (capstone) — not committed
```

> **Address convention:** Binary Ninja `sub_X` (loaded at base 0) == our flash address
> `0x08000000 + X`. `rename_288r.py` adds the base for you (`apply(bv, base=0x08000000)`).

---

## 4. Reproduce the analysis

Requirements: macOS/Linux, Python 3, and the GNU ARM bare-metal toolchain
(`brew install --cask gcc-arm-embedded` or `arm-none-eabi-gcc` from your distro).

```bash
# 1. HEX → raw binary at flash base
arm-none-eabi-objcopy -I ihex -O binary "Compiled FW/B288-REV1.0.hex" re/B288-REV1.0.bin

# 2. Python tooling (capstone). Keystone's wheel won't load on Apple Silicon —
#    we assemble patches with arm-none-eabi-as instead, so it's optional.
python3 -m venv re/.venv && re/.venv/bin/pip install capstone pyelftools

# 3. Full disassembly + the function/peripheral map
arm-none-eabi-objdump -D -b binary -m arm -M force-thumb \
    --adjust-vma=0x08000000 re/B288-REV1.0.bin > re/disasm/full.thumb.asm
re/.venv/bin/python re/scripts/analyze.py re/B288-REV1.0.bin

# 4. (optional) Binary Ninja: load re/B288-REV1.0.bin as raw @0x08000000,
#    then in the console:  import rename_288r; rename_288r.apply(bv)
```

---

## 5. Patch & build workflow (binary patching, pre-source)

Until a rebuildable source tree exists, fixes are **flash code-cave detours**: the image ends at
`0x08006D07`, so on the ≥512 KB STM32F429 everything from `0x08007000` up is free for new code.

1. Write the new routine in ARM Thumb-2 asm; assemble and extract bytes:
   ```bash
   arm-none-eabi-as -mthumb -mcpu=cortex-m4 patch.s -o patch.o
   arm-none-eabi-objcopy -O binary patch.o patch.bin
   ```
2. Place it in the cave and replace the anchor instruction(s) with a `b.w`/`bl` to it
   (a small Python splicer over `re/B288-REV1.0.bin` using capstone to verify offsets).
3. Repack and flash:
   ```bash
   arm-none-eabi-objcopy -I binary -O ihex --change-addresses 0x08000000 patched.bin patched.hex
   # via SWD (recommended for dev): st-flash / openocd / pyocd, OR the module's USB programmer
   openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
       -c "program patched.hex verify reset exit"
   ```
4. **Always keep the stock hex.** Every STM32F4 has a ROM bootloader (BOOT0) fallback, so a bad
   flash is recoverable — but verify before trusting.

Golden rule: **read before write.** Only change the read-side interpolation; leave the write path,
wrap arithmetic, bit-depth logic, and transport state machine untouched unless a fix requires it.

---

## 6. Roadmap

### Phase 0 — Reconnaissance ✅ (done)
Identify MCU, map peripherals/memory, locate and fully trace the delay engine, find exact anchors.

### Phase 1 — Smooth delay modulation (chorus/flanger) 🚧 (patch drafted; needs SWD to validate)
- Detour A @ `0x08001aa6`: `floor(dist)` + keep `frac`.  ✅ drafted (`re/patches/patch1_interp.s`)
- Detour B @ `0x08001ae8`: 2-point **linear interpolation** `bank[i0]·(1−frac)+bank[i1]·frac`
  (extend to all-pass/cubic later).  ✅ drafted + statically verified for `sub_1968`.
  ⬜ still to do: apply the same caves to `sub_1c98` (path B) and the `mode==6` bank_B fetch.
- Add a one-pole slew on the effective read distance; optionally a "modulation mode" that holds
  the sample clock fixed and varies only the fractional offset (true BBD-style chorus/flanger).
- **Validate on hardware:** breakpoint the read site, confirm the integer stair-step, then confirm
  continuous read pointer + glitch-free audio after patch. A/B a slow LFO into TIME CV.

### Phase 2 — The other reported bugs
- Pulse outputs (width/level): inspect TIM setup `sub_3edc`; determine HW vs FW limit with a scope.
- Looper + delay coexistence, and manual-mode cycle switch / fixed 3 s: transport state machine.

### Phase 3 — Rebuildable community firmware 🚧 (engine done + tested; HAL blocked on hardware)
- ✅ Clone-first DSP engine written and host-tested in `firmware/src/` (delay line, taps, TIME
  control, transport, mixer, integration); `make test` green, `make engine` cross-compiles for F429.
- ⬜ StdPeriph init/startup layer (reuse the MARF `Libraries/`) + linker bring-up, then calibrate and
  validate on hardware. Blocked on the bench/SWD session (pinout, codec control bus, clock, calibration).

### Phase 4 — New features
- Additional interpolation modes, tap feedback/modulation options, alternate loop behaviors,
  a documented parameter/CV map, and a firmware-update guide for end users.

---

## 7. How to contribute / learn from this

See **[CONTRIBUTING.md](CONTRIBUTING.md)** for the full guide (fork→PR workflow, build/test, and
safety). PRs to `main` run CI (`make test` + `make engine`) and need those checks green.

- **AI-assisted:** this repo is Claude-Code-ready — the root **[`CLAUDE.md`](CLAUDE.md)** auto-loads
  as project context. Clone your fork, run `claude` in it, and ask it to pick up a task (see the
  "Collaborating with Claude Code" section in CONTRIBUTING.md).
- **Good first tasks:** verify a function label in `re/binja/rename_288r.py` against the
  disassembly; scope the pulse-output circuit; write the capstone splicer for code-cave patches;
  document a peripheral init routine.
- **If you have a 288r:** board photos, silkscreen part numbers (confirm exact F429 variant + SDRAM
  chip), and SWD RAM dumps are the highest-value contributions. See Phase 1 verification steps.
- **Reverse-engineering primer references:** ARM Cortex-M4 Thumb-2 (ARMv7-M ARM), STM32F429
  reference manual (RM0090) for FMC/SAI/RCC register semantics, and the fractional-delay-line /
  interpolated-delay literature for the DSP side (linear vs. all-pass vs. Lagrange interpolation).

---

## 8. Legal / ethical note

This is interoperability and repair-oriented reverse engineering of firmware for hardware owners,
after the vendor stopped supporting the product and did not release the promised source. This repo
contains **analysis, notes, and tooling** plus the vendor's already-publicly-distributed `.hex`.
It does not claim any Buchla or Black Corporation trademark. Do not redistribute any recovered
vendor source verbatim; contribute original, independently-written reconstructions and patches.
Flashing modified firmware is at your own risk and may void warranties — keep the stock image.
