# Patches

Binary patches to `re/B288-REV1.0.bin` via flash **code-cave detours** (free flash from
`0x08007000` on the STM32F429). Each patch: assemble a cave, splice `b.w` detours over the
anchor instruction(s), re-emit `patched.hex`, and re-disassemble to verify.

## Patch 1 — fractional (interpolated) delay tap  → smooth modulation / chorus / flanger

**Problem.** In read fn `sub_1968`, the per-tap read distance is computed in float (`s15`, the
`vfma.f32` at `0x08001aa0`) but immediately **truncated to an integer** (`vcvt.s32.f32` @
`0x08001aa6`), and the delayed sample is a **single integer-indexed fetch** (`ldr.w r3,[r3,r2,lsl#2]`
@ `0x08001ae8`). Sweeping the delay time steps one whole sample at a time → zipper, no glide.

**Fix.** Two caves (`patch1_interp.s`):
- `caveA` @ anchor `0x08001aa6`: `i0 = (int)dist`, `frac = dist - i0` (kept in `s14`), store
  `read_ptr0 = write_ptr - i0`, restore the Z flag the caller's `beq` needs, return to `0x08001ab6`.
- `caveB` @ anchor `0x08001ae8`: fetch `bank[i0]` and `bank[i0-1]` (wrapped by `delay_ram_length`
  @ `0x20000000`), return `out = s0 + (s1-s0)*frac` to `0x08001aec`.

Cortex-M4 has no `VRINT*`, so we use `VCVT.S32.F32` (round-toward-zero = floor for `dist ≥ 0`),
matching the stock code.

**Coverage:** both audio paths and both recirc fetches are patched — 6 detours, 184-byte cave:

| Anchor | Path / case | Cave |
|--------|-------------|------|
| `0x08001aa6` | A (`sub_1968`) truncate dist | `caveA`  → ret `0x08001ab6` |
| `0x08001ae8` | A main tap fetch            | `caveB`  → ret `0x08001aec` |
| `0x08001b78` | A `mode==6` bank_B fetch     | `caveB`  → ret `0x08001aec` |
| `0x08001dd2` | B (`sub_1c98`) truncate dist | `caveA_B`→ ret `0x08001de2` |
| `0x08001e14` | B main tap fetch            | `caveB_B`→ ret `0x08001e18` |
| `0x08001ea4` | B `mode==6` bank_B fetch     | `caveB_B`→ ret `0x08001e18` |

**Build / apply / verify (static):**
```bash
re/.venv/bin/python re/scripts/apply_patch1.py     # -> re/patches/patched.{bin,hex}
```
The splicer asserts the anchor instructions match before patching and prints the resulting
detours + cave disassembly. Output is byte-verified; it has **not** been run on hardware yet.

**Flash (when the SWD session is available):**
```bash
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
    -c "program re/patches/patched.hex verify reset exit"
```
Keep `Compiled FW/B288-REV1.0.hex` as the golden restore image (BOOT0 ROM bootloader is the
ultimate fallback).

**Validate:** breakpoint `0x08001aa6`, sweep the TIME pot, confirm `read_ptr` used to stair-step
and is now continuous; A/B a slow LFO into TIME CV for glitch-free chorus.

### Known limitations of Patch 1 (follow-ups)
- Neighbour wrap uses buffer length only, not the loop start/end pointers — off by ≤1 sample at the
  exact loop boundary in recirc mode (inaudible; refine if needed).
- Linear interpolation is the v1 choice (cheap, ~0.5 dB HF droop near Nyquist). All-pass or Lagrange
  can drop into the same caveB later.
