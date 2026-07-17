# 4. Operation

## The basic patch

1. Patch audio into an **input**; bring up the relevant **input pot(s)**.
2. Choose a **preset bank** (A/B/C) and the **SHORT/FULL cycle** length.
3. Raise a few **output-mixer sliders** to hear individual taps, or take the **mixed out** for the sum.
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
The community firmware smooths the record→recirc handoff so loop boundaries don't click.

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
jumps; that's the limitation this project set out to fix. See [Time & pitch](05-time-and-pitch.md)
for the details and for **PITCH** mode.
