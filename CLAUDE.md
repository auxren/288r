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
  time_control, transport, mixer, envelope, engine, crossfade, audio_io) + `firmware/test/`
  (7 suites). `make test` green; `make engine` cross-compiles for the F429. engine exposes
  `engine_process_multi()` (8 per-tap DAC channels + mixed out); audio_io does the CS42888 4-in/8-out
  TDM block (int24↔float, clamped). Interp fidelity measured: Hermite ~2.4× better than linear @ ½ Nyq.
  Still to build (pre-bench, host-testable): audio_buffer (int16/int32 SDRAM layer), panel (switch/595/
  SPI-ADC decode), storage/settings/calib (MARF-style persistence + cal), pitch_tap (dual-head), tone/
  sat/wow (analog voice). Full plan in the design spec (see DESIGN.md + task output brbh5j78t).
- **Community firmware WORKS ON HARDWARE (bench session 3, 2026-07-16):** `make firmware` →
  `firmware/build/fw/b288-community.hex` runs a **working multitap delay + smooth delay-time modulation
  via the Time-CV** — the headline chorus/flanger fix, confirmed on the unit. Bare-metal BSP in
  `firmware/src/bsp/` (direct-register against vendored F429 CMSIS in `firmware/Libraries/CMSIS/`;
  `-nostdlib`, no newlib). **All the hard `[BENCH]` constants are now resolved** (see
  `re/notes/bench-session-3.md`): I²C1=PB8/9, codec 0x49 + TDM regs, SAI1 SD_A=PD6/SD_B=PF6, 24-bit
  right-justified, **SDRAM = FMC bank 2 (0xD0000000)**, audio in = RX slot 0, TIME-CV = SPI2 ADC ch0.
- **Panel/features built out (2026-07-17, host-tested, on `main`):** taper calibrated to the panel
  legend (linear 0.4×–1.6×, noon=1.0); **config DIP sw1 (×10 extend, clamped) + sw2 (11025 Hz bandwidth
  limit** = new `bwlimit.c` one-pole) wired as boot straps; **live 74HC165 scan** (`panel_ctl.c`) decodes
  A/B/C preset + octave ×1/×2/×4 and applies them smoothly (octave rescales base via `taps_set_base_delay`
  — fixed-rate, no glitch); **LED framework** (`led.c` + walking-1 discovery tool) over the 595, gated off;
  plus a `g_dbg_panel` SWD snapshot for labelling [BENCH] bits and a bench-runbook checklist.
- **More host-tested modules built out (2026-07-17, `main`, standalone — not yet wired):**
  **`audio_buffer.c`** = the int16/int32 SDRAM fidelity layer (vintage 2× capacity; I32 Hermite matches
  the float `delay_line` kernel to 3.7e-9 — DESIGN.md drop-in, wire = swap `delay_line_t` in `engine_t`);
  **transport momentaries** (`transport_update_trig`, edge-driven WRITE/RECIRC, gated in main);
  **`saturation.c`** = soft-clip analog voice (completes the vintage trio: bit-crush + `bwlimit` + sat);
  **`storage.c`** = MARF-style versioned/CRC records + control-pinning; **`calibration.c`** = a concrete
  cal record on it + the Time-CV range-stretch (narrow-range bug fix). `make test` = **15 suites on main**
  (persistence flash backend = F429 internal-flash emulation, [BENCH]).
- **Bench + release tooling (desk work, no hardware):** `re/scripts/saleae_decode.py` self-labels 165/SPI2
  captures (hardware ground truth for the [BENCH] maps; `--selftest` passes); `.github/workflows/ci.yml`
  now builds the flashable image + publishes tagged `.hex`/`.bin` releases; `docs/bench-runbook.md` has the
  panel/config/pitch checklist + `g_dbg_panel` SWD snapshot + Saleae capture recipes.
- **Pitch shifter → playable voice (branch `pitch-shift-engine`):** `pitch_shift.c` (crossfaded-tap,
  from `firmware/PITCH_SHIFT.md`) + `pitch_voice.c` (1.2 V/oct CV map, ratio slew) + `fast_math.c`
  (no-libm single-precision sinf/cosf/exp2f, so the freestanding image links). Global voice wired into
  main, **gated (`PITCH_VOICE_ENABLE=0`)**; enabled image verified to link. `make test` = 13 suites there.
- **Bench sessions 5–5c (2026-07-18, owner-in-the-loop): the PANEL IS ALIVE.** Complete 165 switch
  map owner-verified live (see `panel_ctl.c` header). LEDs cracked: PA0/1/7/8/11 are DSP-driven
  indicator/pulse outputs (input comparator @+0.5FS, envelope presence @0.25FS, write LED, end-of-
  cycle blips at loop wrap — all working). Transport (red write/recirc momentary) working. CYCLE 3-way
  scales the window live. **Savable presets working end-to-end**: hold-write-2s saves to the selected
  C/B/A slot (LED twinkle confirms), recall applies phases + pinned multiplier (catch-band pinning).
  Codec is actually a **CS42448** (ID 0x04). Rear DIPs: PB10=x10 extend (stock table exactly x10;
  pin confirmed by live IDR capture in session 6), PB11=rear sw2 (bandwidth). NOTE: preset
  persistence is still the RAM placeholder — lost at power-off until the internal-flash backend lands.
- **Bench session 6 + ultracode dig (2026-07-18, overnight): FEATURE-COMPLETE against the stock.**
  An 8-agent adversarially-verified decompile dig settled the full architecture (see the
  panel-scan.md CORRECTIONS addendum): boot-once matrix scan + 595 parked at 0x777777; the parked
  ADC3 channel = the c.v. ATTENUVERTER (proven live by an owner knob sweep) → stock control law
  `mult = knob + cv×att` ENABLED; stock pitch mode implemented on our crossfaded shifter (knob =
  pitch-down depth −1.07/−4.75 st, delay pinned, CV bipolar 1.2 V/oct through the same attenuverter
  — and the dig found a STOCK BUG: their path-B pitch sweep is dead code); store beg./end = bit-6
  latching policy (store-end → HOLD the window, RECIRC recalls it); pulse input jacks PG10/11/12
  (write/recirc/arm) wired; pitch branch formally merged. Preset flash persistence live (sector 3).
  A stray patched CV was the source of a whole evening of "worse" reports — the STABILITY rule now
  in force: proven core locked, inferences gated until wire-proven. `make test` = 21 suites.
- **Bench session 7 (2026-07-18, owner-in-the-loop): v1.0.1 TAGGED.** Modulation ZIPPER fixed (tap
  slew tau was 67 µs — snapped to control ticks from any source; now a 10 ms glide, chorus/flanger CV
  clean). Multiplier knob calibrated to the PANEL LEGEND (7-point owner-measured curve, 0.4–1.6 all
  marks read true; the pot taper is non-linear vs the print). **sens. knob bound** (owner sweep:
  analog attenuator feeding codec ADC slot 1; envelope vs fixed SENS_REF) → AUTO LED lights only
  while audio exceeds the sens threshold, auto-capture keys off the same comparison; PA0 = whole-
  chain CLIP LED (~¼ s on input rail or pre-limiter tap overrange; stock comparator behind
  `LED_INPUT_CLIP_MODE 0`). **Pitch mode overhauled:** all-8-tap crossfaded REPLACE (zero depth =
  clean dry; slider 0 = always-dry feed), knob = raw-travel pitch-down depth with an exact-unity
  snap, CV ratio bounded ±2 oct, ~15 ms glide, **AM fixed** (coherence-adaptive crossfade — tone
  ripple 0.33/0.03 dB), **exact int+frac reads** (the float path quantized deep SDRAM reads to
  ¼ sample), per-tap 0–9 ms decorrelation (inverted slider pairs comb, don't cancel). Slot→slider
  map owner-verified via the `g_dac_solo` SWD solo walk → **slider 5 = dead ANALOG path on this
  board** (slot 4 hot on the bus; firmware keeps identity mapping). Settings LOCKED: only the 4
  rear DIPs are read (front DIP matrix never — presets cover it). Soft-knee output limiter
  (transparent <0.75 FS; a 1.15× external feedback loop settles at 0.877 FS, zero flat-tops).
  `make test` = **26 suites**; **v1.0.1 tagged** (CI hex/bin + one-click flasher zip).
- **Still [BENCH]/open:** the "signal in" summing point (proven NOT a codec channel — it reaches the
  multiplier through the analog Time-CV net, scaled by the attenuverter; exact summing point
  unconfirmed); SENS_REF (0.02 FS) feel-calibration; the calibration routine (sliders/pots/36
  trimmers/CV — DESIGN.md spec, unimplemented). Debug
  scaffolding (`g_dbg_panel`, `g_dac_solo`, `sdram_memtest`) intentionally RETAINED in v1.0.1
  (SWD-only) — strip in a future release once the slider-5 repair is verified.
- **v1.1-dev: up-shift anti-aliasing (built + host-proven, NOT YET FLASHED — unit was powered off):**
  polyphase Kaiser-sinc band-limited read for ratio>1 (16 taps, 32 phases, 4 ratio bands; DSP-friend
  learning: reading faster than write = decimation, Hermite doesn't band-limit). Measured on host:
  **70.3 dB alias suppression vs the Hermite path** at +1 oct, passband −0.01 dB, cache purity 1.000.
  A 2-agent adversarial verify caught a REAL blocker pre-flash: 32 uncached SDRAM loads + flash-coef
  ART-thrash ≈ 2× the ISR idle budget (M4F has NO D-cache — SDRAM ≈10–18 cyc/load). Redesign: per-grain
  32-sample streaming cache (grain only moves forward; ~2×ratio loads/sample amortized), active band's
  coefficients CCM-published by the superloop (revoke-before-overwrite protocol), and DWT_CYCCNT ISR
  load telemetry (`g_dbg_panel.isr_pk`) + `g_dbg_ratio_force` so headroom is MEASURED before release.
  FLASH GATE: power the unit, flash, force ratio 4.0 over SWD, confirm isr_pk ≪ budget (28000cyc/block).
  Post-v1.0.1 landed on main (all owner-tested/soak-proven): AA polyphase up-shift filter
  (streaming-cache redesign after the verify panel caught the naive version's ISR blowout),
  pitch-mode overrun fix (skip discarded tap reads; 104%->58-70% ISR), TRANSPOSED MULTITAP
  (per-tap SDRAM echo ring — sliders = pitched echo pattern, stock semantics; delay now 19.1 s
  + 2.73 s voice ring), wet-sliver 4x thinner, PERIOD-ADAPTIVE splice search (background
  autocorr sizes the correlator on confident bass: 30 Hz purity 0.134->1.004 host; subharmonic
  disambiguation needed local refine — pure-sine tests missed it, live hardware caught it).
  Direct audio capture via Big Six ch7/8 (mic permission granted) = full autonomous bench loop.
- **v1.1.0 RELEASED (2026-07-18):** pitch line owner-approved; KARPLUS-STRONG STRING MODE (`ks.c`,
  8 SRAM strings on the tap outs, gesture-entered via next-sound hold, chord = tap phases, CV =
  direct 1.2 V/oct, knob = damping, breathing READY LED); envelope→time (sens-gated); clip-chain
  LED; panel-legend knob taper (owner point-cal 0.4–1.6); presets/cal SURVIVE FW UPDATES (moved to
  flash **sector 7 @0x08060000**, linker capped 384K so overlap = build error, auto-migration) —
  the MARF-style hard requirement. Release-notes convention LOCKED: three sections (New/Fixed/
  Known & open), `docs/release-notes-<tag>.md` = CI release body; commit BEFORE tag.
- **v1.2.0 RELEASED (2026-07-21) — the field-report release** (driven by v1.1.1-rc1/rc2 testers):
  **rear DIPs are MATRIX-CONNECTED** (electrically floating until the 595 parks → every pre-1.2
  boot-time strap read was a coin flip); all 3 straps now latch post-park, deterministic (#12).
  **DIP1 = ×4 "+2 octaves"** (owner-picked over ×10: composes in octaves, no pitch/looper clamp).
  **Panel ticks are block-clock driven** (pass-counted ticks stretched to ~0.4 s under pitch-mode
  load → momentaries/looper dead in pitch; now ~5 ms in every mode, verified 0.4 s→3 ms) (#3).
  Pulse jacks edge-latched (~3 kHz) (#4); cal unpins on entry (#6); pitch CCW = designed dry
  bypass (#11); #8 long-mode clicks = reporter-unit marginal SDRAM (5 h/857-window soak clean on
  reference; despike on offer). **Flasher field-hardened** (staged path-free exec, install offers
  + pip fallback, vendored pystlink for pre-V2J24 dongles — template MUST end `reset`; canonical
  copy in `~/Documents/GitHub/claude_trix/tools/easel-weasel`, sync marf too). Codec init is
  concert-grade (bus-recover + verify-retry ×5 + all-LED boot alarm; 15/15 reboot torture).
  ISR telemetry: TIME 66%, pitch 58–83%, KS ~50%. `make test` = **30 suites**. Manual (docs/01–09)
  updated through v1.2. #3 closed after owner's physical-flick confirmation.
- **v1.2.1-rc1 PRE-RELEASE (2026-07-22, tester-driven — AUTO CONTROL reports #13/#14/#15):**
  red-switch MOVEMENT now resets the looper state machine (#13 — LP state silently survived
  toggles, so the stock "all sounds and back" reset gesture re-entered LP_LOOP and auto-trigger
  could never re-fire; entering a looper position sits READY *armed* = present signal captures
  immediately). NOT yet hardware-verified (bench offline) — field-test RC; graduate to v1.2.1
  on reporter confirmation. #14 closed by-design+docs (READY LED dark for exactly one write
  pass = loop length; ch.6 got a capture-LED walkthrough + red-switch 3-position table).
  #15 open as design discussion: env→time is additive-upward so it clamps dead at multiplier
  full CW — questions posted (bipolar depth? disable strap? does reporter's analog signal-in
  jack work on stock?). Red-switch decode reference: automode 1=all sounds(bit7),
  2=next-sound momentary side(bit8), 0=center/arm-pulse-in. Open: #5 (deeper presets —
  interleaved 16-tap idea), #9 (looper varispeed — comparison questions now posted in-thread),
  #10 (auto re-arm; toggle-reset is the manual workaround), #13/#16 (rc verification),
  #15 (design), slider-5 board repair.
- **#9 VARISPEED CONFIRMED on the 288v (2026-07-23, video forensics):** downloaded the batchas
  288v video "[04]" (RECLee's #9 evidence; yt-dlp, owner-approved) and pitch-tracked the audio
  (autocorr, 4096/2048 @48k): last ~8 s = continuous +8→+18 st ramp on a recirculating loop
  WITH the hand visibly on the 3rd multiplier knob (recirc LED lit). Mechanism: stock retunes
  the PLL (whole sample clock moves) → a recirc loop repitches for free; our fixed-96k engine
  (the smooth-modulation fix) needs explicit varispeed. Proposed in #9: looper positions =
  tape-motor resampled loop playback (pitch-mode fractional-read tech; bench ISR check before
  ship); all-sounds keeps constant-pitch respacing. Bonus finding: at 1:50 the hand rides the
  sens. knob to gate auto-capture (independently validates sens-threshold + rc3 auto re-arm).
  Implement for next RC unless community objects. Video/frames/tracker: job tmp dir
  (pitchtrack.py; frames/f147 = the money shot).
- **v1.2.1-rc4 PRE-RELEASE (2026-07-23): VARISPEED SHIPPED (#9).** engine.c recirc branch:
  head advances lp_rate = lp_mult_ref/mult per sample (clamp 0.25..4), lp_phase fractional
  part folded into the tap-read d_int/d_frac (continuous, not stair-stepped); lp_mult_ref
  captured in all 4 engine_recirc_* entries. Gated: looper positions only, !pitch_mode,
  VARISPEED_ENABLE in board.h. test_varispeed.c = 31st suite (freq x2 @ half mult, unity at
  capture, off = no repitch, clamp rails; gotchas: tc taper is LINEAR raw=(m-.4)/1.2, and
  record MORE than the window before engine_recirc_window or the loop tail is silence).
  BENCH GATE before v1.2.1 graduates: rate-4.0 ISR headroom (isr_pk) + rc field tests.
- **v1.2.1-rc5 PRE-RELEASE (2026-07-23): SIGNAL-GATED STORE END (#10, field-designed by
  @twostroke-ux).** rc3 regression found: auto store-end takes cycled WRITE->HOLD->WRITE
  forever (HOLD is silent + re-arm punches out of it) = "nothing other than write". Now:
  auto takes in store end punch out after AUTO_RELEASE_TICKS (~120 ms) of silence (hang
  trimmed from the window, AUTO_MIN_LOOP floor), cap -> loop the cycle; manual (momentary)
  takes keep owner-tested hold-and-recall. g_lp_take_auto tracks origin. store beg. =
  cycle-quantized (unchanged; docs explain the write-LED tell for staccato). Varispeed
  field-confirmed ×1 by RECLee (#9); ×4 = the ISR bench check. The looper state machine
  is now 5 states x 4 policies deep in main.c — EXTRACT TO looper.c + host suite before
  v1.2.1 (top engineering priority, owner-acked). DONE 2026-07-23: src/looper.{h,c} =
  1:1 extraction (BSP-free, cfg injected, LED intents w/ tri-state READY — EOC stays in
  main), test_looper.c = the 32nd suite (full transition matrix). Semantic notes the
  suite encodes: fire-tick lamps still show the PRIOR state's set (faithful); a
  switch-movement reset with signal present captures on the SAME tick.
- **#9 field update (2026-07-24): ×4 varispeed CONFIRMED no-glitch by RECLee** (field half of
  the ISR gate; bench isr_pk read = formality). NEW report: percussive clipping in loops, not
  in the original, ×1 and ×4 alike. Video audio analyzed (owner-approved download): hard
  flat-tops confirmed real. Camera can't localize the stage → posted the clip-LED
  discrimination procedure (stage1 flash at record = ADC rail/gain staging; stage2 at
  playback only = internal overshoot = OUR bug, fix immediately; never = soft-knee on
  0.75..1.0 peaks). Leading hypothesis: input ADC overload on hot modular transients.
  Awaiting the LED verdict before any code change.
- **v1.2.1-rc3 PRE-RELEASE (2026-07-22, supersedes rc1/rc2 — the AUTO CONTROL line):**
  rc1 = red-switch toggle resets the looper (#13); rc2 = store beg./end toggle likewise (#16 —
  same no-transition-handling family); rc3 = **AUTO RE-ARM (#10): the shared silence->onset
  trigger law now runs in LOOP and HOLD too** — a playing loop re-triggers on the next onset
  (or arm pulse), giving the batchas-video 288v stutter. Evidence came from reading the MW
  thread via the owner's Chrome (Cloudflare blocks curl/WebFetch): reporter jimfowler =
  @twostroke-ux, was on v1.2.0 baseline; Mixcatonic surfaced the 288v video (auto control
  cycles write/recirc continuously = stock re-arms); Mixcatonic still hunting varispeed (#9)
  evidence — none found, so #9 leans design-option. NOT hardware-verified (bench offline) —
  graduate to v1.2.1 on field confirmation. Store-end held window shows write+READY LEDs
  together ("stored and waiting") — documented, was misread as stuck-in-write.
- The interpolation PATCH (`re/patches/`) remains the drop-in fix for the *stock* firmware.

## Key technical facts
- MCU **STM32F429ZET6** (LQFP144) — confirmed from chip marking: **512 KB flash**, 192 KB SRAM
  (SP `0x20030000`) + 64 KB CCM. (`STM32F429.ld` FLASH = 512K.)
- **Codec = Cirrus Logic CS42448** (bench-confirmed chip ID 0x04; earlier notes read it as CS42888 —
  48-TQFP, the chip by the STLINK header; the "second ST QFP" was a misread Cirrus logo; there is NO
  second MCU). **4 ADC-in / 8 DAC-out as used, 24-bit, TDM/I²S**,
  control over I²C or SPI2. → the **8 taps each get their own DAC output**; the F429 drives it via
  **SAI2 multichannel TDM** (hence the firmware's A/B paths). audio_io/engine output should be
  **8-channel TDM**, not one mixed output.
- **SDRAM = ISSI IS42S16400 (8 MB, 4M×16, 16-bit)** @ `0xC0000000` via FMC → use an **int16** buffer
  (float32 won't fit 40 s). Audio **24-bit / 96 kHz** (vendor "196KHz" = typo). Stock image 27,912 B.
- Panel = **74HC595/74HC4051 hardware scan** (DIP-binary tap times 10 ms steps, phase/mute DIPs, 36
  trimmers muxed to ADC) → presets live-read, **likely no NVM**. Full board brief: `re/notes/hardware.md`.
- SWD open (RDP-0 expected); ST-Link/V2 ships with the kit. **NO external EEPROM** (RESOLVED): the
  BOM's `25AA512` was a paste error over a 20-pin connector MPN (PLD1/2 female ↔ PBD1/2 male), and the
  stock `.hex` uses no SPI EEPROM → **no NVM chip; persistence = F429 internal-flash emulation.**
- **Panel switches (BOM):** only **SW14 `(ON)-OFF-(ON)`** and **SW16 `ON-OFF-(ON)`** are momentary →
  the mode-entry **gesture** switches (power-up hold = cal/save). `cal./pre-set` + `A/B/C` are latching
  selectors. Calibration targets: 9 sliders + 7 pots + 36 trimmers (ADC via 4051 mux) + CV inputs
  (Time-CV range bug). Cal routine spec in DESIGN.md "Calibration routine".

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
cd firmware && make engine   # cross-compile engine for STM32F429 (compile-only proof)
cd firmware && make firmware # link flashable image -> build/fw/b288-community.hex
re/.venv/bin/python re/scripts/apply_patch1.py   # (re)generate + verify Patch 1 -> re/patches/patched.hex
```

## What's next
**Resolved** (no longer open): MCU F429Z + 24/96 + 74HC595/4051 scan; codec = **CS42448**
(4-in/8-out TDM as used, no second MCU); SDRAM = **IS42S16400 8 MB/16-bit** → int16/int32 buffer; **no
EEPROM** (BOM paste error); panel switch→GPIO map traced; momentary switches SW14/16 identified.
**Bench session 1 done (2026-07-16, SWD read-only — see `re/notes/bench-session-1.md`):** RDP open;
unit fw == our ref (patch valid); **codec bus = I²C1** (SPI2 = control-surface ADC); audio is **SAI1**
(not SAI2), TDM **8 slots × 32-bit / 24-bit**; **HSE = 8 MHz → SYSCLK 168 MHz, APB1 42 / APB2 84**.
**Still needs the bench:** flash the patch + **listen** (validation), codec I²C address+regs (boot
sniff / logic analyzer), slot→tap order (live test), the exact **pinout** → StdPeriph init, and
**calibration constants** (TIME CV range/taper, slider/pot
gain law, AUTO CONTROL, pulse thresholds). Markers: `TODO(bench)`/`TODO(init)` in `main.c` +
`STM32F429.ld`; full checklist in `re/notes/hardware.md` and `firmware/README.md` "Blocked on hardware".

**Doable now without hardware (mostly done):** ✅ Patch 1 both paths + mode6, ✅ one-pole envelope
followers, ✅ interp-quality measurement. Remaining optional/speculative: an all-pass fractional
interpolation option (good for flanger, but its modulation transients can't be A/B'd without audio
hardware — defer to bench), and more host tests. Further substantive progress needs the board.

**When the bench session happens:** flash `re/patches/patched.hex`, breakpoint `0x08001aa6`, confirm
the read pointer stair-steps on the stock fw and is continuous after the patch; then start
the StdPeriph init layer (reusing MARF's `Libraries/`) and calibrate constants against the real panel.

## Conventions
- **Release notes (owner requirement):** every release's `docs/release-notes-vX.Y.Z.md` uses
  three clean sections — **New features / Fixed / Known & open issues** — with open issues
  restated fresh each release. CI uses that file as the GitHub release body automatically
  (falls back to generated notes if missing).
- Clone-first; don't invent precise constants — parameterize and mark `calibrate on hardware`.
- **Buffer/fidelity (decided):** SDRAM stores **int16 (vintage) / int32 (hi-fi)** — NOT float32;
  fidelity is a live front-panel switch (3 levels 12/16/20-bit in stock) that also sets the SDRAM
  layout (int16 → two ~20 s banks; int32 → one ~20 s bank), fixed at boot. Full spec: DESIGN.md
  "Memory & fidelity — SDRAM buffer layout". Bank_B = recirc/loop path (stock).
- Keep `Compiled FW/B288-REV1.0.hex` untouched (golden). BOOT0 ROM bootloader is the recovery path.
- Attribution: `re/binja/` analysis is @Mixcatonic's (see README Credits) — preserve it.
- Personal machine notes for the original author live outside the repo (`~/.claude/.../memory/`);
  this file is the shared, in-repo handoff.
