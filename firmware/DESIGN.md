# 288r community firmware — rewrite architecture

The reverse engineering (see `re/notes/`) showed the stock firmware works, but its **engine
architecture** is what blocks smooth modulation and wastes cycles. A clean-room rewrite lets us
fix the architecture, not just patch around it. This doc is the north star for that rewrite.

Goal: **reproduce the 288's musical behavior and panel, but with a better-sounding, more efficient
engine** — and make continuous delay-time modulation (chorus/flanger/pitch effects) first-class.

## Two architectural problems found in the stock firmware

1. **Delay time is changed by retuning the audio sample clock.** `time_multiplier_and_envfollow`
   (sub_2030) reprograms the SAI PLL (`RCC_PLLSAICFGR`/`RCC_DCKCFGR`) in octave steps with
   hysteresis to move the coarse delay. Re-locking the PLL glitches and is inherently discrete —
   you cannot smoothly sweep it. This is the root reason chorus/flanger don't work.
2. **Control math runs in software double precision.** sub_2030 and the tap/time path call
   `__aeabi_dadd/dmul/ddiv/…` (libgcc soft-float **doubles**) on a Cortex-M4F whose FPU is
   **single-precision hardware**. Every delay-time update pays a large, needless cycle tax.

Plus the read tap is integer (`roundf`, single fetch) — no interpolation.

## Keep vs. rewrite

| Area | Decision | Why |
|------|----------|-----|
| Panel layout, controls, presets, mixers, transport semantics | **Keep** (behavioral parity) | it's a 288 — the interface is the instrument |
| "Vintage" lo-fi character (bit-depth reduction, low base rates) | **Keep as an option/feature** | part of the sound; make it deliberate, not a side effect |
| Peripheral init (RCC, SAI2, DMA2, FMC-SDRAM, ADC, SPI/I²C, TIM, GPIO) | Hand-write on **StdPeriph** (StdPeriphLib), matching the recovered config | **aligns with the MARF 248r house style** (github.com/auxren/marf uses StdPeriph, not CubeMX) — reuse its `Libraries/` + build/CI/docs scaffolding |
| Delay time via PLL octave retune | **Rewrite** → fixed base rate + fractional read offset | enables smooth modulation; no clock glitches |
| Control math in soft-float doubles | **Rewrite** → single-precision hardware float / fixed-point | large efficiency win, same audible result |
| Integer tap read | **Rewrite** → interpolated fractional delay line | smooth sweeps, better audio quality |
| Envelope followers (256/64-tap running sums) | **Rewrite** → one-pole followers | cheaper, equivalent behavior |

## New engine architecture

**Fixed base sample rate per cycle-length range; modulation is always fractional offset.**
The stock design lowered the sample rate to get longer delays (that's how ~40 s fits in SDRAM),
which is also part of the lo-fi character. We keep a small set of **selectable base rates** for the
coarse SHORT/FULL cycle ranges (a memory/character tradeoff, chosen once when the range changes) —
but **all fine and continuous time changes are done by moving a fractional read pointer at the
current fixed rate**, never by retuning the clock. Result: the long-delay capability and lo-fi
option are preserved as features, while time modulation becomes glitch-free and continuous.

```
 codec in ─▶ [input mixer] ─▶ write → SDRAM circular buffer (int16 vintage / int32 hi-fi)
                                   │  (optional vintage quantize+dither on write)
 8 taps ─▶ for each tap:  delay_samples = base_time · phase[i] · time_mult(+CV, slewed)
                          out_i = interp(delay_line, delay_samples)   ← fractional, HW float
        ─▶ [output mixer + phase select] ─▶ [auto control] ─▶ codec out
                          pulse taps ─▶ pulse outputs
```

- **Buffer sample format:** the host reference `delay_line` stores `float32` for clarity/testing;
  the SDRAM-backed build stores **int16 (vintage) / int32 (hi-fi)** with the layout tied to the
  fidelity switch — full spec in **"Memory & fidelity — SDRAM buffer layout"** below.
- **Interpolation** selectable: linear (cheap) → 4-point cubic/Hermite (better HF) → optional
  first-order all-pass (flat magnitude, ideal for flanger). See `src/delay_line.c`.
- **Delay-time control**: raw ADC/CV → single-precision → one-pole slew → fractional `delay_samples`.
  A slow LFO or CV then sweeps the taps continuously (chorus/flanger) with no zipper.

## Memory & fidelity — SDRAM buffer layout

Hardware: SDRAM = ISSI IS42S16400, **8 MB, 16-bit** @ `0xC0000000`; codec 24-bit / 96 kHz
(re/notes/hardware.md).

### How the stock firmware does fidelity (from the RE)
- **Bit-depth reduction**, 3 levels: on write, samples are arithmetic-right-shifted by **20 / 16 /
  12 bits** (12 = "vintage"); restored by an equal left-shift on read (`sub_1250` / `sub_1968`).
  Even "20-bit" is < 24, so there is always some reduction.
- **Selected live** by a 2-bit value read from a front-panel switch via the 74HC595/GPIO scan
  (`get_mode_2bit` ← RAM `0x20000360`). **No menu, no NVM** — it's a physical switch position,
  re-read each cycle.
- **The inefficiency:** the stock stores the reduced-depth sample in a **full int32 word**, in **two
  full-length banks** (`bank_A` always; `bank_B` parallel-written at the same index in a switch mode
  = the recirc/loop path). At max length that's ~7.2 MB of 8 MB — so it does **not** reclaim the
  vintage savings; its 40 s comes from lowering the sample rate, not from packing.

### Rewrite rule: fidelity selects storage width AND memory layout
Store the *actual* width, so vintage buys memory instead of wasting it:

| Fidelity mode | Sample store | 8 MB buys (96 kHz, mono) |
|---|---|---|
| **Vintage (≤16-bit)** | **int16 (2 B)** | **two full ~20 s banks**, or one ~40 s bank |
| **Hi-fi (24-bit)**    | **int32 (4 B)** | one ~20 s bank, or two ~10 s banks |

- Interpolation and mixing are **always hardware float**; convert int16/int32 ↔ float only at the
  buffer boundary (cheap VCVT; SDRAM bandwidth is a non-issue at these rates).
- **float32 storage is rejected**: no audible gain over int32 for a bounded audio delay line, and it
  halves max delay for nothing. int32 = same 24-bit-clean quality at the same 4 bytes.

### What the two banks are for
Keep the stock's **record + recirc/loop** pair (`bank_A` = live/record, `bank_B` = loop/recirc). Two
banks give seamless looping/overdub and a clean home for the **pitch-wrap crossfade fix** (dual read
head). Optional alternative for bank 2: **stereo / A-B layering** — a product-shape choice; **DECIDE
before wiring `audio_io`**.

### Boot-time layout rule
Fidelity is a live switch, but the SDRAM layout (bank count × width × length) can't change per sample.
**Read the fidelity switch at boot and lay out SDRAM once.** Treat a mid-run fidelity change as a
**buffer reinit** (brief clear) — same spirit as the stock re-reading cycle length on the fly. Do not
try to preserve buffer contents across a width/bank-count change.

### Open (needs the bench)
- Which physical switch selects fidelity (CTS 208-4 "mode" DIP vs the A/B toggle) — from the pin map.
- Confirm `bank_B`'s exact enable (switch bit tested by `sub_4310(6)`) and role, to clone it faithfully.

## Audio-quality improvements
- Fractional/interpolated taps → glide instead of stepping; usable pitch/chorus/flanger.
- Full-precision float internal path; "vintage" bit-depth becomes an explicit, dithered option.
- More output headroom / consistent gain staging in the mixer (verify against panel on hardware).
- **Pitch/Time buffer-wrap glitch (reported bug, re/notes/hardware.md):** in pitch mode a saw-LFO
  sweeps delay time and clicks when it resets at the buffer wrap. Fix = a **dual-head crossfaded
  read** (classic crossfaded pitch tap) so the wrap is hidden. Host-testable: assert no output
  discontinuity across the wrap. Needs the pitch-mode control context (bench) before wiring.

### Delay-time changes: glide vs. crossfade (sequencing the TIME control)
A sudden delay-time change does **not** alter the recorded/feedback audio, but it **teleports the
read pointer** to a new point in the buffer → a waveform step = **click** (and with feedback it
recirculates). Two clean treatments, chosen by intent, plus a control to pick:
- **Glide (portamento):** one-pole slew the tap positions (we already do this in `taps.c`) →
  clickless **pitch-glide** between values — the tape/BBD/chorus/flanger character.
- **Crossfade (snap):** change delay instantly but **crossfade the read old→new over ~2–10 ms** via
  the dual read head → clickless **and** glide-less — the right feel for stepped/sequenced delay
  times. Reuses the same dual-head as the pitch-wrap fix.
- **Control:** a single `glide` amount — **`glide = 0` → crossfade-snap**, `glide > 0` → portamento
  at that rate. Auto default: small deltas use the slew; large/discrete jumps trigger the crossfade.
  Host-testable: assert no discontinuity for a stepped time change at `glide = 0`.

## Efficiency improvements
- Kill soft-float doubles → hardware single-precision FPU (the big win in the control path).
- No PLL re-lock on delay changes (no clock-domain glitch, no re-lock latency).
- Block-based processing over the SAI DMA half/full buffers; one-pole followers; SDRAM access
  patterned for the FMC (sequential write head; interpolation reads are local ±2 samples per tap).

## Persistence & recall (mirror the MARF 248r — *if* the 288r has NVM)
The sibling **MARF 248r** (github.com/auxren/marf) solves NVM cleanly on the same F4 family. **Whether
the 288r actually has a 25512 SPI EEPROM is UNCONFIRMED** (BOM lists `25AA512` but ambiguously; MARF
uses `CAT25512`, which makes it plausible but not proven — verify on the board and/or check whether
the stock `.hex` ever drives an SPI EEPROM). **Resolved: there is no external EEPROM.** The BOM's `25AA512` was a paste error over a 20-pin
connector MPN, and the stock `.hex` uses no SPI EEPROM (only SPI2 = codec). So back all persistence
with **STM32F429 internal-flash EEPROM emulation** (reserve a sector, dual-sector wear-level, write on
explicit save, CRC + default fallback). Prefer a **physical control** where one exists (self-persisting,
no writes). The record/layout/pinning design below is backing-store-agnostic — here the store is
internal flash.

**Mode-entry gesture (from the BOM):** only two panel switches are momentary — **SW14
`(ON)-OFF-(ON)`** and **SW16 `ON-OFF-(ON)`**. A **power-up hold** on one of these is the natural
gesture to enter a calibration/save mode (their runtime role is likely manual write/recirc, so a
boot-time hold doesn't conflict). The `cal./pre-set` and `A/B/C` toggles are latching (persistent
state), not gesture switches.
- **Driver** (`eeprom_25512.{h,c}` ⇐ MARF `CAT25512.c`): `init / read_block / write_block / erase`
  over SPI + a CS GPIO. Confirm the 288r's SPI peripheral + CS pin on the bench.
- **Layout** (`eprom.{h,c}` ⇐ MARF `eprom.h`): an `EpromMemory` of `MemoryRange {start,size}`
  partitions — e.g. settings block, calibration block, (optional) user presets.
- **Versioned + checksummed records** (`storage.h` ⇐ MARF `storage.h`): every block is
  `{ magic, version, crc16, payload }`; on load verify magic+version+CRC-16/CCITT and **refuse
  invalid/older-format blocks** (fall back to defaults, never memcpy garbage). Bump a `*_VERSION` to
  cleanly invalidate old formats. `static_assert` frozen field offsets.
- **Control pinning on recall** (⇐ MARF "slider pinning"): the 288r reads trimmers/switches **live**,
  so a recalled soft value disagrees with the physical control. On recall, **pin** each affected
  parameter to the stored value and **ignore the physical control until it sweeps through** that
  value, then hand back live control. This is the key to reconciling live-read hardware with saved
  state — use it for anything persisted (e.g. the `glide` amount, calibration).
- **What to persist first:** the **glide/crossfade** setting and **calibration** (TIME CV range,
  taper, cycle-length sample counts). User presets are a later feature-phase item.
- **UX to set it:** prefer a **physical control** (trimmer/DIP) where possible (self-persisting, no
  writes); use the EEPROM for values with no dedicated control, set via a **save gesture** (MARF-style
  button/switch combo) or a USB/SWD config tool. Write on explicit save, not per change (EEPROM ~100k
  cycles; internal-flash emulation is a fallback if the chip turns out absent).

## Calibration routine (features phase; the Time-CV fix justifies it early)
The stock "cal." toggle is a **live setup mode** (short buffers + ×2 + taps track the raw control), not
a stored min/max calibration — and there's no NVM. Worth **adding** a proper power-up calibration,
because the panel has many ADC-read analog controls and a **real reported bug** (narrow usable Time CV
range) that is a scaling/offset problem. Mirrors the MARF's `StoredCal` + two-point (min/max) cal.

- **Enter:** hold a **momentary switch (SW14 or SW16) at power-up** → calibration mode (LED feedback).
- **Capture:**
  - **Controls min/max:** prompt "sweep every slider/pot to both extremes" → record ADC min/max per
    channel (9 sliders + 7 pots; optionally the 36 trimmers). Fixes off-isn't-silent / can't-reach-full.
  - **CV inputs:** apply known voltages (e.g. 0 V and +5 V, or +1 V/oct points) → record → compute
    **gain + offset** per CV. This is the fix for the narrow **Time CV** range.
- **Store:** versioned+checksummed `StoredCal` in **internal-flash** (see Persistence). CRC + default
  fallback so an uncalibrated/older unit still runs sanely.
- **Apply at runtime:** normalize every control read through its stored min/max/offset; use
  **control-pinning** on any recalled value. Start with **sliders + Time CV**, expand later.
- **Doc:** a numbered `docs/` calibration procedure, MARF-style.

## Module plan (`firmware/src/`)
```
delay_line.{h,c}   fixed-rate fractional delay line + interpolation      ← done, tested
taps.{h,c}         8-tap 288 model: phase-select, presets, per-tap slewed time  ← done
time_control.{h,c} ADC/CV → slewed single-precision delay_samples (no PLL)      ← done
transport.{h,c}    write / recirc / loop state machine                          ← done
mixer.{h,c}        input & output mixers, phase select, auto control            ← done
envelope.{h,c}     one-pole AUTO CONTROL followers                              ← done
engine.{h,c}       per-sample integration                                       ← done
crossfade.{h,c}    dual read-head crossfade (pitch-wrap fix + glide=0 snap)     ← to build
eeprom_25512.{h,c} SPI EEPROM driver (⇐ MARF CAT25512)                          ← to build
storage.h          versioned+checksummed record formats (⇐ MARF)               ← to build
eprom.{h,c}        EEPROM memory layout (⇐ MARF)                                ← to build
settings.{h,c}     glide/crossfade + load-at-boot, control-pinning              ← to build
calib.{h,c}        power-up cal (SW14/16 hold): slider/pot min/max + CV gain/off ← to build (features)
audio_io.{h,c}     SAI2/DMA2 (CS42888 TDM) block callbacks, format conversion   ← to build (bench)
panel.{h,c}        595 scan: switches, DIP presets, 4051-muxed trimmers         ← to build (bench)
main.c             StdPeriph init + superloop                                   ← skeleton
```
Reuse the MARF's `Libraries/` (CMSIS + STM32F4xx StdPeriph), `Makefile`, `.github/workflows/build.yml`
(host `make test` + arm build + tagged `.hex` release), and numbered `docs/` conventions.

## Open decisions & dependencies
- **Scope/fidelity: DECIDED → strict behavioral clone first.** Match the 288r's panel mappings,
  presets, transport semantics, tap model and "vintage" character exactly; improve the engine only
  where it was broken (interpolated fractional taps, fixed-rate time, hardware float). Features,
  extra modulation, and remapped controls come *after* the clone is nailed. Constants recovered from
  the binary are used directly; anything not fully pinned is marked "calibrate on hardware".
- **Hardware confirmation (needs the SWD/bench session):** exact codec part + control wiring, SDRAM
  size (sets max delay at each base rate), pulse-output driver (bug #2 may be partly analog), and
  the real control→parameter scaling so we match the panel. Until then the engine is developed and
  unit-tested host-side; hardware brings it up against the real board.
