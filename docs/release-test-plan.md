# b288 community firmware — v0.9 release validation plan

Run top to bottom on the unit with the current `build/fw/b288-community.hex` flashed.
Mark each ☐. **Release gate:** every test in sections 1–8 must PASS; sections 9–10 are
record-the-result (known-provisional features). If anything in 1–8 fails, we fix and
re-run that section before tagging.

**Setup:** module powered in the case; a signal source you can start/stop (sustained
tone AND some plucky/percussive material is ideal); listening on **mixed out**; input
mixer A up; output sliders 0, 3, 8 up; multiplier at noon; red AUTO CONTROL switch at
**"all sounds"**; cal./pre-set at **pre-set**; selector at **A**; ×1/×2 at **×1**;
cycle at **full**; rear DIPs all OFF.

---

## 1. Boot & audio path
- ☐ **1.1** Power-cycle the module. Within ~2 s of power-on, audio passes and the
  delay taps echo. No stuck notes, no silence, no noise burst.
- ☐ **1.2** Let it sit 10 minutes with signal. No dropouts, resets, or degradation.

## 2. Multiplier knob + Time-CV
- ☐ **2.1** Knob at noon ⇒ echoes at the "1.0" spacing. Sweep slowly full CCW→CW:
  delay time glides **smoothly** (pitch-bend character, no zipper, no steps).
- ☐ **2.2** At full CW: delay is long but **clean** (no distortion/garbling).
- ☐ **2.3** Patch a slow LFO/CV into **c.v. in**: delay time follows it smoothly —
  chorus/flanger movement, the headline feature. Remove CV: knob takes back over.

## 3. ×1/×2 switch
- ☐ **3.1** Flip ×1→×2: delay time audibly **doubles**, smoothly (no click/glitch).
  Flip back: halves. Labels match direction (×2 = longer).

## 4. Cycle switch (short / mid / full)
- ☐ **4.1** Each of the three positions changes the delay window: full = longest,
  short = quartered. Direction matches the panel labels. Transitions are smooth.
  *(If direction is inverted, note it — one-line fix, not a redesign.)*

## 5. Transport (red write/recirc momentary) + indicator LEDs
- ☐ **5.1** At rest: **write LED on** (unit is writing = normal delay).
- ☐ **5.2** Flick toward **recirc.**: audio freezes into a loop of the current
  buffer; write LED goes off; the **end-of-cycle indicator blinks at the loop rate**.
- ☐ **5.3** Flick toward **write**: live delay resumes; write LED back on; blinking
  stops.
- ☐ **5.4** Loop length follows the cycle switch (recirc with cycle at short = a
  quarter-length loop).

## 6. Level LEDs
- ☐ **6.1** **Input mixer LED = CLIP indicator** (repurposed by owner request):
  dark during clean playing; lights (~¼ s hold per hit) when the input ADC hits
  the rail **or** any tap output would exceed full scale pre-limiter. Validate:
  crank mixer A into a hot source → LED lights; back it off until the LED just
  stops → that's the clean gain-staging point. (Stock >½ FS level-comparator
  behavior is preserved behind `LED_INPUT_CLIP_MODE 0` in board.h.)
- ☐ **6.2** **Auto-control LED** lights while signal is present (envelope >¼ FS)
  and goes dark shortly after the signal stops.

## 7. Presets (the new feature)
- ☐ **7.1 Save:** knob full CCW → **hold the write side ~2 s** → all indicator LEDs
  **twinkle** (~1 s). A brief audio hiccup during the save is expected (flash write).
- ☐ **7.2 Second slot:** selector to **B**, knob full CW → hold write 2 s → twinkle.
- ☐ **7.3 Recall:** flip selector A ↔ B: the delay time jumps between the two saved
  settings, regardless of where the knob sits.
- ☐ **7.4 Knob catch:** after a recall, sweep the knob slowly — it "catches" as it
  nears the saved value and follows your hand from there (no value jumps).
- ☐ **7.5 POWER-CYCLE PERSISTENCE:** power the module off, wait 5 s, power on.
  Selector A ⇒ the CCW setting; B ⇒ the CW setting. **Saves survived the reboot.**
- ☐ **7.6 cal./pre-set:** with A/B saved differently, flip to **cal.** ⇒ the fixed
  evenly-spaced tap pattern; back to **pre-set** ⇒ the selected slot again.

## 8. Rear DIPs (boot-time straps — power-cycle after each change!)
- ☐ **8.1 Resolution (sw3/sw4):** all off = clean 24-bit. sw3 on + power-cycle =
  audibly crunchy (12-bit). Back off + power-cycle = clean again.
- ☐ **8.2 ×10 extend (sw1):** sw1 on + power-cycle = delay times ~10× longer at the
  same knob position. Off + power-cycle = normal. *(Newly mapped — first live test.)*
- ☐ **8.3 Bandwidth (sw2):** sw2 on + power-cycle = highs rolled off (~11 kHz).
  *(Tentative pin — if nothing changes, record it; not a release blocker.)*

---

## 9. Auto mode — record the result (provisional)
- ☐ **9.1** Red AUTO CONTROL to **center**, no signal: unit sits armed ("ready").
- ☐ **9.2** Start a phrase: it should auto-punch into WRITE (write LED), record one
  cycle, then **auto-loop it** (end-of-cycle blinking) without touching anything.
- ☐ **9.3** "next sound" momentary: re-arms/captures the next phrase.
- Write down what actually happens — this is our reconstruction of the stock auto
  behavior and may need tuning (threshold, timing), not correctness fixes.

## 10. Known limitations in v0.9 (verify they're the ONLY gaps)
- Front tap-time DIP matrix + phase/mute DIPs: **never read — DESIGN LOCKED (owner
  decision 2026-07-18): only the 4 rear DIPs are settings; everything the front
  DIPs did on the stock is covered by the save-chord presets.**
- store beg./end switch: decoded, not yet acting.
- TIME/pitch switch: decoded, function lands with the pitch voice (v1.x, branch ready).
- 36 trimmers: scanned (values visible over SWD), not yet applied to parameters.
- Sliders/pots on the output mixer: analog, unaffected by firmware (should all work).

---

**When 1–8 are all ☑:** tell me "sections 1–8 pass" (plus any notes from 9–10) and I
tag `v0.9` — CI builds and publishes the release with the one-click flasher zip.


---

# v2 addendum — features landed after the first pass (bench 6 / overnight)

- ☐ **A1 attenuverter (⊖/⊕):** CV patched: knob **center = CV ignored**, CW = CV adds
  to delay time, CCW = CV **subtracts/inverts**. (Stock law, channel proven live.)
- ☐ **A2 pitch mode (stock semantics):** TIME/pitch → pitch: delay pins to minimum;
  the **multiplier knob now sets pitch-down depth** (up to ~1 st FULL / ~5 st SHORT
  cycle); CV bends bipolar at 1.2 V/oct scaled by the attenuverter. Knob CCW + no
  CV = transparent. Flip back to TIME: knob returns to delay-time duty.
- ☐ **A3 store end (hold):** black switch at **store end**, red AUTO at center:
  play a phrase → it records one cycle → **write+ready LEDs together** (held, delay
  keeps running) → hit **recirc** any time later → the held window loops.
- ☐ **A4 pulse inputs:** a trigger/gate into the **write** or **recirc** pulse jacks
  acts exactly like the momentary; a pulse into **arm** starts a next-sound capture.
- ☐ **A5 persistence re-check:** presets (now also capturing ×1/×2 + cycle) survive
  a power cycle; recalled switch values yield to the physical switch on first move.
- ☐ **A6 sens knob (BOUND — owner sweep 2026-07-18, codec slot 1):** the auto
  LED lights only while audio exceeds the sens threshold: sens full CCW → LED
  dark, always; raise sens → LED tracks the signal. Red AUTO at center: sens at
  zero → auto-trigger never fires; raise sens → quieter sounds trigger capture.
  ("signal in" is NOT a codec slot — it reaches the multiplier via the analog
  Time-CV net; locate it with a knob-wiggle while watching the cv raw value.)
- ☐ **A7 clip LED:** with the current patch (mixer A full into a hot source) the
  input LED should sit lit; lower mixer A until it goes dark → delays clean up.
