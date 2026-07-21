# 6. Presets, taps & mixers

On the stock firmware the 288r's "presets" were not saved files — they were **physical trimmers and
DIP switches read live**. The community firmware keeps the panel-first design but makes the presets
**savable A/B/C slots**, persisted in internal flash. This chapter explains how the taps are placed
and mixed.

## The A / B / C preset slots

The panel has four columns of nine trimmers each — the stock firmware's **preset banks**. The
community firmware does not read the trimmers; instead, each position of the **A / B / C** selector
recalls a **saved slot** holding a full tap pattern (per-tap **phase** on the printed **0–160**
scale, plus the multiplier):

| Selector | Tap placement source |
|---|---|
| **A** | Slot A |
| **B** | Slot B |
| **C** | Slot C |

An empty slot falls back to the default even spacing (20, 40, 60 … 160).

**To save:** hold the red **write** switch for ~2 s — the LEDs twinkle to confirm — and the current
pattern is stored to the selected slot. Presets persist across power cycles. **On recall**, the
stored multiplier is **pinned**: the physical knob is ignored until you sweep it through the stored
value, so a recalled preset never jumps to match the panel.

> **What presets can and cannot recall.** Presets store the *digital* controls: time
> multiplier, ×1/×2, cycle — and in string mode, the chord. They **cannot** store or recall
> the sliders, input mixer knobs, or phase switches: those are pure analog controls with no
> electronic path back into the firmware (true on every firmware, including stock). If a
> recalled preset "keeps the current slider pattern" — that's why.

## Tap placement math

Each tap reads the delay memory at:

```
tap distance = base_time × (phase[tap] / 160) × multiplier
```

- **phase[tap]** — the active preset slot's value for that tap (0–160).
- **base_time** — the base delay window, scaled by the **SHORT/FULL cycle** switch (FULL = full
  window, SHORT = quarter) and the rear **×10 range** DIP. (On the stock firmware this also came
  from the **tap-time DIPs**, which the community firmware never reads.)
- **multiplier** — the front-panel multiplier (see [Time & pitch](05-time-and-pitch.md)).

So the *preset* sets the pattern shape; the *cycle switch and multiplier* scale the whole pattern in
time.

## Phase-invert & mute

Per-tap **phase switches** flip a tap's polarity in the analog **mixed** sum — inverting some taps
turns a plain multitap into a comb/allpass-like timbre and changes how taps reinforce or cancel.
(In PITCH mode the taps are deliberately decorrelated so an inverted pair combs instead of
cancelling — see [Time & pitch](05-time-and-pitch.md).)

The **front DIP matrix** — the stock firmware's tap-time and mute rows — is **never read** by the
community firmware (see [Settings](07-fidelity-tone-settings.md)); what it did on the stock is
covered by the preset slots. To drop a tap from the mix, pull its slider down. The individual tap
outputs still carry every tap.

## The output mixer

Nine **45 mm sliders** set the levels in the analog **mixed** output: slider **0** carries the
always-on **dry** input feed (an analog path, not a tap); sliders **1–8** are taps 1–8.
The mix is a real op-amp sum of the eight DAC channels — there is no digital master bus — so any
digital tone coloration happens per tap or on the record path, never on a master bus (see
[Fidelity & tone](07-fidelity-tone-settings.md)).

**Two ways to use the outputs:**

- **Mixed out** — the summed, slider-balanced blend. Best for a single stereo/mono delay voice.
- **8 tap outs** — each tap on its own jack, full level, independent of the sliders. Best for
  multichannel spatialization, feeding taps to different processors, or Buchla-style patch-programming.

## The preset audio outs (A–D)

The four **preset out** jacks are all-analog and always live: each carries its trimmer bank's
fixed mix of the eight tap channels (8 tap levels + a master, per bank). They ignore the
sliders entirely — the sliders shape the *mixed out*; the preset outs are four pre-composed
alternatives running simultaneously. Program one bank as a tight slapback, another as a long
wash, another as a rhythmic comb, and address them as separate sources in your patch. In pitch
mode they carry the transposed echo pattern like everything else — so a preset out is also the
easy way to hear the pitch voice with all sliders down.

*(Practical notes: because they bypass the sliders, "audio with every slider down" on a preset
out is correct behavior, not a fault. And when checking panel behavior, always note **which
jack** you are monitoring — the output section has four different kinds.)*

## AUTO CONTROL

**AUTO CONTROL** is the automatic capture: with the red transport switch in its center (auto)
position, the transport triggers when incoming audio exceeds the threshold set by the **sens.**
knob. sens. is an analog attenuator on the level-detect input — raising it lets quieter material
trigger; fully counter-clockwise disables auto triggering. The **AUTO CONTROL LED** lights while
the signal is above the threshold — the same comparison that fires the capture — so the LED shows
exactly what will trigger. See [Operation](04-operation.md).
