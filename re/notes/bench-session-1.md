# Bench session 1 — live SWD readout (2026-07-16)

ST-Link V2 (STLINK V2J23S4) on the 288r, openocd 0.12.0, target running. **All read-only** (no flash
written). Resolves nearly every open `TODO(bench)` from `hardware.md`.

## Connection / safety
- Cortex-M4 r0p1 detected, Vtarget 3.21 V.
- **IDCODE `DBGMCU_IDCODE 0xE0042000 = 0x20036419`** → dev id **0x419** (STM32F42x/F43x), rev 0x2003.
- **`FLASH_OPTCR 0x40023C14 = 0x0FFFAAED`** → RDP bits [15:8] = **0xAA = Level 0 (OPEN)** → safe to
  dump/flash.
- Reset vector `mdw 0x08000000 2 = 0x20030000 0x08004341` = matches our reference hex.

## Firmware identity (patch go/no-go = GO)
- Dumped `0x08000000` 512 KB (`288r-unit-dump.bin`, kept as the golden restore).
- Diff vs `Compiled FW/B288-REV1.0.hex` over the used region (27,912 B): **4 bytes differ**, all at
  `0x080001AC` — our hex `0x00000000`, unit `0xFFFFFFFF` (an unused/reserved vector-table slot, IRQ≈91).
  **Immaterial; nowhere near a patch anchor.** The interpolation patch is valid for this unit.
  → For a byte-exact restore use the unit's own dump, not our hex.

## Clock tree (RESOLVED)
- `RCC_CR 0x33035C83`: HSE on+ready, PLL on+ready, PLLSAI on+ready.
- `RCC_PLLCFGR 0x04405408`: **PLLM=8, PLLN=336, PLLP=2, PLLSRC=HSE.**
- `RCC_CFGR 0x0000940A`: SW=PLL, HPRE=/1, PPRE1=/4, PPRE2=/2.
- → **HSE = 8 MHz** (M=8 gives 1 MHz VCO in), **SYSCLK/HCLK = 168 MHz, APB1 = 42 MHz, APB2 = 84 MHz.**
  Confirmed by `I2C1_CR2 FREQ = 0x2A = 42`.
- `RCC_PLLSAICFGR 0x2A001C40`, `RCC_DCKCFGR 0x00000000` — SAI clock from PLLSAI (the firmware retunes
  these for the delay-time octaves; see delay-engine.md).

## Codec bus (RESOLVED) — CS42888 is on **I²C1**
- `RCC_APB1ENR 0x10204003` → TIM2, TIM3, **SPI2**, **I2C1**, PWR enabled. `RCC_APB2ENR 0x00404411` →
  TIM1, USART1, ADC, **SPI1**, **SAI1** enabled.
- **`I2C1` (0x40005400): CR1=0x1 (PE, enabled), CR2=0x2A (42 MHz)** → active I²C bus = the CS42888
  control port.
- **`SPI2` (0x40003800): I2SCFGR=0x0 (I2SMOD=0 → SPI mode), CR1=0x374 (master, SW-NSS, enabled)** →
  SPI2 is the **control-surface SPI ADC** (sliders/pots, CS on GPIOB12), **not** the codec. This
  settles the earlier I²C-vs-SPI ambiguity.
- Still open: the exact codec I²C **address + register values** (needs an I²C sniff at power-up /
  logic analyzer — SWD can't sniff the bus).

## Audio format (RESOLVED) — it's **SAI1**, not SAI2
Correction: the SAI at `0x40015800` is **SAI1** (the F429 has only SAI1; earlier notes said "SAI2").
- **Block A (0x40015804): Master Receiver**, DS=110 → **24-bit**; **Block B (0x40015824): Slave
  Transmitter** (sync to A) — full-duplex, 4-in / 8-out.
- `FRCR 0x000200FF` → **256-bit frame**. `SLOTR 0x00FF0781` → **NBSLOT=7 (8 slots), SLOTSZ=32-bit,
  SLOTEN=0xFF (all 8 enabled)**.
- → **TDM: 8 slots × 32-bit, 24-bit data, 256-bit frame.** Confirms `firmware/src/audio_io.c` (8 taps →
  8 DAC slots). Slot→tap *order* still needs a live audio test.

## What's left for the bench
- Flash the patch + **listen** (validation checklist) — the one step needing ears.
- Codec I²C address + register dump (logic analyzer at boot).
- Slot→tap order (play a known tap, see which slot).
- Control min/max calibration — can be read live: watch `slider_raw_table @0x2000035c` (and the ADC
  path) while the operator sweeps each control (`fw-openocd-swd-live-debug` technique).
