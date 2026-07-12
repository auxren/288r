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
  Makefile             `make test` (host) / `make engine` (cross-compile) / `make firmware` (blocked)
  STM32F429.ld         linker script (memory map; sizes marked CONFIRM for the bench session)
  src/
    delay_line.{h,c}   fixed-rate fractional delay line + interpolation (linear/Hermite)  [tested]
    taps.{h,c}         8-tap 288 model: phase-select, presets, per-tap slewed delay        [tested]
    time_control.{h,c} TIME MULTIPLIER: single-precision, slewed, no PLL/hysteresis         [tested]
    transport.{h,c}    WRITE / RECIRC (looper) state machine + loop-window capture          [tested]
    mixer.{h,c}        per-tap slider gains, phase select, summed output                    [tested]
    engine.{h,c}       integration: the per-sample signal flow tying it together           [tested]
    main.c             top-level superloop + SAI/DMA block callback (HAL calls are TODO/cube)
  test/
    test_delay_line.c  proves continuous, non-stepped fractional reads
    test_taps.c        phase/time scaling + smooth (non-stepped) modulation sweep
    test_engine.c      end-to-end: stability under TIME sweep, delayed output, RECIRC looping
  cube/                CubeMX .ioc + generated HAL  (BLOCKED — needs board pinout)
```

Build & test (no hardware needed):
```bash
cd firmware
make test      # build + run all host unit tests
make engine    # cross-compile the whole engine for STM32F429 (proves it builds; ~1.8 KB)
```

## Blocked on hardware (the bench / SWD session)

The engine is done and tested; a **flashable image** needs board-specific facts we can only get
from the unit. Everything below is a `TODO(bench)`/`TODO(cube)` in the source:

- **Exact part & memories:** F429 flash variant (512K/1M/2M), external SDRAM chip + size and FMC
  timings (sets max delay). `STM32F429.ld` sizes are marked CONFIRM.
- **Pinout → CubeMX `cube/`:** which GPIO/ADC/I²C/SAI/TIM pins map to the codec and each control.
  This produces `SystemClock_Config` + `MX_*_Init` (the `extern`s in `main.c`).
- **Codec:** part number, control interface, word format & channel layout (the int24↔float
  conversion and `audio_block()` layout in `main.c` assume left-justified 24-bit — verify).
- **Clock tree / base rate:** crystal freq, PLL/PLLSAI config, chosen fixed sample rate.
- **Calibration constants:** the TIME taper curve, SHORT/FULL cycle lengths in samples at the base
  rate, output-mixer slider gain law, AUTO CONTROL behavior, and pulse-output thresholds/levels.
  These are parameterized in the engine and marked "calibrate on hardware".

Until then, the **binary patch** in `re/patches/` is the way to hear the interpolation fix on the
stock firmware.

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
