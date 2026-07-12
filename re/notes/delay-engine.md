# 288r Delay Engine — full data-flow + fix strategy

Cross-referenced from Binary Ninja artifacts (`re/binja/`) + our own capstone analysis.
**Address mapping:** Binary Ninja `sub_X` / decompiler offset `X`  ==  our flash address `0x08000000 + X`.
(Their .bin was loaded at base 0; confirmed: their `sub_1180` = our `init_sai_stream` @ 0x08001180.)

## Signal chain (per audio sample, driven by SAI2 DMA half/complete IRQs)

```
        SAI2 RX (24-bit codec in)
                 │
     ┌───────────▼────────────────────────────────────────┐
     │ delay_tap_service_A/B  (sub_1250 / sub_15dc)        │  WRITE side
     │  - sign-extend 24-bit input                         │
     │  - 256-tap running-avg envelope follower            │
     │    (ring @0x20001aec, accum @0x20001700)            │
     │  - delay_ram_bank_A[delay_write_ptr] = sample       │  ← circular write
     │  - sample >>= 12/16/20  (VINTAGE bit-depth reduce)  │
     │  - delay_write_ptr++ (wrap @ delay_ram_length)      │
     │  - write→recirc→loop transport state machine        │
     └───────────┬────────────────────────────────────────┘
                 │
     ┌───────────▼────────────────────────────────────────┐
     │ time_multiplier_and_envfollow (sub_2030)            │  DELAY-TIME control
     │  - reads TIME MULT ADC/CV -> double-precision math   │
     │  - snaps SAI sample-clock PLL in OCTAVE steps        │  ← RCC_PLLSAICFGR 0x40023888
     │    (×1 / ×0.5 / ×0.25) with hysteresis (~0.955)      │     RCC_DCKCFGR  0x4002388c
     │  - tap targets = roundf(preset_phase[i]*multiplier)  │  ← INTEGER targets -> 0x20001f98
     └───────────┬────────────────────────────────────────┘
                 │
     ┌───────────▼────────────────────────────────────────┐
     │ tap read + output mixer (sub_1968 / sub_1c98)       │  READ side  ← mislabeled
     │  for each of 8 taps (preset_tap_current_table        │    "auto_control" in rename
     │      @0x2000000c, chases target table):              │
     │    dist  = tap_pos + corr + tap_pos*scale  (float)   │
     │    dist  = roundf(dist)               ◄── ZIPPER     │  *** ROOT CAUSE (read) ***
     │    rptr  = delay_write_ptr - dist   (integer)        │
     │    wrap rptr vs length / loop start/end              │
     │    out   = delay_ram_bank[rptr]     (single fetch)   │  ← NO interpolation
     │    out <<= 12/16/20 ; stage into SAI DMA out buffer  │
     └───────────┬────────────────────────────────────────┘
                 │
             SAI2 TX (codec out)
```

## Buffer (external SDRAM via FMC; SDCR @0xA0000140)
- `delay_ram_length` @0x20000000, set in `main_init_and_run_loop` (sub_2508) by
  short/full-cycle (sub_fc8) × cal (sub_fbc): **90,400 / ~22,600 / 225,488 / 903,232 samples**.
- 903,232 × 4 B ≈ 3.6 MB → confirms external SDRAM. This is the "up to ~40 s" buffer.
- `delay_ram_bank_A` @0x20000004, `delay_ram_bank_B` @0x20000008 (recirc/second path).
- `delay_write_ptr` @0x200000c4, `delay_read_offset` @0x200000d4,
  loop start/end @0x200000cc / 0x200013c0.

## Why smooth modulation / chorus / flanger does NOT work — two confirmed causes
1. **Read-side quantization (primary).** In sub_1968 the per-tap read distance is a float but
   is `roundf()`-ed before indexing, and the sample is a single integer-indexed fetch
   `delay_ram_bank[write_ptr - round(dist)]`. Sweeping the delay time moves `dist` continuously,
   but the read address jumps one whole sample at a time → zipper/stepping, no smooth pitch glide.
   **A chorus/flanger fundamentally needs a fractional (interpolated) delay tap.**
2. **Coarse delay = sample-clock retune (secondary).** sub_2030 changes the *coarse* delay by
   re-locking the SAI PLL in octave steps (0x40023888/0x4002388c) with hysteresis, instead of
   keeping a fixed rate and varying a continuous read offset. PLL re-lock = glitch + discrete jumps.

## Fix strategy (localized, patch-friendly)

### Patch 1 — fractional interpolation on the tap read  (HIGH impact, low risk)
Target: sub_1968 inner loop (and its twin sub_1c98), the `roundf` + single-fetch region
(decompile lines ~1975-2011; find the `roundf` call site + the `ldr` from `delay_ram_bank`).
Change to linear interpolation:
```
    d      = write_ptr - dist                 ; keep dist as FLOAT (drop roundf)
    i0     = floor(d)          ; frac = d - i0
    i1     = i0 - 1  (older sample; wrap)      ; adjacent tap
    out    = buf[i0]*(1-frac) + buf[i1]*frac   ; VMUL.F32/VMLA.F32 (hard FPU present)
```
Because we can't grow the function in place, use a **flash code-cave detour**: the image ends at
0x08006D07 on a ≥512 KB part, so 0x08007000+ is free. Branch (`b.w`) from the read site into the
cave, do floor+two-fetch+lerp there, branch back. Assemble with `arm-none-eabi-as`
(keystone won't load on arm64). Wrap handling must respect loop start/end pointers, same as current.

### Patch 2 — carry fractional delay time end-to-end  (enables true flanger sweeps)
- In sub_2030, stop `roundf`-ing the tap targets (0x20001f98) — keep them Q-format fixed point or
  float so the current-position slew (0x2000000c) can chase sub-sample positions.
- Add/verify a one-pole slew on the effective read distance so a modulating CV yields a smooth
  swept `dist` (short flanger delays ~0.1-10 ms need sub-sample resolution + high update rate).
- Consider bypassing the PLL octave-retune for a "modulation" mode: fixed sample rate, all delay
  variation via the interpolated fractional offset (classic BBD-style chorus/flanger behavior).

### Exact patch anchors (verified in disassembly of sub_1968 @ 0x08001968)
Read core, tap loop body:
```
0x08001a7a: ldr    r1,[r4,#4]!      ; r1 = preset_tap_current_table[i] (tap position, samples)
0x08001a82: add    r3, r1           ; r3 = correction + tap_pos
0x08001aa0: vfma.f32 s15,s14,s13    ; s15 = dist = (corr+tap_pos) + tap_pos*scale   [FLOAT, has frac]
0x08001aa6: vcvt.s32.f32 s15,s15    ; <== TRUNCATE dist to int   *** PRIMARY ANCHOR (drop/replace) ***
0x08001aaa: vmov   r3,s15
0x08001aae: sub.w  r3,r2,r3         ; read_ptr = write_ptr - int(dist)  -> *0x200000d4
   ...loop/recirc wrap logic 0x08001ab6..0x08001ae0 (leaves wrapped read_ptr in *0x200000d4)...
0x08001ae4: ldr.w  r3,[sl]          ; sl = *0x20000004 = delay_ram_bank_A base
0x08001ae8: ldr.w  r3,[r3,r2,lsl #2]; <== SINGLE FETCH bank[read_ptr]   *** SECOND ANCHOR ***
0x08001aec: str.w  r3,[fp]          ; -> output_sample_scratch 0x20002084
   (mode==6 path fetches from delay_ram_bank_B @0x08001b78, same shape)
```
**Two-detour patch plan** (image ends 0x08006D07; cave space free from 0x08007000 on a ≥512 KB part):
- Detour A @0x08001aa6: `b.w cave_floor` — compute i0=floorf(dist), frac=dist-i0; keep frac in a
  callee-saved VFP reg (s16/d8 already vpush'd; use s17); write read_ptr=wp-i0; return to 0x08001aaa
  so the existing wrap logic runs unchanged on i0.
- Detour B @0x08001ae8: `b.w cave_lerp` — with wrapped i0 in *0x200000d4, compute i1=wrap(i0+1) via
  length (*0x20000000) + loop pts, fetch bank[i0] & bank[i1], `out = bank[i0]*(1-frac)+bank[i1]*frac`
  (VMUL/VFMA.F32), truncate to int, store, return to 0x08001aec.
- Twin function sub_1c98 (path B) gets the same treatment. Do NOT touch write path (sub_1250/15dc)
  or the wrap arithmetic — only the read interpolation changes.

### Verification (needs the SWD session, a few days out)
- Flash stock hex, breakpoint the sub_1968 read site, log `dist`/`rptr` while sweeping the TIME pot
  → confirm the integer stair-step. Then flash Patch 1 and confirm `rptr`/interp is continuous.
- A/B listen: slow LFO into TIME CV should give glide-free chorus, not zipper.

## Key symbol map (add flash base 0x08000000 to get our addresses)
- write:  delay_tap_service_A 0x1250 / _B 0x15dc
- time:   time_multiplier_and_envfollow 0x2030
- read:   tap-read+mixer 0x1968 / 0x1c98   (rename calls these "auto_control_ramp_update")
- main:   main_init_and_run_loop 0x2508
- codec:  init_sai_stream 0x1180 ; sai_dma_*_cb 0x5ce8/0x5d48/0x5d54/0x5db4
- panel:  read_mode_switches 0xf64 ; get_cycle_length_switch 0xfc8 ; lookup_preset_tap_position 0x3c64
- Full map: re/binja/rename_288r.py ; decompile: re/binja/288r_decompiled_abridged.txt
```
