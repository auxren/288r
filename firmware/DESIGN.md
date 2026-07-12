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
| HAL/CubeMX peripheral init (RCC, SAI2, DMA2, FMC-SDRAM, ADC, I²C, TIM, GPIO) | **Regenerate** from recovered config | boilerplate; not worth hand-porting |
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
 codec in ─▶ [input mixer] ─▶ write float32 → SDRAM circular buffer (delay_line)
                                   │  (optional vintage quantize+dither on write)
 8 taps ─▶ for each tap:  delay_samples = base_time · phase[i] · time_mult(+CV, slewed)
                          out_i = interp(delay_line, delay_samples)   ← fractional, HW float
        ─▶ [output mixer + phase select] ─▶ [auto control] ─▶ codec out
                          pulse taps ─▶ pulse outputs
```

- **Buffer stores normalized `float32`** (same 4 bytes/sample as the stock int32 — no memory
  penalty) → interpolation and mixing are branch-free hardware-float.
- **Interpolation** selectable: linear (cheap) → 4-point cubic/Hermite (better HF) → optional
  first-order all-pass (flat magnitude, ideal for flanger). See `src/delay_line.c`.
- **Delay-time control**: raw ADC/CV → single-precision → one-pole slew → fractional `delay_samples`.
  A slow LFO or CV then sweeps the taps continuously (chorus/flanger) with no zipper.

## Audio-quality improvements
- Fractional/interpolated taps → glide instead of stepping; usable pitch/chorus/flanger.
- Full-precision float internal path; "vintage" bit-depth becomes an explicit, dithered option.
- More output headroom / consistent gain staging in the mixer (verify against panel on hardware).

## Efficiency improvements
- Kill soft-float doubles → hardware single-precision FPU (the big win in the control path).
- No PLL re-lock on delay changes (no clock-domain glitch, no re-lock latency).
- Block-based processing over the SAI DMA half/full buffers; one-pole followers; SDRAM access
  patterned for the FMC (sequential write head; interpolation reads are local ±2 samples per tap).

## Module plan (`firmware/src/`)
```
delay_line.{h,c}   fixed-rate fractional delay line + interpolation      ← started
taps.{h,c}         8-tap 288 model: phase-select, presets, per-tap time
time_control.{h,c} ADC/CV → slewed single-precision delay_samples (no PLL)
transport.{h,c}    write / recirc / loop state machine
mixer.{h,c}        input & output mixers, phase select, auto control
audio_io.{h,c}     SAI2/DMA2 block callbacks, format conversion
panel.{h,c}        switches, DIP presets, I²C sliders, trimmers
main.c             init + superloop
```

## Open decisions & dependencies
- **Scope/fidelity** (your call): strict behavioral clone with the engine improved only where it
  was broken, vs. an enhanced instrument (new interpolation modes, extra modulation, remapped
  controls). The engine work below is valid either way.
- **Hardware confirmation (needs the SWD/bench session):** exact codec part + control wiring, SDRAM
  size (sets max delay at each base rate), pulse-output driver (bug #2 may be partly analog), and
  the real control→parameter scaling so we match the panel. Until then the engine is developed and
  unit-tested host-side; hardware brings it up against the real board.
