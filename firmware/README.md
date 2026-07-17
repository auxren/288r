# firmware/ — rebuildable C reconstruction (Phase 3)

Goal: a **readable, editable, buildable** C firmware that replaces the shipped `.hex`, so features
(smooth modulation/chorus/flanger, pulse-output fixes, mode fixes) become ordinary source edits
instead of binary patches.

## Strategy — bare-metal BSP + reconstructed DSP (MARF house style)

The shipped firmware is standard STM32: peripheral init + a small hand-written DSP core. We hand-write
the init and hand-write clean C for the bespoke ~2–3 KB that actually is the 288r. This mirrors the
sibling **[MARF 248r](https://github.com/auxren/marf)** (same author, same STM32F4 family) — `Makefile`,
GitHub Actions CI, `docs/`, and its EEPROM/persistence pattern (see DESIGN.md "Persistence & recall").

**Note on the init layer:** MARF's vendored StdPeriph is the F40x-era library and has no F429 **FMC**
(SDRAM) or **SAI** driver — the two things the 288r most needs. So the BSP (`src/bsp/*`) is written
**bare-metal (direct CMSIS register access)** against the vendored F429 CMSIS device header
(`Libraries/CMSIS/`, from ST's `cmsis_device_f4`). Bare-metal is also more transparent to debug at the
bench. This toolchain ships libgcc but not newlib, so the image links `-nostdlib` + a tiny freestanding
runtime (`src/bsp/freestanding.c`).

| Layer | How we produce it | Source of truth |
|-------|-------------------|-----------------|
| RCC/PLL, SAI2+DMA2, FMC-SDRAM, ADC, SPI/I²C, TIM, GPIO | hand-written **StdPeriph** init matching recovered config | RM0090 + `re/notes/architecture.md`, analyzer, `init_sai_stream` (sub_1180) |
| **Delay engine** (write/read/tap/time/transport) | **hand-written C, `src/`** (done, host-tested) | our decompilation |
| Panel/preset/mixer glue, persistence | hand-written C (persistence ⇐ MARF) | `rename_288r.py` mapping, MARF |

This targets **functional/audible equivalence**, not a byte-identical image (not worth the cost).

## Layout (as it grows)
```
firmware/
  README.md            this file
  DESIGN.md            rewrite architecture (keep vs rewrite, new engine, quality/efficiency wins)
  Makefile             `make test` (host) / `make engine` (cross-compile) / `make firmware` (flashable image)
  STM32F429.ld         linker script (F429ZET6 map: 512K flash, 192K RAM, 64K CCM, 8M SDRAM)
  Libraries/CMSIS/     vendored F429 CMSIS device header + core + startup (ST/ARM, permissive)
  src/
    bsp/               bare-metal F429 bring-up: clock, sdram(FMC), audio_sai(+DMA), codec(I2C1), gpio
    delay_line.{h,c}   fixed-rate fractional delay line + interpolation (linear/Hermite)  [tested]
    taps.{h,c}         8-tap 288 model: phase-select, presets, per-tap slewed delay        [tested]
    time_control.{h,c} TIME MULTIPLIER: single-precision, slewed, no PLL/hysteresis         [tested]
    transport.{h,c}    WRITE / RECIRC (looper) state machine + loop-window capture          [tested]
    mixer.{h,c}        per-tap slider gains, phase select, summed output                    [tested]
    engine.{h,c}       integration: the per-sample signal flow tying it together           [tested]
    main.c             top-level: BSP init order + the TDM↔engine audio-ISR bridge + superloop
  test/
    test_delay_line.c  proves continuous, non-stepped fractional reads
    test_taps.c        phase/time scaling + smooth (non-stepped) modulation sweep
    test_engine.c      end-to-end: stability under TIME sweep, delayed output, RECIRC looping
```

Build & test:
```bash
cd firmware
make test      # build + run all host unit tests (no hardware)
make engine    # cross-compile the DSP engine for STM32F429 (compile-only proof)
make firmware  # link the flashable image -> build/fw/b288-community.hex
```

## Flashable image — builds now; bench-gated constants remain

`make firmware` produces a complete, linked image (`build/fw/b288-community.hex`): clock → SDRAM →
codec → SAI/DMA → the validated smooth-delay engine, 8 taps out the 8 DAC channels. It **builds and
links clean**, but it is a *bring-up* — every value tagged `[BENCH]` in `src/bsp/board.h` is a
best-effort guess and must be confirmed on the SWD/logic-analyzer session. **Do not expect audio on
the first flash.** The full procedure (one peripheral at a time, with a pass-check for each) is
[docs/bench-bringup.md](../docs/bench-bringup.md). Recovery is always SWD — reflash the golden stock
hex; SWD + the BOOT0 ROM bootloader make bricking effectively impossible.

Confirmed (bench 1) and already baked in: MCU **STM32F429ZET6**, **HSE 8 MHz → 168 MHz**, codec bus
**I2C1**, audio **SAI1** TDM (8×32-bit, 24-bit), SDRAM **IS42S16400** (timings from the -7 datasheet).

`[BENCH]` to confirm (all quarantined in `src/bsp/board.h`):
- **SAI clock chain** (`PLLSAI_*` + `SAI_MCKDIV`) to land exactly on 96 kHz, and whether codec MCLK is
  F429-generated or board-supplied.
- **Codec I²C address + register sequence** (sniff the stock power-up I²C) and the RESET pin.
- **Exact pins** (SAI AF6 set, I²C1, FMC), the **DMA stream/channel** map, and the **TDM slot→jack order**.
- **Calibration constants:** TIME taper, SHORT/FULL cycle lengths, slider gain law, AUTO CONTROL,
  pulse thresholds (parameterized; marked "calibrate on hardware").

For a *guaranteed-working* hardware test today, the **binary patch** in `re/patches/` (already
validated on the unit) hears the interpolation fix on the stock firmware.

## Not yet wired (next layers, not bugs)

The control surface (SPI2 ADC for Time-CV/pots/sliders, 74HC595/4051 DIP+trimmer scan), momentary-
switch transport gestures, the settings/calibration boot chord, and the staged rewrite DSP modules
(pitch `pitch_tap`, analog tone, int16/int32 SDRAM layer) are designed in `DESIGN.md` but not in this
first bring-up image. Until the control surface lands, the delay time sits at a fixed default — enough
to validate the whole audio path end to end.

## Binary ↔ source map (keep this honest as we go)
- `delay_line` + `transport`  ⇦ `delay_tap_service_A/B` (sub_1250 / sub_15dc)  [write/record]
- `delay_line` + `taps` + `crossfade` ⇦ tap read+mixer  (sub_1968 / sub_1c98)  [read/output]
- `time_control`              ⇦ `time_multiplier_and_envfollow` (sub_2030)
- `transport`                 ⇦ write/recirc/loop state machine (transport_mode @0x200000d0)
- `main()` / superloop        ⇦ `main_init_and_run_loop`  (sub_2508)

Stock RAM variables (e.g. `delay_write_ptr` @0x200000c4) are documented in
`re/binja/rename_288r.py`; the switch→function map is in `re/notes/hardware.md`.

## Flash the image
```bash
make firmware   # -> firmware/build/fw/b288-community.hex
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
    -c "program build/fw/b288-community.hex verify reset exit"
```
Follow [docs/bench-bringup.md](../docs/bench-bringup.md) to verify each peripheral and fill in the
`[BENCH]` constants. For a guaranteed-working test today, the **binary patches** in `re/patches/`
hear the interpolation fix on the stock firmware.
