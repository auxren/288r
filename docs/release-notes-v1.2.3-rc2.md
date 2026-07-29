# 288r community firmware — v1.2.3-rc2 (pre-release for field testing)

**Updating preserves your saved presets.** Supersedes rc1.

## New features

- **Overdub / sound-on-sound**: while a loop plays, **hold the recirc momentary** —
  everything at the input layers into the loop for as long as you hold. Existing material
  fades gently (~5%/pass) under new layers; a soft ceiling lets dense stacks bloom instead
  of clipping; the full-precision buffer means layer forty sounds as clean as layer one.
  Write + recirc LEDs light together while held, and the auto re-arm is suspended so a
  loud hit can't steal your loop mid-gesture. Composes with varispeed tape-style: layer
  onto a slowed loop, speed it back up, your layer comes back pitched.

## Fixed

- (rc1's min-statistics capture floor carries forward: staccato keeps full sensitivity,
  loop bleed absorbed — #13 verdict pending.)

## Known / open issues

- Deeper presets (#5); #10/#13 detector verdicts pending; FM feel-calibration input
  welcome. Next up this cycle: split-tap loop+live, stereo master, knob-steered feedback.
