# 3. Front-panel reference

This chapter lists every control and jack and what it does. Control **functions** are documented from
reverse-engineering the firmware and the module BOM; a few exact panel *labels* are still being
confirmed against the physical unit and are noted where relevant.

## Mode / selector switches

| Control | Type | Function |
|---|---|---|
| **cal. / pre-set** | latching toggle | Selects **preset** (normal) or **cal.** In *cal.*, the module runs a live setup mode: shorter buffers, ×2 time multiplier, and the taps follow the **raw** trimmer values instead of the hysteresis-committed preset. |
| **SHORT / FULL cycle** | latching toggle | Scales every tap position 4:1 — **FULL** = long delay window, **SHORT** = quarter-length (per-unit ≈ 44 vs 11 samples). Sets the overall time range. |
| **A / B / C** (preset select) | 3-position toggle | Chooses which stored **phase row** feeds the taps: a default even spacing, or bank A / B / C. See [Presets](06-presets-taps-mixers.md). |
| **resolution** (fidelity) | rear 2-bit DIP (sw3/sw4) | Stored bit depth: **20-bit** (hi-fi), **16-bit**, or **12-bit vintage**. Fixed at boot; sets the SDRAM layout. See [Fidelity](07-fidelity-tone-settings.md). |
| **multiplier: TIME / PITCH** | mode switch | Selects how the multiplier behaves — **TIME** varies delay time (chorus/flanger), **PITCH** tracks pitch. See [Time & pitch](05-time-and-pitch.md). |

## Momentary switches (transport / gestures)

Two panel switches are spring-return (momentary): **SW14** `(ON)-OFF-(ON)` and **SW16**
`ON-OFF-(ON)`. In normal use they act as manual **write / recirculate / pulse** triggers for the
transport. In the community firmware, **holding the red write switch ~2 s** saves the current state
to the selected A/B/C preset slot — see [Presets](06-presets-taps-mixers.md).

## Continuous controls

| Control | Count | Function |
|---|---|---|
| **Output-mixer sliders** | 9 × 45 mm | Levels into the analog **mixed** sum. Slider **0** carries the always-on **dry** input feed (an analog path, not a tap); sliders **1–8** are taps 1–8. The taps also always appear at their individual outputs regardless of slider position. |
| **Rotary pots** | 7 | Input mixer / time / level controls (POT1–5 log, POT6–7 linear). The **multiplier**, **c.v. attenuverter**, and **sens.** knobs are documented in the rows below; remaining per-knob assignments are being confirmed on the unit. |
| **multiplier** knob | 1 of the 7 | Scales all tap times. In **TIME** mode the printed panel legend reads true — **0.4** at the CCW stop, **1.0** at noon, **1.6** at the CW stop — because the firmware calibrates out the pot's non-linear taper against a mark-by-mark measured curve (the stock firmware wasted the bottom fifth of the travel). In **PITCH** mode the knob sets pitch-down depth from raw pot travel instead — see [Time & pitch](05-time-and-pitch.md). |
| **c.v. attenuverter** | 1 of the 7 | Scales the **c.v. in** contribution to the multiplier: `mult = knob + CV × attenuverter`. The center detent is a dead zone (CV ignored); turning CCW inverts the CV. Active from power-up in both TIME and PITCH modes. |
| **red AUTO CONTROL — hold gesture** | — | **Hold the *next sound* side ~2 s** → the LEDs twinkle and the module toggles **String mode** (Karplus-Strong; see [Time & pitch](05-time-and-pitch.md)). The READY LED **breathes** while the mode is active; the same hold exits. A short flick keeps its normal next-sound meaning. |
| **sens.** | 1 of the 7 | An analog attenuator on the level-detect input (one of the codec's ADC channels); its job follows the red AUTO CONTROL switch. **All sounds:** currently no function (the envelope→delay-time self-modulation is disabled as of v1.2.2 pending its redesign — see the manual's Time & pitch chapter). **Center / next sound:** auto-capture **threshold** — fully CCW disables auto triggering (LED dark); raising it lets quieter material trigger. |
| **Preset trimmers** | 36 (4 banks × 9) | Program the four **preset audio outs**: each bank is an all-analog mix (8 taps + master level) permanently summed to its own output jack — four simultaneous, differently-composed views of the delay, independent of the sliders and of firmware. (The MCU never reads these trimmers — verified.) See [Presets](06-presets-taps-mixers.md). |
| **Tap-time DIPs** | 6 × 8-position | Stock: binary tap-time presets in **10 ms** steps. Not read by the community firmware (see note below). |
| **Phase-invert DIPs** | 4 × 5-position | Per-tap phase inversion in the mix. |
| **Mute DIPs** | 4 × 4-position | Stock: per-tap mute in the mix. Not read by the community firmware (see note below). |
| **Config DIP (rear)** | 1 × 4-position | The four settings straps, read at boot: **sw1** ×10 range extend, **sw2** bandwidth limit, **sw3/sw4** resolution. See [Settings](07-fidelity-tone-settings.md). |

> **Community firmware — front DIP matrix.** The tap-time and mute DIP rows (the front DIP matrix)
> are **never read** by the community firmware; everything they did on the stock firmware is covered
> by the save-chord presets (see [Presets](06-presets-taps-mixers.md)). The only settings are the
> four rear config-DIP straps (see [Settings](07-fidelity-tone-settings.md)). The per-tap phase
> switches act in the analog mixed path and keep working.

> **Unit-specific note — slider 5.** On the specific unit this firmware was developed against,
> slider **5** is dead. Tap 5 is present and correct on the codec's TDM bus (verified by soloing
> each DAC slot over SWD), but the signal reaches no slider in any phase-switch position — a broken
> analog path on that board (check that AOUT net: solder joint, coupling cap, buffer op-amp
> section). This is a hardware fault on that one unit, not a firmware limitation; the firmware
> keeps the identity mapping (slider N = tap N) so the panel legend stays honest.

## Jacks & indicators

- **Input(s)** — audio (and CV) into the input mixer. The codec provides 4 inputs.
- **Tap outputs** — **8 individual outputs**, one per tap (each tap has its own DAC channel).
- **Mixed out** — the analog op-amp sum of the taps, scaled by the output-mixer sliders.
- **Pulse in / Arm pulse in** — trigger/sync inputs for the transport (record/recirc, cycle sync).
- **Banana jacks** — Buchla-format CV/pulse interconnect.
- **LEDs** (5) — status/activity indicators. Two are worth knowing by name:
  - **Input-mixer LED** — a whole-chain **clip** indicator: it lights for ~¼ s when the input ADC
    rails *or* any tap output would exceed full scale ahead of the output limiter. Gain-staging
    recipe: raise the input-mixer level until the LED lights, then back off until it stays dark.
    (The stock behavior — a simple half-full-scale input comparator — is available by building with
    `LED_INPUT_CLIP_MODE 0` in `board.h`.)
  - **AUTO CONTROL LED** — lights only while incoming audio is above the threshold set by the
    **sens.** knob; it is the same comparison that fires the automatic capture. With sens. fully
    CCW the LED stays dark and auto triggering is disabled.

> **Note on "no menu."** There is no menu system. On the stock firmware the presets were *physical* —
> trimmers and DIP switches read live every scan, with nothing to save or recall. The community
> firmware keeps the panel-first design but makes the presets **savable**: hold the red **write**
> switch ~2 s to store the current state to the selected A/B/C slot, persisted in internal flash
> (see [Presets](06-presets-taps-mixers.md)). The only settings are the four rear DIP switches
> (see [Settings](07-fidelity-tone-settings.md)).


> **Boot alarm:** if every indicator LED flashes rapidly for ~3 seconds at power-on, the codec
> failed to initialize after five verified attempts — audio I/O is down (dry still passes on the
> analog path). This should never happen in normal use; if it does, power-cycle and, if it
> repeats, treat it as a hardware fault (I²C bus / codec).
