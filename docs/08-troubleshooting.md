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
| **Sound at tap outs but not mixed out** | Output-mixer sliders are down. |
| **Delay time won't change / "always the same sound"** | Stock-firmware limitation (quantized time + PLL octave stepping). This is the exact bug the community firmware / interpolation patch fixes — see [Time & pitch](05-time-and-pitch.md). |
| **Zipper/steppy noise when modulating TIME** | Stock firmware reads whole samples — flash the interpolation patch or the community firmware for smooth fractional reads. Community firmware before v1.0.1 also had a residual broadband zipper from a too-fast control slew (knob, CV, any source); v1.0.1 glides the taps over ~10 ms and modulation is clean. |
| **Static/glitch when sweeping PITCH hard** | Stock-firmware artifact. The community firmware's pitch mode splices with correlation-aligned crossfades and is clean — see [Time & pitch](05-time-and-pitch.md). |
| **Fidelity switches seem to do nothing mid-use** | The rear resolution DIPs are read **at boot**. Change them, then power-cycle. |
| **A preset "won't save"** | A tap of the switch isn't a save — **hold** the red write switch for ~2 s with the A/B/C selector on the slot you want; the LED twinkle confirms the save. Presets persist in internal flash across power. |
| **Tight patched flanger loop sounds comby/metallic** | The external loop's round trip through the codec and block buffering is ~1 ms, which puts a ~1 kHz comb floor under super-tight feedback loops. Physics, same as stock; irrelevant for ordinary delay regeneration. |
| **Codec/SDRAM silent after flashing** | Power-cycle the case once after a flash so the codec and SDRAM re-initialize cleanly. |

## The input mixer LED is a clip indicator

In the community firmware the **input mixer** LED lights for about ¼ s when the input ADC rails
**or** any tap output would exceed full scale before the output limiter — a whole-chain clip
indicator, not just an input meter. Use it for gain staging: raise input mixer A until the LED
lights, then back off until it stays dark. If you prefer the stock behavior (a comparator at half
full scale on the input), rebuild with `LED_INPUT_CLIP_MODE 0` in `firmware/src/bsp/board.h`.

## "PITCH seems to do nothing"

Work through this list:

1. **Cycle switch at FULL?** At FULL the knob's maximum depth is only about −1 semitone — a subtle
   detune, easy to miss. Flip cycle to SHORT (about −4.75 st max) to hear it plainly.
2. **Anything patched into c.v. in?** Pitch CV goes through the **attenuverter** just like TIME CV,
   so a patched signal with the attenuverter up adds to (or overrides) the knob. Unpatch it, or set
   the attenuverter to its center detent (CV ignored).
3. **Listening on the right slider?** Slider 0 is the MASTER (a parallel sum of all eight tap channels) — it carries the
   shifted voice — listen on sliders 1–8 (the taps).
4. **Knob at the very bottom?** The bottom ~2 % of the knob's travel deliberately snaps to exact
   unity — a clean bypass that kills residual detune-beating. No shift there is by design.

## A slider is silent (dead-slider triage)

1. Flip that tap's **phase switch** through its positions. If the tap never appears in *any*
   position while the other sliders work, the digital side is probably fine and the fault is in the
   analog path for that output.
2. *(Bench, SWD)* With a debugger attached, use the `g_dac_solo` walk: write the `g_dac_solo` byte
   (−1 = off, 0–7 = solo one codec slot) and step through the slots to hear which slider carries
   each one. A slot that is hot on the TDM bus but reaches no slider in any phase-switch position is
   an analog fault — check that AOUT net (solder joint, coupling cap, buffer op-amp section). See
   `docs/bench-runbook.md`.

**Known fault on the project's reference unit:** slider 5 is dead. Codec slot 4 is hot on the TDM
bus but reaches no slider in any phase-switch position — a broken analog path on the board, not a
firmware issue. The firmware keeps the identity mapping (slider N = tap N) so the panel legend stays
honest, and `g_dac_solo` stays in the build for post-repair verification.

## Digital "ringing" artifact

Some high-frequency ringing/aliasing character has been heard on both stock and patched firmware
during aggressive modulation. It does not indicate a hardware fault. The community firmware's
Hermite interpolation was measured for exactly this case (transparent under modulation); if you
hear it on v1.0.1, please report it with an audio clip.

## Getting help

- Check the [firmware project & issues](09-firmware-and-contributing.md).
- For reverse-engineering / bench details, see `re/notes/` in the repo.
- When reporting a problem, note **which image** you flashed, the **switch/knob positions**, and what
  you heard — audio clips help a lot.
