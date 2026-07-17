# 288r community firmware — rewrite architecture


> **Authoritative spec:** the reconciled, buildable implementation plan is in **[§ Definitive implementation spec](#definitive-implementation-spec-synthesized-2026-07-16)** at the bottom of this file (5-lens design synthesis, 2026-07-16). It supersedes the older *Module plan* section. This header material remains as design rationale.
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
- **BUILT + host-tested: `src/audio_buffer.c`.** Implements exactly this — int16/int32 circular
  storage under a float delay line, mirroring `delay_line`'s interface (init/clear/write/read/read_at/
  read_loop/advance_loop) so it's a drop-in. Measured (`test/test_audio_buffer.c`): I16 gives 2× the
  samples of I32 for the same bytes; I32's Hermite read matches the float `delay_line` kernel to 3.7e-9
  (near-lossless), I16 sits at the 16-bit floor; vintage crush + clamp + wrap verified. **Not yet wired
  into the engine** — a bench-gated swap of `delay_line_t` for `audio_buffer_t` in `engine_t`, with the
  fidelity switch picking `AB_FMT_I16`/`I32` and `ab_set_vintage()` replacing the pre-write quantize.

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

> **Superseded** by the *Definitive implementation spec* below (kept for history; the spec's §3 is the current module/file plan).
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
audio_io.{h,c}     CS42888 TDM block: int24<->float, 8 taps -> 8 DAC slots      ← done, tested
                   (the SAI2/DMA2 init + IRQ wiring is the bench-gated part)
panel.{h,c}        595 scan: switches, DIP presets, 4051-muxed trimmers         ← to build (bench)
main.c             StdPeriph init + superloop                                   ← skeleton
```
Reuse the MARF's `Libraries/` (CMSIS + STM32F4xx StdPeriph), `Makefile`, `.github/workflows/build.yml`
(host `make test` + arm build + tagged `.hex` release), and numbered `docs/` conventions.

## Direction (updated 2026-07-16, after hardware validation)
The interpolation fix is **validated on hardware** for time-mode modulation (bench-session-2). Pitch
static + residual ringing are rewrite-level. Decision: **full clean rewrite — free to replace any
stock code; not bound to match the binary.** Constraints & goals:
- **Keep the device's feature set** (8 taps, phase/preset A–D, short/full cycle, write/recirc/looper,
  input+output mixers, auto control, pulse I/O, time + **pitch** modes, vintage option). Behavior can
  be re-implemented better; the *capabilities* stay.
- **Highest fidelity this hardware allows**, with a lean toward a **slightly more analog tone**:
  full 24-bit path, high-quality interpolation, clean gain staging; plus optional analog-flavor DSP
  (gentle HF roll-off / feedback-path filter / soft saturation).
- **Pitch mode:** fix via the **dual-head crossfade** (`crossfade.{h,c}`) — validated as the needed
  approach on the bench.
- **Time control:** continuous, slewed, no PLL octave-stepping, no hysteresis (kills the ringing).

## New features (this build)
- **Boot chord → calibration/settings mode:** at power-up, if the **PULSE IN** and **ARM PULSE IN**
  switches are both held **left**, enter a cal/settings mode (features TBD — control min/max cal,
  fidelity, analog-tone/filter params, …). Read those switch positions once at boot (panel scan);
  exact pins TBD on the bench.
- **Modal "hold-a-chord + turn-a-knob" settings** (e.g., set a **tone/filter** cutoff): a held
  switch combo re-purposes a knob to a setting. Powers the analog-tone filter and other params
  without adding panel controls. Persist via internal-flash (see Persistence).

## Open decisions & dependencies
- **Hardware confirmation (needs the SWD/bench session):** exact codec part + control wiring, SDRAM
  size (sets max delay at each base rate), pulse-output driver (bug #2 may be partly analog), and
  the real control→parameter scaling so we match the panel. Until then the engine is developed and
  unit-tested host-side; hardware brings it up against the real board.

---

## Definitive implementation spec (synthesized 2026-07-16)

*Reconciles five expert design lenses (fidelity/analog-tone, pitch-crossfade/anti-alias, settings/cal, engine-integration/real-time, feature-preservation) into one buildable plan. Sub-headings are demoted one level to nest under this section.*

Status: supersedes the *Module plan* section above. Grounded against the live code (`firmware/src/`, 7 host suites all green) and bench sessions 1–2. **Note:** the BSP is bare-metal, not StdPeriph (MARF's StdPeriph is F40x-era, no F429 FMC/SAI); the first flashable bring-up image now builds (`make firmware`) — see the *Flashable image* section of `firmware/README.md` and `docs/bench-bringup.md`.

Conventions: **[BENCH]** = exact constant/behavior gated on a hardware measurement; **[DELTA]** = deliberate, justified change from stock. Stock flash addr = `0x08000000 + sub_X`.

---

### 1. Executive summary (the architecture in 8 bullets)

1. **Fixed 96 kHz, forever.** The codec clock is never retuned (kills the octave-step ringing at its source). All delay-time and pitch variation is a fractional read offset into the SDRAM buffer. PLLSAI is set once at boot.
2. **float32 internal, int16/int32 at the SDRAM boundary only.** M4F single-precision hard-FPU throughout; zero soft-doubles (deletes the stock's biggest CPU tax). `1.0f = 24-bit codec full scale`. Storage width (int16 Q15 / int32 Q23) and bank split are latched at boot from the live resolution switch; intN↔float is a 1-cycle VCVT at the buffer edge.
3. **Block processing with mandatory CCM prefetch.** `engine_process_block()` runs `BLK = 32` frames per SAI1 DMA half. Each tap's read span is burst-copied SDRAM→CCM float scratch once per block (turns 8 random FMC row-activations/sample into sequential bursts). This is the single most important RT decision: it drops pitch-mode worst case from ~91 % to ~49 % of budget.
4. **Two read frontends, mode-selected per tap.** TIME mode = slewed single-head read with `crossfade.c` handling `glide=0` snaps (validated). PITCH mode = a **new continuous dual-head windowed reader** (`pitch_tap.c`, H910/rotating-head style) that geometrically excludes the write-head discontinuity — the fix for the bench-2 broadband static.
5. **Continuous, slewed time control — no PLL, no hysteresis.** `time_control.c` one-pole slew on a single-precision exponential multiplier map. This is the chorus/flanger smoothness fix, validated in direction by bench-2.
6. **"Slightly analog" is an opt-in, orthogonal voice.** Default ships **neutral/bit-honest** (parity first). Analog character = per-tap one-pole HF roll-off + in-loop feedback roll-off + soft saturation + wow/flutter, each individually toggleable, all persisted. Per-tap because the "mixed" jacks are an analog op-amp sum of the 8 DAC outs — there is no digital master bus to color.
7. **MARF-style persistence + new modal UI.** Versioned/CRC16, dual-sector wear-leveled internal-flash store; control-pinning so stored values never fight live panel controls. New boot-chord (PULSE IN + ARM PULSE IN held LEFT at power-up) → settings/cal mode; new "hold-a-chord + turn-a-knob" relative param editor.
8. **Three execution contexts, strictly layered.** SAI1 DMA ISR owns the entire audio path (never touches I²C/SPI/flash); SysTick 1 kHz owns panel scan + control smoothing + pulse timing; the superloop owns settings/flash/cal. Control crosses SysTick→ISR as one double-buffered `params_t` snapshot (word-sized publish index, lock-free).

---

### 2. Definitive per-sample / per-block signal flow

Per SAI1 DMA half-block of `BLK = 32` frames. `∀s` = per sample, `∀t` = per tap (NUM_TAPS = 8).

```
                          CS42888  (control: I2C1)
   ADC slots 0..3 ──SAI1_A Master-RX──▶ DMA2 circular RX[2][BLK*8]  (SRAM1 — DMA can't reach CCM)
        │
        ▼  AUDIO ISR (half / complete), all state in CCM
 ┌──────────────────────────────────────────────────────────────────────────────────┐
 │ 0. publish→snapshot params_t (gains, mult target, mode, tone/sat/wow, cal, banks)  │
 │                                                                                    │
 │ 1. ∀s  INPUT COND:  in  = int24→float(RX[s][IN_SLOT])         (audio_io)          │
 │        x = input_mixer(in..., in_gain[])   → DC-block HPF ~5 Hz  → write_sat       │
 │                                                                                    │
 │ 2. ∀s  WRITE PATH (transport):                                                     │
 │        WRITE   : w = x                                                             │
 │        RECIRC  : w = x + fb_sat( fb_tone( fb_gain · loop_read ) )   ← in-loop      │
 │                       fb_tone = one-pole LP (tape/BBD per-repeat darkening)        │
 │                       fb_sat  = soft clip (feedback can never blow up)             │
 │        vintage : w = dl_vintage_quantize(w, bits, dither?)  (+ optional ZOH crush) │
 │        ab_put(bankA, wpos, w)  [int16: sat-rail + TPDF inside]  ; mirror bankB if  │
 │        enabled. RECIRC advances loop window instead of head.                       │
 │                                                                                    │
 │ 3. ∀s  CONTROL:  mult = tc_update(raw01)   (slewed, NO PLL, NO hysteresis)         │
 │        TIME : taps_update → cur[t] one-pole slew toward base·phase[t]/160·mult     │
 │        PITCH: pitch_ratio = slewed ρ_panel;  ptap ratios set from pitch_law        │
 │                                                                                    │
 │ 4. ∀t  PREFETCH (once/block): compute read span for the tap's head(s);            │
 │        ab_fetch(bank, start, n) burst SDRAM→CCM float window  (n≈40 TIME, ≤136 PITCH)│
 │                                                                                    │
 │ 5. ∀s ∀t  READ (from CCM window):                                                  │
 │        TIME : tap = xfade_read(win, cur[t](+wow_offset), Hermite)  (snap via xfade)│
 │        PITCH: tap = ptap_read(win, ρ_t, Hermite, aa_tier)   (dual-head, eq-power)  │
 │        during a 10 ms mode switch: compute both, equal-power mode_mix ramp         │
 │                                                                                    │
 │ 6. ∀s ∀t  chan[t] = tap · gain[t] · phase_sel[t] · auto_gain   (mixer, per-tap)   │
 │        chan[t] = tap_tone[t](chan[t])           ← per-tap analog HF roll-off       │
 │        env followers(in, Σchan) → AUTO CONTROL auto_gain (slow)                    │
 │                                                                                    │
 │ 7. ∀s  TX[s][t] = float→int24 clamp   (fault-containment rail; never wraps)        │
 │ 8.     pulse events (cycle/loop-point) → flags → SysTick/TIM pulse outs            │
 └──────────────────────────────────────────────────────────────────────────────────┘
   DAC slots 0..7 ◀── SAI1_B Slave-TX (sync A) ── DMA2 circular TX[2][BLK*8] (SRAM1)
   8 taps → 8 DAC outs → 8 panel jacks + analog op-amp sum ("mixed" jacks)
```

Three quantization/level decision points (everything else is float): **(a)** SDRAM write (width + optional vintage crush + dither), **(b)** feedback write-back (the only place quantization *accumulates* — sat+dither here), **(c)** DAC clamp (`audio_f_to_out`, the fault rail — keep it).

---

### 3. Module / file plan for `firmware/src/`

Directory reorg into `dsp/` (host-tested, hardware-free), `bsp/` (StdPeriph, bench-gated constants quarantined), `app/` (glue). All existing 7 suites must keep passing unmodified.

#### 3.1 DSP core (`src/dsp/`)

| Module | Purpose | Key interface | Host test | Status |
|---|---|---|---|---|
| `delay_line.{h,c}` | fractional delay + interp kernels + wrap/loop math | + `dl_read_windowed(win,base,frac,interp)`, + `dl_read_aa(win,delay,interp,aa_tier)`; existing `dl_read*`/`dl_vintage_quantize` kept | `test_delay_line`, `test_interp_quality` (+swept-read THD+N per kernel) | **extend** |
| `taps.{h,c}` | 8-tap model, phase 0..160, per-tap slew | unchanged; PITCH maps `cur[t]` → ptap window center | `test_taps` | exists |
| `time_control.{h,c}` | slewed exp multiplier (no PLL/hysteresis) | unchanged; + ρ-map for PITCH (`tc_update` slews ρ, not delay) | `test_taps`/new | extend |
| `transport.{h,c}` | WRITE/RECIRC/loop + window capture | unchanged | (covered by engine) | exists |
| `mixer.{h,c}` | per-tap gain·phase, 8 chan + analog-sum, master, auto | replace `master=1/8` placeholder w/ calibrated law; mute = explicit flag | new `test_mixer` | **extend** |
| `envelope.{h,c}` | one-pole asymmetric follower (AUTO CONTROL) | unchanged primitive; real scaling **[BENCH]** | `test_envelope` | exists |
| `crossfade.{h,c}` | dual-head **event** xfade — TIME `glide=0` snaps only | + equal-power fade curve (poly sin/cos); API unchanged | `test_crossfade` (+eq-power flatness) | **extend** |
| `pitch_tap.{h,c}` | **continuous dual-head windowed pitch read** (H910); the pitch-static fix | `ptap_init/set_window/set_ratio/update_block/read/read_loop/current_delay` (§6) | `test_pitch_tap` (§6 criteria) | **new** |
| `audio_buffer.{h,c}` | int16/int32 banked SDRAM layer; boot-time layout | `ab_init/ab_put/ab_get/ab_fetch(burst→CCM float)`; sat+TPDF inside int16 `ab_put` | `test_audio_buffer` (int16 round-trip ≤1 LSB w/ dither) | **new** |
| `tone.{h,c}` | one-pole voice LP (fb_tone + tap_tone[8]) | `tone_init/set_fc/process` (inline ~4 cyc) | `test_tone` (per-pass decay vs analytic `a`) | **new** |
| `sat.{h,c}` | soft saturator (rational tanh) | `sat_init(drive)→makeup`, `sat_process` (~22 cyc, 1 VDIV) | `test_sat` (unity small-signal, monotonic) | **new** |
| `wow.{h,c}` | shared wow/flutter modulator (offset in samples) | `wow_init/set_depth/tick_block/offset` | `test_wow` (peak cents vs `2πfA/fs`) | **new** |
| `dither.h` | xorshift PRNG + TPDF | `xs32`, `tpdf` (inline) | covered by buffer/vintage tests | **new** |
| `engine.{h,c}` | compose everything; block loop | reshape to `engine_process_block()`; add mode/pitch/tone/sat/wow/xf/ptap/bank state (§5.1) | `test_engine` (+mode-switch continuity) | **rework** |
| `audio_io.{h,c}` | CS42888 TDM int24↔float, 8 taps→8 slots | per-block call; unchanged codec math | `test_audio_io` | exists |

*Dropped from earlier lenses:* `src2x.{h,c}` (half-band 2:1 for a 48 kHz vintage rate) — **not needed**: int16 single-bank @96 kHz already gives 43.7 s (§5), so we hit the 40 s spec without ever moving the clock. Vintage "low-rate grit" is reproduced by optional in-buffer ZOH decimation-emulation (character only, real clock fixed). Revisit only if bench A/B demands a true reduced-rate voice. Similarly `pitch_head.{h,c}` (event-scheduled wrap-guard) is **superseded** by `pitch_tap` (§6 rationale).

#### 3.2 Settings / cal / persistence (`src/app/`, host-tested)

| Module | Purpose | Key interface | Host test | Status |
|---|---|---|---|---|
| `panel_input.{h,c}` | named logical panel inputs over 595/GPIO/SPI2-ADC/4051; debounced `panel_snapshot_t` | `panel_scan_tick(out)` @1 kHz; `panel_sw()`; one `panel_map[]` table (2 rows `[BENCH]`) | via `ui_mode`/`calib` scripted snapshots | **new** |
| `led.{h,c}` | 5-LED pattern engine (bargraph/blink/fill/busy) | `led_pattern_t`; pin table `[BENCH]` | `test_led` | **new** |
| `store_hal.h` + `store_hal_f429.c` | flash backend iface; target StdPeriph FLASH_*, host RAM mock w/ fault injection | `erase_bank/program/map` | mock in `test_flash_store` | **new** |
| `flash_store.{h,c}` | dual-sector (6+7), wear-leveled append-log, latest-`seq` wins, torn-write safe | `flash_store_init/load/save/needs_migration` | `test_flash_store` | **new** |
| `storage.h` | frozen record fmts + `_Static_assert` + CRC16/CCITT | `store_hdr_t{magic,rec_id,version,len,seq,crc16,hdr_crc}` | `test_storage` | **new** |
| `pinning.{h,c}` | per-param control pinning (stored until knob sweeps through) | `pin_engage/pin_apply` | `test_pinning` | **new** |
| `calib.{h,c}` | cal capture + normalization (raw→[0,1]/volts) | `calib_knob01/calib_cv_volts`; P1/P2 flows | `test_calib` | **new** |
| `settings.{h,c}` | runtime `dsp_params_t`/settings: defaults, load/apply/save, dirty | `settings_load/apply/save` | `test_settings` | **new** |
| `ui_mode.{h,c}` | mode state machine: boot chord, NORMAL/SETTINGS, pages, chord+knob editor | `ui_init/ui_tick(snapshot,out)` (pure) | `test_ui_mode` | **new** |
| `params.{h,c}` | double-buffered SysTick→ISR snapshot publish | `params_publish/params_snapshot` | `test_params` | **new** |
| `panel.{h,c}` | decode `panel_switch_bits`/DIP/trimmer → engine params (bank/phase/mute/phase-inv/cycle) | `panel_decode()` | `test_panel` | **new** |
| `main.c` | init order, boot-chord check, superloop | — | — | extend |

#### 3.3 StdPeriph BSP (`src/bsp/`, MARF house style, reuse MARF `Libraries/`)

`board_pins.h` (all bench-gated constants in ONE file) · `clock.c` · `gpio.c` · `sdram_fmc.c` · `sai_dma.c` · `codec_cs42888.{h,c}` · `i2c1.c` · `spi2_adc.c` · `panel_scan.c` (595/4051) · `pulse_tim.c` · `flash_store_f429` backend. Config table in §7.4; all `[BENCH]` pins isolated here.

---

### 4. Real-time budget verdict

Budget = 168 MHz / 96 kHz = **1750 cycles/sample** (56,000/block). FMC SDCLK = HCLK/2 = 84 MHz, no cache → every direct SDRAM access stalls. Reconciled numbers (the three lenses' estimates converge once prefetch is applied):

| | Direct per-sample SDRAM | **Block-prefetch into CCM** |
|---|---:|---:|
| TIME mode (single head, analog chain on) | ~955 (55 %) | **~585 (33 %)** |
| PITCH worst case (all 8 taps dual-head, Tier-0) | ~1595 (91 %) | **~860 (49 %)** |
| + Tier-1 AA on all taps | — | +~200 → ~60 % |
| + HQ 6p5o interp (opt, both heads) | — | +~640 → **ship OFF** |

**Verdict: it fits, with comfortable headroom — provided block-prefetch into CCM is treated as a requirement, not an optimization.** Direct per-sample reads are viable for TIME but marginal (91 %) in PITCH; prefetch removes the risk.

Risks → mitigations (in order):
1. **SDRAM random-access latency** → block prefetch: per tap/block copy `[floor(min)−1 .. ceil(max)+2]` as one sequential FMC burst into CCM float scratch (worst case 8 taps × 2 heads × 136 × 4 B ≈ 8.5 KB, fits 64 KB CCM). Bandwidth ≈ 7 MB/s of ~168 MB/s peak (refresh ~1 %) — non-issues.
2. **Prefetch estimate unverified** → first bring-up task is a `make cycles` DWT_CYCCNT profile of `engine_process_block` and `ptap_read`×8 against this table (validate FMC burst behavior).
3. **CCM/DMA constraint** → SAI RX/TX ping-pong buffers MUST live in SRAM1 (DMA can't reach CCM); only CPU-filled scratch + hot engine state + ISR stack go in CCM.
4. **Mode-transition double cost** → bounded 10 ms, cannot coincide with itself; ignore.
5. **Overrun fallback lever** → drop PITCH interp to linear during crossfades only (−0.5 dB HF, inaudible; saves ~200 cy), and skip dual-head for muted taps (mixer gain ≈ 0). Concurrency capped at 8 fades by construction.
6. **FPU discipline** → single-precision only; VFMA in kernels; **no VRINT on this FPU** — `dl_read_at` must keep wrapping `r` into `[0,len)` before the int cast so VCVT truncate = floor (document the invariant in code).

---

### 5. Buffer/fidelity, interpolation, and the "slightly analog" chain

#### 5.1 Buffer/fidelity decision

**float32 processing; int16/int32 storage; fixed 96 kHz; layout latched at boot.** float32 storage stays rejected (same 4 B as int32, less headroom-per-bit for bounded audio). The live 2-bit resolution switch (GPIOD11/12, stock behavior) selects storage width/voice at boot:

| Switch (stock) | Storage | Scaling | 1 bank / 2 banks | Character |
|---|---|---|---|---|
| "20-bit" → **HI-FI** | int32 | Q23 (`x·2²³`) | 21.8 s / 10.9 s ×2 | +48 dB buffer headroom (8 guard bits); floor below codec; no dither needed |
| "16-bit" → **STANDARD** | int16 | Q15 (`x·2¹⁵`) | 43.7 s / 21.8 s ×2 | ≥ ~93 dB effective; workhorse; storage-boundary TPDF on |
| "12-bit" → **VINTAGE** | int16 + 12-bit crush | Q15 | same as STANDARD | deliberate lo-fi; bit-crush dither **default OFF** (parity with stock crunch) |

Reconciled decisions (resolving the two conflicting lenses):
- **No engine-rate reduction / no `src2x`.** 40 s spec is met by int16 single-bank @96 kHz (43.7 s). Dropping the clock was the stock's *workaround*, not a feature to reproduce faithfully; its coloration is optional ZOH crush.
- **bank_B in hi-fi [DELTA + decision]:** memory is split dynamically. `panel_switch_bits` bit6 enables bank_B → two banks (int32: 10.9 s each; int16: 21.8 s each). Disabled → one full bank (21.8 s / 43.7 s). This resolves "int32 can't hold two full banks." Document: hi-fi + bank_B = ~10.9 s/bank; use STANDARD for long loops.
- **Mid-run resolution flip [DELTA]:** layout is boot-latched; a mid-run change triggers a graceful buffer reinit (mute-fade + fast clear). Documented UX change from stock's live re-read.
- **Two dithers, different levels:** (1) int16 storage-boundary TPDF (about the Q15 LSB, inaudible, default ON in STANDARD/HI-fi-n/a) vs (2) vintage 12-bit-crush dither (audible texture, **default OFF** to match stock, opt-in fidelity win). Feature-parity wins the default.

#### 5.2 Interpolation decision

**4-pt Hermite (Catmull-Rom) is the default everywhere** (stateless → safe under modulation; measured ~2.4× lower RMS error than linear @ ½ Nyquist). Options: **Linear** (cheap/vintage voice, kept in enum), **6-pt 5th-order "optimal" (Niemitalo)** as a compile+settings HQ A/B toggle (+~40 cy/tap-head, ship OFF), **1st-order Thiran all-pass** as a flanger-focused toggle (flat magnitude, but stateful → smears fast modulation → default OFF). **No oversampled read, no global pre-read band-limit**: at 96 kHz (~2.4× oversampled vs 20 kHz audibility) with ρ ∈ [0.25, 4.0], Hermite + the tiered AA (§6.4) is transparent; the feedback LP is the backstop for recirculated image build-up.

#### 5.3 The "slightly analog" chain (opt-in; default neutral)

**Default ships bit-honest (parity first, flavor opt-in — punch-list #16).** Character = a curated, subtle defect set, each block toggleable, params persisted, editable via chord+knob. Placement (per §2): **fb_tone** in the recirc write-back (cumulative per-repeat darkening), **tap_tone[t]** per DAC channel (one-time output voice — the "output filter" the hold-chord knob sets, per-channel because the mix is analog), **sat** before each storage quantize (rail in int16), **wow** as one shared additive offset on every tap delay ("one transport").

One-pole LP `y += a·(x−y)`, `a = 1 − exp(−2π·fc/fs)` @96 kHz:

| fc | a | | fc | a |
|---|---|---|---|---|
| 4 k | 0.2304 | | **7 k (fb_tone dflt)** | **0.3675** |
| 6 k | 0.3248 | | 10 k | 0.4803 |
| | | | **12 k (tap_tone dflt)** | **0.5441** |

fb_tone range 2–16 kHz (log knob); tap_tone 4 kHz–bypass. **Soft sat** (rational tanh, ~0.5 % accurate over ±3):
`y = makeup · u(27+u²)/(27+9u²)`, `u = clamp(x·drive, ±3)`, `makeup = 1/tanh_approx(drive)` at set-time (unity small-signal). Defaults: drive 1.0 = bypass (hi-fi) / rail (int16); analog 1.25; vintage 1.5. **Wow/flutter** (one shared modulator, output = samples):

| | Rate | Depth (analog dflt) | Delay depth A |
|---|---|---|---|
| Wow | 0.6 Hz | ±0.05 % (±0.87 ¢) | 12.7 smp |
| Flutter | 8 Hz × 1 Hz-smoothed random AM | ±0.02 % (±0.35 ¢) | 0.38 smp |

Depth knob 0–5× (0 = off = hi-fi default; analog 1.0×; vintage 1.5×). Below the ~2 ¢ JND → reads as "air," not detune. Tick per-block @2 kHz + linear ramp ≈ 2 cy amortized.

**Convenience "voice" preset** (settings-mode selection, persisted) sets the tone/sat/wow bundle: **NEUTRAL (default)** all bypass · **ANALOG** fb 7 k / tap 12 k / drive 1.25 / wow 1.0× · **VINTAGE-TONE** fb 6 k / tap 8 k / drive 1.5 / wow 1.5×. Note: this tone-voice axis is **orthogonal** to the storage-fidelity axis (§5.1, resolution switch) — a deliberate un-conflation of the fidelity lens's single HI-FI/ANALOG/VINTAGE knob, so a user can run int32 storage with analog tone, or int16 storage neutral, etc.

---

### 6. Pitch-mode crossfade design (winning approach)

**Winner: continuous dual-head *windowed* read (`pitch_tap.c`), not event-scheduled xfade retriggering.** Rationale: in pitch mode both heads drift continuously at `dD/dn = 1−ρ`; an event-triggered `xfade_t` freezes its outgoing head mid-fade (→ warble every wrap) and needs a fragile trigger state machine (fade-still-active-at-next-wrap, ρ sign flips, single-head gaps). The windowed reader is the limiting case that removes all of it.

**Geometry.** Per tap, one sawtooth phase φ∈[0,1) drives two heads a half-cycle apart:
```
φ₁ = frac(φ + 0.5);  D_k = D_MIN + φ_k·S;  r_k = (wpos − D_k) mod L
φ(n+1) = frac(φ(n) + (1−ρ)/S)        (read advances at ρ ✓)
y = g(φ₀)·x(r₀) + g(φ₁)·x(r₁)
```
Each head's gain is **zero exactly at its saw-wrap** (the instant D_k teleports by S); heads are ½-cycle apart so when one is silent the other is at peak. `D_k` is confined to `[D_MIN, D_MIN+S]` with `S ≤ L−16`, so the **write-head boundary is never crossed** — the discontinuity is geometrically excluded, not masked.

**Guard zone:** `D_MIN = 8`, `D_MAX = L−8`, usable `S_MAX = L−16` (covers Hermite's 4-tap reach + drift + AA pre-average's ≤5 fetches).

**Window = equal-power `g(φ) = sin(πφ)`** (default). Heads read program S/2 apart ≈ uncorrelated for real audio → constant loudness (no periodic tremolo); its derivative kink sits at the zero-gain point (no splatter). +3 dB bump only on strongly periodic material at S/2 ≈ n·period — the least-objectionable failure, what every classic unit ships. **257-entry `sin(πφ)` LUT in flash + lerp** (1028 B, <−80 dB, ~8 cy). Hann `sin²` available as compile option; linear as A/B control. Also back-port equal-power to `crossfade.c` TIME snaps (heads there are decorrelated too).

**Params.** `S` = window/grain size: clone default `min(cycle_len−16, S_MAX)` (full-cycle sweep = stock's long-loop pitch character, minus the click); settings-mode `pitch_span` 2048…cycle_len for H910 micro-pitch. Fade length = S/2 per handoff. **ρ range [0.25, 4.0]** (±2 oct, matches stock octave machinery — chosen over the fidelity lens's ±1 oct because PITCH is the pitch specialist's call; exact taper/center-detent **[BENCH]**). `ρ→1` degeneracy: when `|1−ρ|<1e−4`, relax φ→0.5 at k≈0.001/smp (~100 ms) → collapses to a single clean read at `D_MIN+S/2`. **Per-tap state** (stock control law spreads ρ by `phase[i]/160`): `PITCH_LAW_STOCK_SCALED` (`ρ_i = 1+(phase[i]/160)(ρ_panel−1)`, clone default) vs `PITCH_LAW_UNIFORM` (settings option, enables one shared-φ fast path). `ptap_t` ≈ 28 B ×8 = 224 B.

**Tiered anti-aliasing** (per tap, selected per block from ρ, ~5 % hysteresis, switch only at block edges):
```
Tier 0  ρ∈[0.7,2.0]   Hermite only                                   (baseline)
Tier 1  ρ∈(2.0,4.0]   +boxcar pre-average s'=½(sᵢ+sᵢ₊₁) then Hermite  (+1 fetch,+4 add/head; ×2 for ρ>3)
Tier 2  ρ∈[0.25,0.7)  Hermite + tracking output one-pole fc=clamp(0.45·ρ·fs,4k,43k)
```
Pitch-UP folding is the only true alias source and is negligible ≤ +1 oct at 96 kHz; Tier 1 attenuates the 19–43 kHz band that folds at ρ→4. Escape hatch `PTAP_SINC8` (compile-time) if bench listening at ρ=4 offends.

**Interface (`pitch_tap.h`):** `ptap_init(p,dmin,span,start_delay)` · `ptap_set_window` · `ptap_set_ratio` · `ptap_update_block` (AA tier + lp_g) · `ptap_read(p,dl_or_win,interp)` · `ptap_read_loop(...,loop_start,loop_end,...)` · `ptap_current_delay` (dominant head, for mode handoff). RECIRC works unchanged with `L_eff = loop span`; the loop splice seam is the loop's own character (stock behaves identically) — document, don't fix.

**Mode switching (glitch-free):** no state morph. On a TIME↔PITCH edge, seed the incoming mode coherently (`PITCH: φ=0.5`, head0 at current slewed delay), then compute **both** reads and equal-power-mix over `MODE_XFADE_SAMPLES = 960` (10 ms), then drop the outgoing reads. Same ramp covers SHORT/FULL and `pitch_span` changes.

**Host-test criterion (`test_pitch_tap.c`, the headline assert):** sine `w=0.05π` into an 8192-sample line, ρ=1.26, S=2048 (~25 wraps over 200 k samples): assert `max|y[n]−y[n−1]| < 3·ρ·w·A` across the **entire** run including wraps; the **control case** (naive single sweeping head) must *fail* by >10× (proves the test catches the bench-2 defect). Plus: write-head exclusion (instrumented), pitch accuracy (Goertzel ±1 % at ρ∈{0.5,0.84,1.19,2.0}), aliasing (<−35 dBc Tier-0; Tier-1 ≥6 dB better at ρ=3), equal-power loudness ripple <±1 dB, ρ→1 convergence to `dl_read(D_MIN+S/2)`, engine-level mode-flip continuity, RECIRC (bound holds except ±2 smp of loop splice).

---

### 7. Settings / cal mode

#### 7.1 Boot chord
Power-up gesture: **PULSE IN (SW14) held LEFT AND ARM PULSE IN (SW16) held LEFT** — both are spring-return momentaries (BOM), so this is unambiguous and impossible to latch accidentally. Read **before** the audio ISR starts, in parallel with SDRAM init (adds ~0 ms to normal boot). Timing (`ui_mode.h`, host-tested): `BOOT_SETTLE_MS 30` (discard) → `BOOT_DETECT_MS 100` (both-LEFT the entire window or → NORMAL) → `BOOT_CONFIRM_MS 600` (LEDs fill L→R; early release → NORMAL) → enter (5 LEDs flash 3×) → `MODE_SETTINGS`. Optional runtime re-entry (`UI_RUNTIME_REENTRY`, default ON): same chord held ≥1 s in NORMAL.

#### 7.2 State machine (`ui_mode`, pure `ui_tick(snapshot,out)`)
`ST_BOOT_CHECK → (no chord) ST_NORMAL ↔ (chord) ST_SETTINGS`. Pages selected by existing **latching** selectors (no new controls):

| CAL/PRESET (B10) | A/B/C | Page |
|---|---|---|
| cal | A | **P1** control min/max cal |
| cal | B | **P2** CV two-point cal |
| pre-set | A | **P3** analog tone (audible while edited) |
| pre-set | B | **P4** system: fidelity override, interp, glide, pitch_span, factory reset |
| — | C | reserved |

In-mode: SW14-left tap = confirm/action; SW16-left hold ≥1 s = exit (auto-save dirty settings; cal saved only if confirmed). Audio: P3/P4 run the live engine; P1/P2 freeze engine params at entry snapshot; `audio_mute` only during sector erase. `ui_out_t{leds, request_save_settings, request_save_cal, audio_mute, suppress_transport}` — superloop performs writes.

#### 7.3 Chord + knob modal editor (relative-with-clamp)
Hold a momentary LEFT; a mapped knob is *stolen* to edit a setting. `chord_map[]` (one table):

| Held | Knob | Edits |
|---|---|---|
| PULSE IN-L (SW14) | TIME pot | `tone_cutoff_hz` (tap_tone/fb_tone fc) |
| PULSE IN-L | POT6 (lin) | `sat_drive` |
| PULSE IN-L | POT7 (lin) | `tone_damping` (fb roll-off) |
| ARM PULSE-L (SW16) | TIME pot | `glide` |
| both-L ≥1 s | — | enter SETTINGS |

Semantics: latch knob raw on press; knob goes live only after moving >`EDIT_DEADBAND ≈2 %`; edit is **relative** (`param += Δknob·span`, clamped) → never jumps. On release the knob's **normal** parameter is **pinned** (§7.5) until the physical control sweeps back through its pre-chord value. Transport conflict rule: the momentary's normal action fires **on release if hold <300 ms and no mapped knob moved** (tap = original manual write/recirc/pulse); else chord/edit + `suppress_transport`. Flagged for bench feel-validation; fallback = chord entry boot-only. Save: chord-release starts a 500 ms coalescing timer → one `request_save_settings` append.

#### 7.4 Cal flows
- **P1 control min/max:** working `min=0xFFFF/max=0`; user sweeps each of 9 sliders + 7 pots to extremes (median-of-3 fold); LED bargraph = channels with span ≥ `CAL_MIN_SPAN ≈60 %`; SW14 commits (insufficient-span channels keep prior/default; committed get ±0.5 % inset guard so extremes reach 0.0/1.0). Applied as `norm = clamp((raw−min)/(max−min))` (+invert). Fixes nothing that regresses; uncalibrated → identity (unit behaves like today).
- **P2 CV two-point** (fixes the Time-CV narrow-range bug): LED1 blink → apply 0 V, SW14 captures 256-avg; LED2 blink → apply +5 V, SW14 captures; compute gain/offset with monotonic sanity check (reject → keep prior). `volts = (raw−offset)·gain`.
- **Fidelity apply-at-boot:** load settings → read resolution switch → resolve (`FOLLOW_SWITCH` | `FORCE_*`) → lay out SDRAM once. P4 change lights "reboot to apply" (LED1+2 alternate).

#### 7.5 Persistence records (`storage.h`, MARF pattern, frozen w/ `_Static_assert`)
Flash: sectors 0–5 = code (256 K; app ≪ that), **sector 6 = store bank A (128 K)**, **sector 7 = store bank B (128 K)**. Header (12 B, word-aligned): `{uint16 magic=0x5238 (written LAST=commit), uint8 rec_id, uint8 version, uint16 len, uint16 seq, uint16 crc16 (CCITT 0x1021/0xFFFF over payload), uint16 hdr_crc}`. Records **appended**; load takes highest-`seq` valid per `rec_id`; payload→header→magic order = torn-write safe (power loss always recoverable); bank-full → copy latest of each rec_id to other bank + erase. Payloads: `cal_payload_v1` (9+7 `{u16 min,max}` + 4 `{f32 gain,off}` CV + valid flags ≈104 B), `settings_payload_v1` (§ below, ≈24 B). Version bump = defaults, no migration shims in v1. Endurance ≈30 M saves — non-issue. **Control pinning** (`pin_t{target,last_live,active}`, release when live crosses target or within `PIN_EPS=0.01`) used at boot recall, chord-release handback, and cal-page exit.

Settings payload: `tone_cutoff_hz` (1 k–20 k log, ≥20 k=bypass; dflt bypass), `tone_damping` (0–1, dflt 0), `sat_drive` (0–1, dflt 0), `glide` (≥0, **0 = crossfade-snap**, dflt small>0), `fidelity_mode` (FOLLOW_SWITCH|FORCE_INT16|FORCE_INT32, dflt FOLLOW), `interp` (linear|hermite, dflt hermite), `aa_on` (dflt on), `voice` (NEUTRAL|ANALOG|VINTAGE-TONE, dflt NEUTRAL), `pitch_span`, `pitch_law`, `wow_depth`.

---

### 8. Feature-preservation punch-list (MUST NOT REGRESS)

1. **Tap→output-jack (TDM slot) order** — live-test before release (wrong order silently scrambles panel tap 1..8). [BENCH]
2. **36-trimmer role** (tap positions vs levels vs both-per-bank) — resolve `hardware.md` self-contradiction on the bench *before* writing `panel.c`. [BENCH]
3. **AUTO CONTROL musical behavior** — highest silent-regression risk; only a placeholder additive term exists. Characterize `0x20002088` live (feed signals, watch), then implement for real. [BENCH]
4. **bank_B enable + role, incl. hi-fi int32** — resolved to dynamic memory split (§5.1); confirm stock enable condition live. [BENCH]
5. **DIP 10 ms tap-times vs 0..160 phase mode** — pin `panel_switch_bits` bit3 selector; reconcile the two tap-time sources. [BENCH]
6. **SHORT/FULL printed panel times** — calibrate cycle lengths in samples @96 kHz so the 4:1 ratio + absolute legend match.
7. **cal./pre-set live setup mode** — preserve its stock audible effects (short buffer, ×2 mult, immediate/raw tap tracking); keep distinct from the NEW boot-chord stored-cal mode (do not conflate).
8. **PITCH mode ships only when click-free AND static-free** — `pitch_tap` integrated into `engine.c` (today engine reads `taps.cur[]` directly; the patched stock build is *worse* than stock here).
9. **Octave switch bits 9/10** still audibly select ×1/×2/×4 after PLL removal (remap to multiplier/base-length scaling). [DELTA]
10. **Vintage 12-bit texture** — bit-crush dither default OFF to match stock crunch (opt-in fidelity win).
11. **Mid-run resolution flip** — graceful mute-fade + fast buffer clear; document the boot-latched-layout change from stock's live re-read. [DELTA]
12. **Output loudness / slider law** — replace `master=1/8` placeholder with measured gain staging. [BENCH]
13. **Pulse output** — match stock 5 ms/5 V timing at minimum; the 14.5 V improvement is likely hardware-bound (don't promise); don't regress timing semantics. [BENCH]
14. **Manual transport feel** — fix the "stuck ~3 s" bug, but measure stock auto-cycle timing first. [BENCH]
15. **Mute DIPs preserve slider settings** — mute = explicit flag, not a gain overwrite.
16. **Analog-tone defaults NEUTRAL** — parity first, flavor opt-in (default voice bypasses all character blocks).
17. **Recovery path** — keep `Compiled FW/B288-REV1.0.hex` + `288r-unit-dump.bin` as golden restores; BOOT0 ROM DFU documented as unbrick. USB update path [DELTA — dropped unless owner requests re-impl; SWD + ROM DFU cover recovery].

New-feature interaction guards: boot chord read once before runtime roles attach (trigger on *switch position*, not jack voltage); chord-held knob must not also drive its normal param; on release, pin until swept through.

---

### 9. Prioritized build order

Everything through step ~7 is **host-testable now** (no hardware). Steps 8+ are **bench-gated**. Keep `make test` green at every commit; each new module lands with its suite.

**Phase A — DSP foundation (host, no bench):**
1. `audio_buffer.{h,c}` + `dither.h` + `test_audio_buffer` — int16/int32 Q15/Q23 layout, `ab_get/ab_put/ab_fetch`, boot layout, storage-boundary TPDF. *This unblocks everything (delay_line reads through it).* Change `delay_line_t.buf` → windowed reads sourced from `ab_fetch` scratch; keep the float-buffer path as the host-test backend so the 7 existing suites pass unmodified.
2. `pitch_tap.{h,c}` + `test_pitch_tap` — the headline wrap-continuity assert + naive-head control case (proves the bench-2 fix). Equal-power sin LUT, tiered AA, ρ→1 relax.
3. `engine` rework → `engine_process_block()` (BLK=32), integrate `xfade` (TIME snap) + `ptap` (PITCH) + `mode_mix` 10 ms ramp; `test_engine` mode-flip continuity. Equal-power upgrade to `crossfade.c`.
4. `tone/sat/wow` + suites; wire per §2 (fb_tone in recirc, tap_tone per channel, write_sat, wow offset); default voice NEUTRAL.
5. `storage.h`/`flash_store`/`store_hal`(mock)/`pinning`/`calib`/`settings` + suites — the whole persistence + cal-math stack against RAM mocks with fault injection.
6. `panel_input`/`ui_mode`/`led`/`params`/`panel` + suites — boot chord, page routing, chord+knob relative editor, tap-vs-hold discrimination, all via scripted `panel_snapshot_t`.
7. `mixer` extend (real gain law scaffolding, explicit mute flag, AUTO CONTROL structure) + `test_mixer`.

**Phase B — bench-gated (needs the SWD/logic-analyzer session):**
8. `bsp/` StdPeriph bring-up (reuse MARF `Libraries/`): `clock` (§7.4 PLL), `sdram_fmc` (timings ready), `sai_dma`, `i2c1`+`codec_cs42888` (I²C addr + regs [BENCH]), `spi2_adc`, `panel_scan` (595/4051), `pulse_tim`, `store_hal_f429`. `make cycles` DWT profile validates the §4 budget on real FMC first.
9. Fill the ~2 `[BENCH]` rows in `panel_map[]`, `board_pins.h`, codec regs, TDM slot order; calibrate the punch-list constants (§8: tapers, cycle lengths, AUTO CONTROL, thresholds).
10. Flash + listen: verify PITCH static gone, TIME ringing gone; A/B analog voice; feel-validate the transport tap/hold rule.

**First 3–5 concrete steps for the implementer right now:**
1. Create `src/dsp/`, `src/bsp/`, `src/app/`; move the 12 existing DSP files into `src/dsp/`; update the Makefile include paths; confirm `make test` still green (pure refactor, one commit).
2. Write `dither.h` (xorshift + TPDF) and `audio_buffer.{h,c}` with a host malloc backend; add `test_audio_buffer` (int16 round-trip ≤1 LSB with dither, int32 exact, bank split math). Commit.
3. Add `dl_read_windowed`/`dl_read_aa` to `delay_line.{h,c}` (tier-0 ≡ existing `dl_read`); extend `test_interp_quality` with a swept-read THD+N harness per kernel. Commit.
4. Write `pitch_tap.{h,c}` + `test_pitch_tap` with the wrap-continuity headline assert **and** the failing naive-single-head control case. This is the proof-of-correctness for the module's whole reason to exist. Commit.
5. Rework `engine` to `engine_process_block()` and wire `ptap`/`xfade`/`mode_mix`; extend `test_engine` with a mid-stream TIME↔PITCH flip continuity assert. Commit.

Then proceed down Phase A (steps 4–7) while the bench session is scheduled; Phase B starts the moment hardware is on the bench, with `make cycles` as bring-up task #1.

---

Files to create/extend (absolute): new under `/Users/oren/Documents/GitHub/288r/firmware/src/dsp/` — `pitch_tap.{h,c}`, `audio_buffer.{h,c}`, `tone.{h,c}`, `sat.{h,c}`, `wow.{h,c}`, `dither.h`; extend `delay_line.{h,c}`, `crossfade.{h,c}`, `engine.{h,c}`, `mixer.{h,c}`. New under `/Users/oren/Documents/GitHub/288r/firmware/src/app/` — `panel_input`, `led`, `flash_store`, `store_hal`, `storage.h`, `pinning`, `calib`, `settings`, `ui_mode`, `params`, `panel`. New `/Users/oren/Documents/GitHub/288r/firmware/src/bsp/` StdPeriph layer. Matching suites under `/Users/oren/Documents/GitHub/288r/firmware/test/`.
