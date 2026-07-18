# 4. Operation

## The basic patch

1. Patch audio into an **input**; bring up the relevant **input pot(s)**. To gain-stage, raise the
   input level until the **input-mixer LED** (the whole-chain clip indicator) lights, then back off
   until it stays dark.
2. Choose a **preset bank** (A/B/C) and the **SHORT/FULL cycle** length.
3. Raise a few **output-mixer sliders** to hear individual taps — sliders **1–8** are taps 1–8;
   slider **0** is the always-on **dry** input feed — or take the **mixed out** for the sum.
4. Set the **multiplier** for the delay time you want; use **TIME** mode to modulate it (see
   [Time & pitch](05-time-and-pitch.md)).

Each of the eight taps is also present at its **own output jack** all the time — patch those
separately for multichannel / spatial use, independent of the mixer sliders.

## Delay vs. looper (the transport)

The 288r continuously records audio into its delay memory. The **transport** decides whether it keeps
recording or freezes a region and loops it:

- **WRITE (delay)** — the write head keeps moving; the taps read behind it. This is a normal multitap
  delay: new audio constantly replaces the oldest.
- **RECIRC (looper)** — the module stops advancing the write head into new memory and instead
  **recirculates** a captured window, optionally feeding it back on itself. The taps now read from a
  frozen loop, so you get a repeating phrase you can process with the taps and mixer.

The **momentary switches** and **pulse inputs** drive these transitions (manually or from a clock).
Automatic capture (the red transport switch in its center position) is keyed by the **sens.** knob:
it triggers when incoming audio exceeds the sens. threshold. The **AUTO CONTROL** LED lights while
the signal is above that threshold — the same comparison that fires the capture — and with sens.
fully CCW auto triggering is disabled. The community firmware smooths the record→recirc handoff so
loop boundaries don't click.

## Feedback

In looper/recirculate operation the module can feed the delayed signal back into the write path,
building up repeats. The community firmware puts a gentle **high-frequency roll-off and soft
saturation** in the feedback path (opt-in "analog tone"), so repeats darken and settle instead of
building up harshly — tape/BBD-style. See [Fidelity & tone](07-fidelity-tone-settings.md).

## The taps

All eight taps read the *same* recorded audio, but at different distances behind the write head:

```
tap distance = base_time × (phase[tap] / 160) × multiplier
```

- **phase[tap]** comes from the active preset bank's trimmer for that tap (0–160).
- **base_time** is set by the tap-time DIPs and the SHORT/FULL cycle switch.
- **multiplier** is the front-panel multiplier (TIME or PITCH mode).

So a preset defines the *pattern* of the eight taps (a chord, a rhythm, a comb), and the multiplier
scales the whole pattern in time. Phase-invert and mute DIPs, plus the mixer sliders, shape how those
taps combine at the mixed output.

## Modulating the time (the headline feature)

Sweeping the multiplier (by hand or with CV) in **TIME** mode moves all taps together. Because the
community firmware reads the taps at **fractional** positions, that sweep is smooth — which is exactly
what makes **chorus, flanger and doppler/tape** effects possible. On the stock firmware the same
sweep stepped in whole samples and "zippered," and coarse changes retuned the audio clock in octave
jumps; that's the limitation this project set out to fix.

In TIME mode the multiplier knob's printed marks read true — **0.4** at the CCW stop, **1.0** at
noon, **1.6** at the CW stop. CV modulation goes through the **c.v. attenuverter**:
`mult = knob + CV × attenuverter`, so the attenuverter must be off its center detent (a dead zone
where CV is ignored) for CV to have any effect, and turning it CCW inverts the sweep. The same
attenuverter also scales the pitch CV in **PITCH** mode. See [Time & pitch](05-time-and-pitch.md)
for the details and for **PITCH** mode.
