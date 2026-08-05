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
- **BENCH SESSION 8 (2026-07-24, owner partially present then remote): varispeed gate PASSED,
  seam fix landed, and an OPEN CLOCK MYSTERY.** (1) ISR gate: genuine rate-4.0 varispeed =
  70% of budget (isr_pk 2464/3500), 20 s soak flat — the earlier 61% read was pitch-mode-loop
  by accident (TIME/pitch switch was in pitch; symptom triad "knob does nothing + clicks +
  not smooth" = pitch mode's depth knob + splicing; ALWAYS check that switch first). (2) Wrap
  click: dl_loop_splice (capture-time tail->lead-in crossfade) + 4 GUARD SAMPLES past
  loop_end (the interp stencil reads whole-buffer neighbours; fractional rates walk through
  the seam zone every wrap). Host regression proves guards specifically (fails without).
  34 suites. Owner's "still clicks" listen predates the guard flash — re-verify by ear.
  (3) g_dbg_eoc_mute (SWD) isolates EOC-pulse electrical bleed — inconclusive so far.
  (4) CAPTURE CHAIN UNRELIABLE: Big Six/ffmpeg avfoundation injects ~20/s drift-splice
  events + mislabels rate (abs frequencies wrong, ratios OK) — remote listening needs a
  clean capture path before trusting click analysis. (5) **OPEN MYSTERY [BENCH]: head
  advance measured ~192k samples/s at BOTH rate 1 and rate 4 while the block clock reads
  exactly 96k frames/s and lp_rate=4.0** — mutually contradictory via SWD-sampled wpos
  (wrap-unwrap ambiguity suspected); need a bench measurement of the true frame clock +
  advance factor (reference tone through the dry path, or scope MCLK). The vendor's
  "196KHz" spec may not have been a typo — verify before more absolute-frequency DSP.
  Remote SWD looper driving recipe: poke xport window/mode + g_lp.state + load_image
  sine into delay_buf (0xd0100000) + lp_mult_ref for rate; offsets via __builtin_offsetof
  arm compile (dl.wpos@8, xport.mode@0xd0, ls@0xd8, le@0xdc, tc.mult@0xc0, varispeed@0x148,
  ref@0x14c, phase@0x150, rate@0x154; g_lp.state@+0x18).
- **v1.2.1 RELEASED (2026-07-24) — the AUTO CONTROL release:** varispeed, signal-gated
  store end, auto re-arm, both toggle resets, wrap-click fix (splice+guards, owner
  ear-confirmed), looper extraction. Same-day field reports (owner, live): pitch mode ×
  looper glitched (#19) + pitch controls felt seconds-slow in ×4 (#20).
- **v1.2.2-rc1 PRE-RELEASE (2026-07-24, same-day fix cycle): PITCH VOICE IS LOOP-AWARE
  (#19) + echo pattern exempt from ×4 (#20).** All ps reads window-map via
  dl_read_loop_frac in RECIRC (ps_set_loop_window per block in the ISR prologue); AA
  bypassed while looping (streaming cache can't cross the seam); pt-ring echo + dry-anchor
  delays scale 1/extend (loop capture length still composes with ×4); varispeed gated off
  in KS mode (audit). VERIFIED: clip counter 0 vs 100+/s baseline at every ratio 0.5-3.9,
  BOTH window orientations, ISR 74-80%. ADVERSARIAL REVIEW CAUGHT A REAL PRE-FLASH BUG:
  wrapped windows (end<start = MAJORITY of long captures; start=head+len-window) were
  rejected by the setter — silently disabling the fix exactly where it matters; span law
  now mirrors dl_read_loop_frac. test_pitch_loop.c (34 suites) covers both orientations +
  a validity check (unmapped path must glitch). Remote recipe: force ratio via
  g_dbg_ratio_force, watch clip_q + isr_pk — full objective A/B without audio.
- **"NASTY CRACKLY MODE" diagnosed live over SWD (2026-07-24 evening) → v1.2.2-rc2:** the
  env→time self-mod (sens knob, all-sounds) at full depth with the sens channel carrying
  ~0.3 FS of attenuated input + the ×4 base = continuous ±28k-sample delay jitter = garble.
  Diagnosis chain: mode flags all clean → tc.mult wobbling vs static knob/CV → g_sens_env
  float 0.30-0.33 wobbling with the playing. OWNER CALL: feature DEFAULT OFF
  (ENV_TIME_ENABLE 0) until the #15 bipolar redesign (slow ~80ms envelope, capped depth,
  extend-exempt). Docs updated (ch3/ch5); #15 informed. Diagnostic lesson: "wobbling
  tc.mult with static inputs" = check every additive control source, the dbg sens display
  caps at 255 and HIDES saturation dynamics — read the float.
- **"Knob dead" postmortem (2026-07-24 late): THE ST-LINK CABLE WAS STRESSING THE BOARD** —
  flexed the control-ADC (MCP3204) connection: bit-shifted SPI frames (knob→CV channel
  bleed, ~20% control range, phantom CV offsets) then fully mute MISO after a power cycle.
  Removing the cable stress fixed everything. Firmware verified clean end-to-end during
  the diagnosis (SPI2 CR1/SR healthy, control law, pin logic all correct). LAB RULE
  corollary: when the MODULE acts possessed, check what the cable is LEANING ON. Runbook
  updated. Earlier "SPI desync from SWD halt" theory was wrong — it was mechanical.
- **v1.2.2-rc3 PRE-RELEASE (2026-07-25): THE SENS CHANNEL HEARS THE MODULE'S OWN OUTPUT**
  — the unifying root cause behind field #21 (presence LED stuck on during recirc) AND #10
  (store-end silence punch-out never fired: the delay's own echoes held sens high through
  every rest) AND our reference unit's saturated sens with silent input. Fix: every
  silence/onset decision + the presence LED corroborated by input A's own envelope (g_env,
  proven clean during playback); looper_tick gains input_env + cfg.input_eps (LP_INPUT_EPS
  0.01 [cal]). Plus field-designed v2 of store-end auto: NO cycle cap — envelope is the
  master (rolling write while signal present; silence loops the LAST phrase bounded to one
  cycle). 37 looper checks. Also in the rc: 1 Hz SPI2+MCP3204 self-heal re-frame (owner:
  wedged knob states needing reboot — flash pending ST-Link reconnect), env→time OFF.
  jimfowler design votes: modulation = CV/FM only (matches the disable); signal-in dead on
  HIS unit too → possibly dead on the whole production run (analog path — asked the field).
- **env→time REMOVED ENTIRELY (2026-07-25, owner decision, #15 CLOSED):** code, defines
  (ENV_TIME_*, g_auto_now), and all manual references gone. Final modulation architecture:
  CV in (attenuverter + 10 ms de-zipper) = the modulation input; signal-in jack = the
  analog self-mod path (dead on 2+ units — production-run question open); sens knob = the
  looper capture threshold, nothing else. Attenuverter dead-zone width = cal-pass item.
- **MULTIPLIER POT DIAGNOSED (2026-07-25): worn/dirty track in the UPPER HALF of travel.**
  Definitive dual-trace method: owner's slow-sweep audio (slider-1-only direct voice)
  aligned second-by-second against a background SWD log of knob_raw/target/ratio. Lower
  half: pot smooth, audio glides 6-9 cents/step, ratio glued to target within a cent =
  the ENTIRE electronic chain exonerated. Upper half (codes ~2200-4094): pot flat 2-3.5 s
  then LEAPS 600-800 codes between samples; every audio "snap" (±40-60 cents) aligns with
  a leap. This retroactively explains "3-4 loose points" + "snaps to positions". Fix =
  hardware (exercise/DeoxIT/replace — repair list with slider 5). Optional firmware
  bandaid (not implemented): max-rate glide turning leaps into ~100 ms bends. ALSO that
  session: PITCH_RATIO_SLEW 0.0007→0.0028 (tau ~4 ms; the 15 ms glide read as knob lag —
  owner audio measured ~150 ms full-span portamento; old "steppy at 5 ms" was under the
  slow pass-counted tick cadence, invalid now); slider 1 = DIRECT pitched voice (instant
  response; sliders 2-8 = echo pattern; saves an SDRAM read); #23 detune-spread filed+
  parked. LESSON: coordinating live panel actions over chat fails ~4/5 times — use LONG
  passive background monitors + owner-paced recordings, align by content afterwards.
- **THE "QUANTIZED KNOB" WAS CONTROL-TICK STARVATION, NOT THE POT (2026-07-25, owner's
  mode-dependence observation cracked it):** the one-shot splice search burned 60-110M
  cycles (bass-extended sizes) freezing the superloop 0.5-3.6 s during pitch activity —
  knob readings froze then leapt (the "3-4 loose points" since v1.0.1). Proven with new
  tick_gap telemetry + a synthetic ratio-force ramp (no hands needed). Fixed: chunked
  resumable search+scan (PS_SRCH_CHUNK 4000 — do NOT shrink, see code comment: 1500 let
  frozen search geometry go stale at ratio ~4, purity collapse; ratio context stamp
  guards resume), adaptive decimation for bass windows (8x cheaper, quality identical:
  30 Hz purity 1.004, AA 1.000), sweep gate (no searches while ratio slews). Measured
  3634 ms -> 101 ms peak gaps. POT EXONERATED (upper-track diagnosis retracted — the
  "smooth lower half" was the sweep's slow start before searches kicked in). ALSO fixed
  same session: stale-flash trap struck AGAIN (make failed, openocd flashed old hex,
  'Verified OK' — CHECK BUILD EXIT BEFORE FLASH); fabsf isn't freestanding (fm_fabsf).
- **SIGNAL-IN SOLVED (2026-07-25 late, owner board-tracing + live SWD slot sweep): the
  signal-in jack = CODEC TDM RX SLOT 2** (jack -> pot -> R157||C126 -> 10k -> TL072 pair ->
  C124 -> CS42448 AIN pins 49/50). Live-verified rms 0.89 slot 2 with the jack driven.
  The years-old "dead analog path into the Time-CV net" verdict was WRONG (wrong slots
  watched); the stock computed its signal-in feature DIGITALLY from this channel. Opens:
  #15 env->time redesign on the REAL jack + dedicated pot (owner decision pending),
  sidechain/second-input ideas. jimfowler's "signal-in broken" needs a slot-2 retest.
  NOTE pot-up hot sources clip the ADC (peak 1.0 observed).
- **v1.2.2 RELEASED (2026-07-28) — the signal-in release.** Consolidates the rc1-rc6 line:
  signal-in = codec slot 2 reborn as the dual-domain FM input (TIME = delay-time FM, pitch =
  voice vibrato/FM; panel pot = depth; presence gate; per-tap slew limiting); pitch voice
  loop-aware (#19); tick-starvation fix (chunked splice search — the "quantized knob");
  SPI wedge dead (bounded waits + self-heal); slider 1 = direct voice; input-overload LED
  (#21, 277-style, field-designed); SELF-CORROBORATING capture threshold (#10 v3: sens
  pickup is PRE-POT PARALLEL per jimfowler's continuity check — the serial feel was OUR
  corroboration gate; new law = slow ~2 s ambient baseline + ~6 dB onset ratio, input-A
  pot fully decoupled, sidechain patches work); env→time removed. 36 suites. Field verdicts
  on the redesigned detector/LED still incoming (#10/#21 open). ROADMAP AGREED (owner):
  overdub/sound-on-sound (float accumulation, cheap), split-tap loop+live mode (tap
  partitioning = zero extra reads), stereo master option (2 DAC channels), Verbos-style
  knob-steered internal feedback (needs the 4051-mux control-map bench session — ADC3
  path, alive; g_dbg_muxscan is the tool). ISR headroom = the binding constraint (~20-35%
  TIME, ~15-25% pitch).
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
- **2026-07-29 marathon debug session (owner live): THE LED/EOC BLEED CRACKED — bench-8's
  open mystery CLOSED.** The DSP-driven indicator pins (PA0/1/7/8/11) electrically bleed
  into the audio path on EDGES. Proven by g_dbg_eoc_mute (wrap zipper vanished, owner ear)
  then g_dbg_led_mute in bsp/panel.c (the family-wide choke-point mute — od-session zipper
  vanished too). Two firing patterns: the EOC wrap blip (fuzz burst per loop pass, ~0.35-2 s
  periodicity in the owner's zipps.wav = the loop length), and threshold-compare LEDs
  edge-storming at up to block rate when an envelope hovers at threshold (AUTO/presence
  during sound-on-sound). Shipped mitigations (LEDs stay on): ~100 ms dwell hysteresis
  (LED_DWELL_BLOCKS) on AUTO/presence, KS breathing PWM (a 3 kHz edge stream) -> slow
  on/off, EOC blip 2 ticks -> 1 (~5 ms). If a whisper remains: bisect LED pin vs the
  PA7/PA8 jack pulses; consider TIM hardware-PWM drive (all 5 pins are timer channels).
  SAME SESSION, also landed: **varispeed TAP FREEZE** (stock tape-head model: on a playing
  loop the multiplier is MOTOR-ONLY, tap sample-distances hold capture scale — live
  respacing double-applied the knob and the fast sweeps aliased through Hermite = 'zippers
  then corrects', owner-confirmed fixed); **overdub v2**: slew-gain write limiter (the
  memoryless knee flat-topped stacks at ±0.92 = 'digitally clippy'; then the fast-attack
  follower gain-rippled at 2x signal = harmonic distortion baked in — now peak-hold env +
  ~5 ms slewed gain, THD 0.02%), varispeed-clock input resampling (box-decimate rate<1 /
  linear interp rate>1; ZOH dropped/duplicated = mid-od knob zipper), hard FS clamp
  (limiter lag baked 1.092 content), 2-pole ~10 kHz squeal guard on the layered input
  (an 18.87 kHz codec-round-trip feedback mode carried 13% of loop energy after od
  sessions — 'a little FM where layers overlap' = beats between layers' carrier copies);
  varispeed widened to ANY playing loop (owner ask). Field lab lessons (hard-won):
  g_dbg_panel.isr_pk is a NEVER-RESET boot latch re-copied every slow tick — reset
  g_isr_pk (the real static) or every read shows the all-time max (a 115% one-shot from
  the capture splice read as 'constant overrun' for an hour); re-nm EVERY address after
  EVERY build (a stale poke hit g_env); the owner's Big Six ch7 capture chain is NOT the
  mixed out (solo walk unaffected) — content dumps from SDRAM are the only trustworthy
  audio forensics; input-A rail clipping (peak parked at exactly 1.0 in content) is the
  gain-staging signature — check it FIRST on any 'staticy capture' report (also the
  leading #9 percussive-clipping suspect). Manual ch5 rewritten: signal-in = the FM input
  (stale 'dead analog path' text), plus a check-signal-in-first troubleshooting callout.
  Live ISR load in TIME-recirc measured ~90% of budget — tight; watch as features land.
- **v1.2.3-rc3 PRE-RELEASE (2026-07-30):** the marathon-session batch shipped (overdub v2
  line, tap freeze, chunked splice, LED mitigations, starvation breaker + od linear-interp
  headroom — od held at rate 1 now measures BELOW idle baseline; idle TIME-recirc ~93%).
  All open issues commented with rc3 asks. COMMITTED on-tracker: #13 floor-law-off
  diagnostic build (one comparison site in looper.c) + SENS_REF range cal (~-50..-6 dB
  sweep; jimfowler sending usable-arc data — his unit has NO output bleed, ours does:
  per-unit variance is real); #24 = NEXT DEBUG TARGET: spike crossing ratio 1.0 upward in
  pitch mode — suspect the AA engage boundary (up-shift-only = the +side fingerprint;
  force-sweep g_dbg_ratio_force over SWD to reproduce handless); #18 closing as by-design
  in a few days (tap time-scrub physics; explanation promised for the manual). Graduation
  to v1.2.3 on field verdicts (#13/#16/#19/#20).
- **v1.2.3-rc4 PRE-RELEASE (2026-07-31): #24 FIXED + a free 17-point ISR win.** RECLee's
  knob-only repro (full-CCW->CW, every time) overturned the AA-boundary theory: the real bug
  = the dry->voice WET SLIVER crossed by the ratio glide in <2 samples = an instant swap
  between reads 30-60 ms apart (spike on every unity departure; born when the thinned
  sliver met the quickened slew, both v1.2.2-era). Fix: the wet mix gets its OWN ~4 ms
  smoothing decoupled from ratio (can't park mid-mix, can't snap) + grain phase pinned 0.5
  in the unity bypass (bypass read == grain A @0.5 -> seamless exit; the ps-level phase bug
  alone did NOT reproduce on host — the audible spike was the main.c mix layer, remember
  the layering). Wire-proven: 12 forced unity crossings over SWD (g_dbg_ratio_force),
  owner-ear clean. BONUS: the wet_s edit shifted inlining topology and DROPPED the ISR
  ~93%->76% (bsp_audio_isr as own symbol; -4KB text) — headroom largely restored.
  LESSONS: CI was silently red for a day (M_PI vs Linux gcc in the new test — CHECK CI
  AFTER EVERY PUSH; a red CI silently blocks tagged releases); Big Six device index moved
  then vanished (avfoundation enumeration empty — re-check `-list_devices` before captures).
  Field state: #18 CLOSED (by-design, reporter-confirmed); #13 trending resolved as
  gain-staging (jimfowler: sidechain works, riff was too-staccato, 206 too hot; scope
  numbers pending; floor-off diagnostic build ON HOLD); #24 awaiting RECLee's rc4 verdict;
  #16/#19/#20 awaiting rc3/rc4 confirmations -> then v1.2.3 graduates.
- **v1.2.3-rc5 PRE-RELEASE (2026-08-01): #24 FULLY CRACKED — a THREE-LAYER bug, each layer
  isolated by a different field repro.** Layer 1 (rc4, knob departures): wet-sliver snap ->
  4ms smoothing. Layer 2 (envelope crossings of 1.02): AA engage = hard kernel swap + a
  flash-rows CPU spike -> hysteresis (on>1.025/off<1.015) + ~5ms AA<->Hermite crossfade +
  rows-ready gate + band-0 kept pre-published by the superloop (test_aa asserts the
  invariants w/ a delayed-publisher; NOTE: waveform-differential click detectors CANNOT see
  this class — the steady spectral difference between paths swamps transients; assert
  invariants instead). Layer 3, THE CORE (found by live LFO + aa_bypass poke = AA
  exonerated, clicks persisted): the exact-unity BYPASS COLLAPSE jumped the grain read to
  the window center on every bypass ENTRY — bipolar vibrato grazes unity ~6x/s = pops
  immune to every smoothing above. FINAL ARCHITECTURE: no special unity read path (grains
  play through; phase/offsets DRIFT to the single-grain pole ~1s; pole INIT keeps parked
  unity bit-exact) + TIMESCALE-SEPARATED dry swap in main.c (true dry only after ~300ms
  SETTLED at unity; modulation keeps the voice engaged — no source swaps = no pops AND no
  'random drops'). Owner-verified clean under live bipolar LFO. ALSO: isr_pk protocol
  gotcha resurfaced — SWD-forced ratio steps (20-25Hz pokes) create their OWN pops (the
  slew passes micro-steps through in 1 sample): SWD forcing CANNOT emulate continuous CV;
  use a real LFO for ear tests. And the day's meta-lesson: a silent str.replace no-op
  flashed the same image twice ('identical text size' = the tell — ASSERT every patch
  landed). Awaiting RECLee's 281/e verdict -> #24 closes -> v1.2.3 graduates.
- **v1.2.3-rc6 PRE-RELEASE (2026-08-02): #24 layer 4/5 — CONTINUOUS AA COEFFICIENT MORPH.**
  RECLee's rc5 verdict: unity crossings fixed, deeper 281e sweeps still clicked at fixed
  points = the AA band edges (1.40/2.00/2.83): hard table swaps + a revoke-to-flash window
  per republish. Fix: the publisher LERPS adjacent band tables along a continuous band
  coordinate (+/-6% zones, 2%-motion republish) into PING-PONG CCM buffers swapped
  atomically — response GLIDES (swap delta 0.183 -> 0.0037/step, 50x finer), revoke-to-
  flash eliminated, read path uses published rows unconditionally (band-match gate gone).
  NOT yet bench-flashed (unit off) — rc6 shipped for RECLee's deterministic repro as the
  definitive test (owner call). NOTE for the flash-next-power-on queue: current HEAD =
  morph build; verify isr_pk through a forced deep ramp when flashed. Also that session:
  slider-5 hunt continues — jack is NOT normalled (topology corrected: jack tip and fader
  top are one hard net; TL072 on PCB1 = local summing, two virtual-earth buses ch0-4/
  ch5-8 = fader pin-3 groups; do NOT bridge them); every DC measurement passes mated and
  unmated; next = SIGNAL-INJECTION test (oscillator driven backwards into the tap-5 jack,
  slider solo, listen at mix out) to split fader-branch vs feed live with zero disassembly.
- **v1.3.0 RELEASED (2026-08-02) — the rc3-rc6 graduation (owner call: 1.3, it's a feature
  release).** Headline features: OVERDUB/sound-on-sound (slew-gain write limiter, varispeed-
  clock input resampling, ~10 kHz squeal-guard LP, FS clamp, starvation breaker + linear-
  interp headroom); TAPE-MOTOR multiplier on any playing loop (tap freeze = fixed heads);
  CONTINUOUS pitch modulation (unity pole-drift + timescale-separated dry swap + AA engage
  crossfade/hysteresis/rows-gate + band-edge coefficient MORPH w/ ping-pong CCM). Fixed:
  recorded seam tear (chunked splice), LED-bleed mitigations, ISR 93->76%. Manual ch5/ch6
  updated; release notes = the consolidation. ISSUES: #9/#10/#16/#19/#20 CLOSED as shipped;
  #13/#18 closed earlier (gain-staging + by-design); #24 open pending RECLee's deep-sweep
  verdict on the morph; #5 open (roadmap). **NEW #25 (RECLee): write/recirc PULSE OUT jacks
  (red bananas) never fire during loop capture — top post-1.3 item.** Note the jacks share
  DSP-driven pins with the transport LEDs (bleed-mitigation interaction possible; or we
  never drove them stock-correctly — decompile what stock does on PA7/PA8-family pins at
  transport transitions + scope the jacks on the bench). ALSO OPEN: the reference unit is
  still on the rc5-era flash (rc6/v1.3.0 content NOT yet flashed locally — unit was off;
  flash + forced-deep-ramp isr_pk check on next power-on); slider-5 mix-dead hunt paused at
  the signal-injection test (see rc6 entry). Roadmap: split-tap loop+live, stereo master,
  knob-steered feedback (needs the 4051 mux bench session).
- **Post-1.3 feature slate DRAFTED (2026-08-04, owner brainstorm):** see DESIGN.md
  "Post-v1.3.0 feature slate" — next-cycle trio = ducked delay (sens knob in all-sounds,
  currently unused there), micropitch spread #23 (rear DIP4 + per-preset), reverse loop
  (hold-write-2s in-loop); wow/flutter rides the vintage DIP; longer arc = harmonizer regen
  -> FDN wash -> shimmer (the knob-steered-feedback trilogy, needs the 4051-mux bench
  session); config-layer gesture (write+recirc hold) held in reserve. Session learnings
  exported to claude_trix (7 tricks: fw-isr-starvation-self-latch, fw-realtime-burst-
  amortize, fw-dsp-table-switch-morph, fw-dsp-modulation-special-cases, fw-telemetry-
  latch-vs-copy, hw-dmm-continuity-vs-signal, hw-gpio-edge-bleed-audio).
- **v1.3.0 FLASHED LOCALLY + DEEP-RAMP GATE PASSED (2026-08-04):** reference unit now runs
  release content. Forced ratio ramp 1.0->2.5->1.0 in pitch mode: flat 88-90% isr_pk through
  every AA band-edge crossing both directions (edges invisible in the load profile = the
  morph working); worst 94% at the engage crossfade window; zero overruns. Local half of the
  #24 verification complete — RECLee's 281e ear verdict is the remaining field half.
- **SLIDER 5 REPAIRED (2026-08-04) — the oldest hardware item CLOSED.** Root cause: an OPEN
  joint between ch5's ~68 k summing resistor and its phase-switch center lug on PCB1 (dead
  since before the project; the origin of v1.0.1's 'slider 5 = dead analog path'). Found by
  the backwards-injection live walk after every DC measurement passed; bridged, channel
  restored. Full traced mixer topology now in re/notes/hardware.md (fader lug map, 4.99 k
  taper loads, 68 k -> phase-switch(center=MUTE) -> bus -> 4.7 k -> dual-TL072 sum, mix out
  = pin 7 via 470 R; tap jacks NOT normalled; the two fader ground groups sit at different
  DC — don't bridge). Firmware kept identity slot mapping throughout, so no code change
  needed; the v1.0.1 debug scaffolding (g_dac_solo etc.) stays — it carried this hunt.
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
