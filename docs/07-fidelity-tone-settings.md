# 7. Fidelity, tone & settings

## Fidelity (bit depth)

The two rear-panel **resolution** DIP switches set the bit depth the delay memory stores audio at.
They're read **at boot** because they also set how the SDRAM buffer is laid out.

| Setting | Depth | Character | Buffer |
|---|---|---|---|
| hi-fi | **20-bit** | cleanest | one long bank |
| normal | **16-bit** | clean | one long bank |
| **vintage** | **12-bit** | lo-fi grit, quantization character | short → allows two banks (see below) |

**Why boot-time:** the 8 MB SDRAM holds ~43 s mono at 16-bit. Choosing a narrower/wider sample width
changes how much time fits and whether the buffer is split into two banks, so the layout is fixed once
at startup. Change the switches and power-cycle to switch fidelity.

**Vintage two-bank mode:** in the community firmware, selecting **vintage** can split the buffer into
**two shorter banks** — a main delay bank and a **recirc/loop bank** — so you can run a delay and a
loop region at once. (Bank_B = the recirc/loop path, matching the stock design.)

## Analog tone (community firmware, opt-in)

The community firmware ships **neutral/bit-honest by default** (faithful clone first). The shipped
tone control is the **bandwidth-limit** rear DIP (sw2, below): a fixed one-pole low-pass at ~11 kHz
that darkens the whole voice, tape-style.

Deeper analog-voice elements exist in the codebase (host-tested) but are not yet wired to any
control — the module runs clean unless you rebuild with them enabled:

- **HF roll-off** — a one-pole low-pass per repeat in the feedback path (validated: high-frequency
  energy above 6 kHz drops ~42 % → ~18 %, a tape-like darkening).
- **Soft saturation** — gentle waveshaping so feedback compresses/warms instead of clipping harshly.
- **Wow/flutter** — subtle time modulation for tape-like instability *(planned)*.

## Output stage & patched feedback

The outputs pass through a **soft-knee limiter**: transparent below 0.75 of full scale, then a
smooth knee that approaches full scale asymptotically. An external feedback loop patched at greater
than unity gain (measured at 1.15×) settles at 0.877 of full scale with **zero flat-topped samples**
— tape-style bloom instead of digital clipping. Feedback is **external patching only** by design;
there is no internal feedback path.

One physical note: the loop round trip through the codec and block buffering is ~1 ms. That's
irrelevant for ordinary delay regeneration, but it sets a ~1 kHz comb floor for super-tight patched
flanger loops — same physics as the stock unit.

## Settings (community firmware)

The settings policy is deliberately simple: **the only settings are the four DIP switches on the
rear of the board.** They are read once at boot — set a switch, power-cycle, and it applies.

| DIP | Function |
|---|---|
| **sw1** | **×10 delay-range extend** — multiplies the whole delay-time table by ten (clamped to the buffer). |
| **sw2** | **Bandwidth limit** — a one-pole low-pass at ~11 kHz for a darker, vintage voicing. |
| **sw3 / sw4** | **Resolution** — the stored bit depth of the delay memory (see above). |

The **front-panel DIP matrix** (the tap-time, phase and mute rows) is **never read** by the
community firmware. Everything it did on the stock firmware is covered by the save-chord presets —
hold the red **write** switch ~2 s to save the current state to the selected A/B/C slot (see
[Presets](06-presets-taps-mixers.md)).

There is no settings menu and no user calibration mode. The control calibration is baked into the
firmware: the multiplier knob's curve was measured mark-by-mark against the panel legend (0.4 / 0.6
/ 0.8 / 1.0 / 1.2 / 1.4 / 1.6 read exactly true — the pot's taper is non-linear versus the print,
and the old firmware ate the bottom fifth of the travel), and the control-ADC span is stretched to
its measured endpoints so knob and CV reach their full range.

### Persistence

Presets and stored state live in the **STM32F429's internal flash** (there is no external EEPROM on
this board). The store is **versioned and checksummed** — a record with a bad magic/CRC is refused
and defaults are used, so a corrupt write can't brick the module. On recall, **control-pinning**
keeps the stored multiplier in effect until you physically sweep the knob through it, so a recalled
preset never jumps when the panel doesn't match. (This mirrors the MARF 248r persistence pattern.)

> **LEGACY ×10 notes (DIP 1 is now ×4 — see below) (by design, worth knowing):** with rear DIP 1 on, the octave
> switch saturates almost immediately (×10 already sits near the 19-second buffer ceiling, so
> ×2/×4 add little); pitch mode's three longest echoes clamp to the voice ring's 2.7 s depth
> (the late pattern bunches up); envelope→time modulation spans 10× the range (attacks can
> sweep seconds of delay — dramatic by design); presets recall *relative* times, so a slot
> saved in ×1 plays 10× longer under ×10; and every glide covers 10× the distance, so all
> moves bend pitch deeper. String mode is immune (its tuning ignores the range strap).

> **DIP 1 is now “+2 octaves” (×4), changed from the stock’s ×10 table** after field reports:
> ×10 exceeded three subsystem envelopes (octave switch saturated immediately, pitch mode’s
> late echoes clamped, looper cycles stretched to 10–40 s and felt broken). ×4 composes in
> pure octaves with the ×1/×2/×4 switch — every combination is fully delivered, and the full
> memory depth (~19 s) is reached cleanly at DIP + ×4.
