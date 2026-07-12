# Compiled firmware images

| File | What it is |
|------|------------|
| **`B288-REV1.0.hex`** | The **stock** shipped 288r firmware. Unmodified. **This is your restore image** — flash it to go back to normal. |
| **`B288-REV1.0-interp-EXPERIMENTAL.hex`** | ⚠️ **EXPERIMENTAL, UNTESTED ON HARDWARE.** Stock firmware **+ the interpolation patch**: adds fractional/interpolated delay taps so delay-time modulation is smooth (chorus/flanger) on both audio paths. Everything else is unchanged. |

## ⚠️ Read before flashing the experimental image

- It has been **assembled, spliced, and re-verified in the disassembler, but never run on real
  hardware.** You would be the first to test it. Please report results — see
  [`../docs/validation-checklist.md`](../docs/validation-checklist.md) and open a
  "Release validation (bench)" issue.
- **You can't brick the unit** by flashing: SWD + `B288-REV1.0.hex` always restores it. But
  **never touch the RDP / option bytes** (RDP Level 2 is the only true brick).
- Experimental firmware can output **loud / DC** signal — keep monitor levels **low** while testing.
- **Back up your unit's current firmware first** (dump `0x08000000`) before flashing anything.

## Flash / restore (SWD, ST-Link)

```bash
# flash the experimental image
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
  -c "program 'B288-REV1.0-interp-EXPERIMENTAL.hex' verify reset exit"

# restore stock
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
  -c "program B288-REV1.0.hex verify reset exit"
```
(STM32CubeProgrammer / ST-Link Utility work too.)

## Reproduce / verify it yourself
The experimental image is deterministically generated from the stock hex by the patch tooling —
regenerate and diff to confirm exactly what changed (6 code-cave detours + a 184-byte cave):
```bash
re/.venv/bin/python re/scripts/apply_patch1.py   # -> re/patches/patched.hex
```
Details: [`../re/patches/README.md`](../re/patches/README.md).
