# 288r community firmware — v1.2.3-rc6 (pre-release for field testing)

**Updating preserves your saved presets.** Supersedes rc5 (all rc3–rc5 fixes included).
This rc completes the #24 line: after rc5 fixed modulation through unity, deeper CV
sweeps still clicked at fixed points — this build removes those too.

## New features

- (None — see rc3/rc4/rc5 for the overdub v2 line, tape-motor multiplier, seam/LED
  fixes, and the continuous-through-unity pitch modulation.)

## Fixed

- **#24, deep-sweep clicks**: with the attenuverter well past noon, a slow envelope
  sweeping the pitch ratio clicked deterministically at specific points — the
  anti-aliasing filter's internal band edges (ratio 1.40 / 2.00 / 2.83), where the
  filter previously *switched* coefficient tables (and briefly fell back to slow
  flash tables while re-publishing, costing a CPU spike at exactly the wrong
  moment). Both mechanisms are gone: the filter's coefficients now **morph
  continuously** as the ratio moves (published in 2% slivers — measured 50× finer
  than the old swaps — via double-buffered fast memory, so a valid table is always
  live and the flash fallback no longer exists). There is no switch left anywhere in
  the pitch-modulation path: unity, engage, and every band edge are all continuous.

## Known / open issues

- Bench verification of this build is pending (host-verified + regression-tested;
  the reporter's deterministic 281e repro is the definitive test — that's this rc).
- If captures sound crunchy, check the **input clip LED** while recording. Sens
  range guidance is in manual ch. 6.
- Deeper presets (#5). Next up: split-tap loop+live, stereo master option,
  knob-steered internal feedback.
