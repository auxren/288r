# Buchla 288r — Firmware Reverse-Engineering Notes

**Image:** `Compiled FW/B288-REV1.0.hex` → `re/B288-REV1.0.bin`
**MD5 (hex):** 0856238bc7c63220433a7f656bc3e26a
**Approach:** black-box RE (no source). Hardware available with SWD/JTAG for grounding.

## Product context
- **Original Buchla 288 "Time Domain Processor":** 8-stage voltage-controlled looping
  digital delay. Never mass-produced (2 prototypes). Mark Verbos later did the "288v".
- **288r:** Roman Filippov / Black Corporation digital clone. 24-bit/192 kHz (switchable
  12-bit "vintage"), up to ~40 s loop buffer, presets via rear trimmers + DIP switches,
  USB programmer included. Source was promised public but never released; support dropped.
- **Reported bugs (community):** (1) looper vs. delay mutually exclusive; (2) pulse outs
  weak — 5 ms / 5 V vs. 288v's 14.5 V; (3) manual mode: cycle switch ignored, write/recirc
  stuck ~3 s. **Primary goal for THIS effort:** smooth delay-time modulation → chorus/flanger.

## Target MCU — STM32F42x/F43x, Cortex-M4F (Thumb-2, hard-FPU)
Evidence from the binary:
- Vector table at 0x08000000; Initial SP = **0x20030000** (= 192 KB contiguous SRAM top,
  the F427/429/437/439 layout 0x20000000–0x2002FFFF).
- Entry/Reset = 0x08004341. Full 89-entry external IRQ table populated.
- Image span 0x08000000–0x08006D07 (**27,912 bytes**). Small, tractable: **37 functions**.
- FPU instructions present (VMUL/VADD/VCVT…) → Cortex-M4**F** with float DSP.

## Peripheral / memory map (confirmed from literal pools + reg accesses)
| Block | Base | Role |
|---|---|---|
| **FMC SDRAM ctrl** | 0xA0000140 | external SDRAM controller config |
| **SDRAM** | **0xC0000000** | **the delay/loop buffer** (multi-second audio) |
| **SAI2** | 0x40015800 | audio codec interface, 24/192, full-duplex |
| **DMA2 Str1 + Str4** | 0x40026428 / 0x40026470 | SAI2 RX/TX audio DMA (ping-pong) |
| ADC1/2/3 | 0x40012000/200/300 | CV inputs, pots, trimmers (delay-time control) |
| TIM1 / TIM3 / TIM2 | 0x40010000 / 0x40000400 / TIM2 IRQ | timing; **pulse-output** candidates |
| RCC | 0x40023800 | clocks |
| GPIOA–G | 0x40020000.. | switches, DIP config, pulse GPIO |
| USART1 | 0x40011000 | serial (debug / update?) |
| USB-OTG | 0x50000000 | firmware-update path (minor) |

## Function map (the ones that matter)
| Addr | Size | What it is |
|---|---|---|
| **0x8001180** | **8842 B** | **main() + inlined DSP loop.** 141 FPU, 1209 mem ops. Touches SAI2, ADC, TIM1/3, FMC, RCC, GPIO. **Delay engine + interpolation live here.** |
| 0x8003f80 | — | SAI2 + DMA2 + GPIO init (audio bring-up) |
| 0x8003edc | — | TIM1/TIM3/RCC setup (**pulse-output timing**) |
| 0x800445e | — | reads ADC1/2/3 (**delay-time / CV / pot control path**) |
| 0x8004702 | — | ADC + SysTick/NVIC (timed control sampler) |
| 0x8003d90 | — | ADC + GPIOF (switch/pot read) |
| 0x8004faa | 1476 B | memory-heavy (SDRAM buffer clear/copy candidate) |
| 0x8000a28 | 1188 B | integer-mul heavy (index/address math candidate) |

## Interrupt vectors of interest
- SysTick → 0x800425c → dispatcher 0x8004410
- TIM2_IRQ (28) → 0x800426c → shared dispatcher 0x8006748
- **DMA2_Stream1 (57) → 0x8004278** and **DMA2_Stream4 (60) → 0x8004284**, both → shared
  audio-DMA handler **0x8004ba0** (HAL_DMA_IRQHandler style; fires block callback indirectly).
- Default trap = 0x8004390 (`b .`), shared by 85 unused vectors.

## Working hypothesis for the "smooth modulation" fix
Audio runs SAI2 double-buffered via DMA2; ISR flips buffers, **main loop (0x8001180)
processes each block**. Delay output = read SDRAM at `write_ptr - delay_len`. Zipper/steppy
modulation almost certainly because:
1. the read index is **integer** (no fractional/linear interpolation on the SDRAM tap), and/or
2. the delay-time control value is applied **without slew/smoothing** (raw ADC each block).
Chorus/flanger needs: (a) fractional-delay interpolation on the SDRAM read, and
(b) a one-pole slew on the delay-time parameter. **Next phase:** disassemble the FPU section
of 0x8001180, confirm the read-index math, then verify live via SWD (breakpoint the block
loop, watch the read pointer while sweeping the delay pot).

## Tooling set up in `re/`
- `re/B288-REV1.0.bin` — raw image @ 0x08000000 (objcopy from hex).
- `re/.venv` — Python venv with **capstone 5.0.7** (keystone lib won't load on arm64;
  use `arm-none-eabi-as`/`gcc` to assemble patches instead).
- `re/scripts/analyze.py` — literal-pool + function + peripheral tagger.
- `re/disasm/full.thumb.asm` — full `arm-none-eabi-objdump` Thumb-2 listing.
- `re/notes/fw-map.txt` — analyzer output.
