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

## Flashable image — v1.0.1: works on hardware, feature-complete against the stock

`make firmware` produces the release image (`build/fw/b288-community.hex`): clock → SDRAM → codec →
SAI/DMA → the smooth-delay engine, 8 taps out the 8 DAC channels. As of **v1.0.1** (bench sessions
1–7, owner-verified on the unit) it is a working replacement for the stock firmware:

- **Smooth delay-time modulation** — the headline chorus/flanger fix: fractional Hermite taps with a
  10 ms multiplier glide (the 67 µs control slew that caused broadband zipper is fixed).
- **Multiplier knob calibrated to the panel legend** (TIME mode): 7-point owner-measured curve —
  0.4 at the CCW stop, 1.0 at noon, 1.6 at the CW stop, all marks reading true.
- **Stock control law:** mult = knob + CV × attenuverter (center detent = CV ignored, CCW inverts),
  live from boot in both modes.
- **Pitch mode** (stock semantics on the crossfaded shifter): all 8 taps carry the shifted voice
  (crossfaded replace; zero depth = clean dry), knob = pitch-down depth (−1.07 st FULL / −4.75 st
  SHORT cycle) with an exact-unity snap at the bottom of travel; CV bipolar 1.2 V/oct through the
  attenuverter (ratio bounded ±2 oct); ~15 ms glide; coherence-adaptive crossfade kills the splice
  AM (tone envelope ripple 0.33/0.03 dB); exact int+frac delay reads; per-tap 0–9 ms decorrelation.
  Feedback patching cascades the shift per pass (H949-style spirals).
- **Panel live:** full switch scan (A/B/C presets, ×1/×2, cycle, store beg./end, transport
  momentary), pulse input jacks (write/recirc/arm), and indicator LEDs — the input mixer LED is a
  whole-chain CLIP indicator (~¼ s hold; stock >½-FS comparator behind `LED_INPUT_CLIP_MODE 0`),
  the AUTO CONTROL LED lights only while audio exceeds the sens. threshold.
- **sens. knob** (analog attenuator feeding codec ADC slot 1): sets how quiet a signal still
  triggers the AUTO LED and auto/looper capture; full CCW disables both.
- **Savable presets:** hold write ~2 s → the selected A/B/C slot, flash-persistent, control-pinning
  on recall.
- **Settings = the 4 rear DIPs only (locked):** sw1 ×10 extend, sw2 bandwidth limit, sw3/sw4
  resolution — boot-time straps, power-cycle to apply. The front DIP matrix is never read; the
  presets cover everything it did on the stock.
- **Soft-knee output limiter** (transparent below 0.75 FS): external feedback patching blooms
  tape-style instead of flat-topping. (Feedback is external patching only, by design.)

Host tests: **26 suites** (`make test`). Recovery is always SWD — reflash the golden stock
hex; SWD + the BOOT0 ROM bootloader make bricking effectively impossible.

Bench-verified and baked in: MCU **STM32F429ZET6**, **HSE 8 MHz → 168 MHz**, codec **CS42448** on
**I2C1**, audio **SAI1** TDM (8 slots × 32-bit, 24-bit), **SDRAM = FMC bank 2**, Time-CV = SPI2
ADC ch0.

**Known hardware fault (this unit, not firmware):** codec slot 4 is hot on the TDM bus but reaches
no slider in any phase-switch position → broken analog path on the board (check that AOUT net:
solder joint, coupling cap, buffer op-amp section). The firmware keeps the identity mapping
(slider N = tap N) so the panel legend stays honest; the SWD `g_dac_solo` solo tool remains in the
build for post-repair verification.

## Still open (not release blockers)

- **"signal in" (TIME section):** proven NOT a codec channel — it reaches the multiplier through
  the analog Time-CV net (summed with c.v. in, scaled by the attenuverter). `[BENCH]`: the exact
  summing point is unconfirmed. Practically, signal-in envelope modulation of delay time works
  through the CV path (keep the attenuverter up to hear it).
- **SENS_REF** (the fixed 0.02 FS threshold behind the sens. knob): calibrate by feel.
- **Calibration routine** (sliders/pots/36 trimmers/CV): spec in DESIGN.md, not yet implemented.
- **Debug scaffolding retained intentionally in v1.0.1** (SWD-only, invisible in normal use):
  `g_dbg_panel` telemetry, `g_dac_solo`, `sdram_memtest` — documented in
  [docs/bench-runbook.md](../docs/bench-runbook.md); strip in a future release once the slider-5
  repair is verified.

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
The **binary patch** in `re/patches/` remains the drop-in interpolation fix for anyone staying on
the stock firmware.
