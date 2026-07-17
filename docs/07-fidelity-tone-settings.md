# 7. Fidelity, tone & settings

## Fidelity (bit depth)

A front-panel **resolution** selector sets the bit depth the delay memory stores audio at. It's read
**at boot** because it also sets how the SDRAM buffer is laid out.

| Setting | Depth | Character | Buffer |
|---|---|---|---|
| hi-fi | **20-bit** | cleanest | one long bank |
| normal | **16-bit** | clean | one long bank |
| **vintage** | **12-bit** | lo-fi grit, quantization character | short → allows two banks (see below) |

**Why boot-time:** the 8 MB SDRAM holds ~43 s mono at 16-bit. Choosing a narrower/wider sample width
changes how much time fits and whether the buffer is split into two banks, so the layout is fixed once
at startup. Change the switch and power-cycle to switch fidelity.

**Vintage two-bank mode:** in the community firmware, selecting **vintage** can split the buffer into
**two shorter banks** — a main delay bank and a **recirc/loop bank** — so you can run a delay and a
loop region at once. (Bank_B = the recirc/loop path, matching the stock design.)

## Analog tone (community firmware, opt-in)

The community firmware ships **neutral/bit-honest by default** (faithful clone first). An optional
**analog-tone** voice adds tape/BBD-style character, applied **per tap** (because the mixed output is
an analog op-amp sum — there's no digital master bus to color). Each element is independently
switchable and persisted:

- **HF roll-off** — a one-pole low-pass that darkens the sound, per repeat in the feedback path
  (validated: high-frequency energy above 6 kHz drops ~42 % → ~18 %, a tape-like darkening).
- **Soft saturation** — gentle waveshaping so feedback compresses/warms instead of clipping harshly.
- **Wow/flutter** — subtle time modulation for tape-like instability *(planned)*.

You can leave all of it off for a clean digital delay, or dial in as much character as you like.

## Settings / calibration mode (community firmware)

The stock firmware has no settings menu. The community firmware adds a **modal settings/calibration
mode** entered with a **boot chord**:

> **Hold PULSE IN + ARM PULSE IN to the LEFT while powering on** → enter settings/calibration mode.

This uses the two momentary switches (which have no runtime conflict at power-up) as a deliberate,
hard-to-trigger-by-accident gesture. Inside settings mode you can adjust parameters with a
**"hold-a-chord + turn-a-knob"** relative editor, and run the calibration routine.

### What calibration covers

Calibration measures and stores the real endpoints of the panel controls so the firmware maps them
correctly:

- **Sliders / pots** — min/max travel and taper (gain law).
- **36 trimmers** — normalize the 0–160 range to the ADC readings.
- **Time-CV range/taper** — fix the narrow-range bug so full CV swing gives full modulation.
- **AUTO CONTROL** threshold and **pulse** input thresholds.

### Persistence

Settings and calibration are stored in the **STM32F429's internal flash** (there is no external EEPROM
on this board). The store is **versioned and checksummed** — a record with a bad magic/CRC is refused
and defaults are used, so a corrupt write can't brick the module. On recall, **control-pinning** keeps
a stored value in effect until you physically sweep the live control through it, so a recalled setting
never jumps when it doesn't match the panel. (This mirrors the MARF 248r persistence pattern.)

> **Presets are not persisted** — they're live panel state (see [Presets](06-presets-taps-mixers.md)).
> Only *settings and calibration* live in flash.

Exact gesture labels, the full settings list, and calibration constants are being finalized against
the physical unit; items dependent on bench measurement are marked in the firmware and DESIGN.md.
