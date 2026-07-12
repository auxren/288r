# firmware/ — rebuildable C reconstruction (Phase 3)

Goal: a **readable, editable, buildable** C firmware that replaces the shipped `.hex`, so features
(smooth modulation/chorus/flanger, pulse-output fixes, mode fixes) become ordinary source edits
instead of binary patches.

## Strategy — regenerate the boilerplate, reconstruct the DSP

The shipped firmware is standard STM32: **HAL/CubeMX-style peripheral init + a small hand-written
DSP core.** We don't hand-decompile the boilerplate; we regenerate it from the config we recovered
from the binary, and hand-write clean C for the bespoke ~2–3 KB that actually is the 288r.

| Layer | How we produce it | Source of truth |
|-------|-------------------|-----------------|
| RCC/PLL, clocks | CubeMX project for STM32F429 matching recovered PLL/SAI clock setup | RM0090 + `re/notes/architecture.md` |
| SAI2 + DMA2 (codec I/O) | CubeMX (full-duplex, ping-pong, 24-bit) | analyzer + `init_sai_stream` (sub_1180) |
| FMC SDRAM (delay buffer) | CubeMX FMC/SDRAM (bank, timings) | SDCR @0xA0000140, buffer lengths |
| ADC1/2/3, I²C, TIM, GPIO | CubeMX | function map |
| **Delay engine** (write/read/tap/time/transport) | **hand-written C, `src/`** | our decompilation |
| Panel/preset/mixer glue | hand-written C | `rename_288r.py` mapping |

This targets **functional/audible equivalence**, not a byte-identical image (not worth the cost).

## Layout (as it grows)
```
firmware/
  README.md            this file
  DESIGN.md            rewrite architecture (keep vs rewrite, new engine, quality/efficiency wins)
  src/
    delay_line.{h,c}   fixed-rate fractional delay line + interpolation (linear/Hermite)  [done, tested]
    (taps, time_control, transport, mixer, audio_io, panel, main ...  to come — see DESIGN.md)
  test/
    test_delay_line.c  host unit test (proves continuous, non-stepped fractional reads)
  cube/                CubeMX .ioc + generated HAL (to be added)
  Makefile / CMake     arm-none-eabi build -> B288-community.hex   (to be added)
```

Build+run the host tests:
```bash
cc -std=c11 -Wall -Wextra -Ifirmware/src firmware/test/test_delay_line.c \
   firmware/src/delay_line.c -o /tmp/tdl -lm && /tmp/tdl
```

## Binary ↔ source map (keep this honest as we go)
- `delay_write_sample()`     ⇦ `delay_tap_service_A/B`  (sub_1250 / sub_15dc)
- `delay_read_tap()`         ⇦ tap read+mixer            (sub_1968 / sub_1c98)
- `time_multiplier_update()` ⇦ `time_multiplier_and_envfollow` (sub_2030)
- `main()` / superloop       ⇦ `main_init_and_run_loop`  (sub_2508)

RAM variable addresses (e.g. `delay_write_ptr` @0x200000c4) are documented in
`re/binja/rename_288r.py` and mirrored as named fields in `struct delay_state`.

## Build (once cube/ + Makefile land)
```bash
make            # -> firmware/build/B288-community.hex
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
    -c "program firmware/build/B288-community.hex verify reset exit"
```
Until then, use the **binary patches** in `re/patches/` for hardware bring-up and A/B testing.
