# Overnight report — 2026-07-18 (bench session 6, autonomous)

*You went to bed with: sliders 0+4 up, solid signal in, mixer A full, 266 random CV →
c.v. in (atten full up), multiplier full CCW, TIME, ×1, preset A, store beg., all
sounds, red momentary centered, short cycle. Unit powered, SWD attached.*

## The short version
**The firmware is feature-complete against the stock**, every mechanism is either
proven on the wire or decompile-verified by adversarial agents, and the unit soaked
all night on the final build with health monitoring. The evening's "it keeps getting
worse" had two real causes, both closed: a stray CV patched into c.v. in (your
catch), and me wiring unverified inferences into your only build (my failure —
fixed with the stability rule: proven core locked, inferences gated until proven).

## What landed overnight (all committed, 22/22 test suites)
1. **Attenuverter law enabled — proven.** The observer caught your c.v.-knob sweep
   on the parked ADC3 channel (7→4085). The stock control law is live:
   `delay time = knob + CV × attenuverter`. Watched it track the 266 live all night.
2. **Stock pitch mode** (adversarially verified against the decompile): in pitch,
   delay pins to minimum, the multiplier knob = pitch-down depth (−1 st FULL /
   −5 st SHORT), CV bends ±1.2 V/oct through the same attenuverter. (The dig also
   found the stock's own pitch is half-broken — a counter-reuse bug kills path B.
   Ours has no such bug and no wrap-click.)
3. **Store beg./end done right**: it's a latching *policy* switch (bit 6). store
   beg. = auto-loop at cycle end. store end = the take is **held** while the delay
   keeps running; RECIRC recalls it whenever you want.
4. **Pulse input jacks wired**: write (PG10), recirc (PG11), arm/next-sound (PG12)
   — gates into them act exactly like the switches.
5. **Knob span calibrated**: full-CCW used to read 16% (644/4095); now true 0.
6. **Pitch branch formally merged**, flasher bundle refreshed, RE notes corrected
   (big addendum in `re/notes/panel-scan.md`), new host test for the hold/recall
   window logic.

## Facts that ended the mysteries (verified)
- The stock reads the DIP matrix **once at boot**, then parks the 595 at 0x777777
  forever. The parked analog channel = the **c.v. attenuverter**. The 36 trimmers
  are analog-only — the MCU never reads them (so no firmware work ever pending).
- The matrix words we thought were garbage are the real B/C/D preset-bank switches
  (read at boot; stock also requires a reboot to see changes).
- PA0/1/7/8/11 = the five indicator/pulse outputs (input, write, recirc, ready,
  presence) — all driven, all owner-verified.

## Your 10-minute wake-up checklist (in your current patch state)
1. **Knob**: full CCW → delay time follows the 266's CV alone; sweep up → knob adds.
2. **Attenuverter**: turn it to **center** → the 266 stops affecting delay; **CCW**
   → the CV *inverts*. (First time this knob has ever worked.)
3. **Pitch**: TIME/pitch → pitch. Knob CCW = clean; raise knob = pitch bends DOWN
   (this is the stock behavior — depth knob), CV wobbles it. Back to TIME.
4. **Store end**: black switch → store end, red AUTO → center, play a phrase → LEDs
   show write then write+ready (held). Hit recirc later → the phrase loops.
5. **Pulse in**: patch a gate into the write or recirc pulse jack → acts like the
   momentary.
6. Then the full `docs/release-test-plan.md` (v2 addendum included) when you're
   ready to gate the release tag.

## Soak results — PASSED
- **6 h 07 m continuous** (01:16–07:23), 728 health samples, **zero stalls, zero faults**
- Block clock: 3000–3005/s the entire night (mean 3000 — rock steady)
- Control chain: multiplier tracked the 266's random CV across its full range
  (mult 0.050–0.910, cv 203–3734) — thousands of smooth modulation excursions
- 3 logger reconnects, all my own flash/restart cycles — none from the unit
- Dawn check: unit alive, same build, still tracking (mult=0.35 @ cv=1427)
