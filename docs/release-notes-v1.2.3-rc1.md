# 288r community firmware — v1.2.3-rc1 (pre-release for field testing)

**Updating preserves your saved presets.** First rc of the post-v1.2.2 cycle.

## New features

- (none — targeted fix release)

## Fixed

- **The capture threshold no longer "re-zeroes" against your playing**
  ([#13](https://github.com/auxren/288r/issues/13)): v1.2.2's ambient baseline averaged
  everything it heard — including the performance — so repeated staccato ratcheted the
  threshold up until sensitivity effectively disabled. The ambient floor is now the
  envelope's **minimum over a sliding ~1.5 s window** (what the level dips to *between*
  notes): staccato keeps full sensitivity at any tempo with gaps, the write/recirc stutter
  works, and continuous loop-playback bleed still becomes the floor a new sound must
  clearly exceed. A 12-hit staccato train is now a permanent regression test.

## Known / open issues

- Deeper presets ([#5](https://github.com/auxren/288r/issues/5)); FM feel-calibration
  input welcome; #10/#13 detector verdicts pending on this build.
- In development this cycle: overdub/sound-on-sound, split-tap loop+live, stereo master,
  knob-steered internal feedback.
