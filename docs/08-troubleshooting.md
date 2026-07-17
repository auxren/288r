# 8. Troubleshooting

## Recovery: restore stock firmware

If a flash goes wrong, the module misbehaves, or you just want factory behavior back, reflash the
golden image over SWD:

```bash
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
  -c "program 'Compiled FW/B288-REV1.0.hex' verify reset exit"
```

The F429's SWD and BOOT0 ROM bootloader are independent of the application flash, so a bad image can
always be overwritten — see [Installation & flashing](02-installation-and-flashing.md). The only
unrecoverable action is setting **RDP Level 2** (option bytes); ordinary flashing never does this, so
don't change option bytes.

## Common issues

| Symptom | Likely cause / fix |
|---|---|
| **No sound at all** | Check the input pot is up and a tap slider (or the mixed out) is up. Confirm the case is powered and the module is seated. |
| **Sound at tap outs but not mixed out** | Output-mixer sliders are down, or the relevant taps are muted (mute DIPs). |
| **Delay time won't change / "always the same sound"** | Stock-firmware limitation (quantized time + PLL octave stepping). This is the exact bug the community firmware / interpolation patch fixes — see [Time & pitch](05-time-and-pitch.md). |
| **Zipper/steppy noise when modulating TIME** | Stock firmware reads whole samples. Flash the interpolation patch or the community firmware for smooth fractional reads. |
| **Static/glitch when sweeping PITCH hard** | Buffer-wrap discontinuity in PITCH mode. Being fixed with the dual-head crossfade reader (community firmware, in development). |
| **Fidelity switch seems to do nothing mid-use** | The resolution/fidelity selector is read **at boot**. Change it, then power-cycle. |
| **A preset "won't save"** | Presets aren't saved — they're live trimmer/DIP state. Set the trimmers; the A/B/C selector switches between banks instantly. |
| **Codec/SDRAM silent after flashing** | Power-cycle the case once after a flash so the codec and SDRAM re-initialize cleanly. |

## Digital "ringing" artifact

Some high-frequency ringing/aliasing character has been heard on both stock and patched firmware
during aggressive modulation. It's on the list for the community firmware (interpolation-kernel and
anti-alias work in the rewrite); it does not indicate a hardware fault.

## Getting help

- Check the [firmware project & issues](09-firmware-and-contributing.md).
- For reverse-engineering / bench details, see `re/notes/` in the repo.
- When reporting a problem, note **which image** you flashed, the **switch/knob positions**, and what
  you heard — audio clips help a lot.
