# 288r community firmware — v1.2.3-rc4 (pre-release for field testing)

**Updating preserves your saved presets.** Supersedes rc3 (everything in rc3's notes is
included); this rc adds one targeted fix and a performance win.

## New features

- (No new features — see rc3 for the overdub v2 line, tape-motor multiplier, and
  seam/LED fixes.)

## Fixed

- **#24 — spike when pitch mode leaves exact unity** (knob from full CCW, or CV
  crossing 0 V; deterministic, every time). Root cause: the dry→voice crossfade band
  was crossed by the ratio glide in under 2 samples, snapping the output between two
  reads ~30–60 ms apart in the buffer. The crossfade now has its own ~4 ms smoothing,
  decoupled from the ratio glide — it can't park mid-mix (the beating artifact the
  thin band prevents stays dead) and can't snap. The unity-bypass grain state is also
  pinned so the voice resumes from the exact sample the bypass was reading. Verified
  on the reference unit with 12 forced unity crossings in both directions: clean.
- **Processor headroom improved ~17 points** (loop playback ~93% → ~76% of the audio
  deadline) from a code-layout change — more margin for everything, and the rc3
  "running near the ceiling" caveat is substantially relaxed.

## Known / open issues

- If captures sound crunchy, check the **input clip LED** while recording — hot
  modular sources rail the ADC on crest peaks well before they sound hot.
- A forgotten source patched into **signal in** (the FM input) will FM-garble loops
  by design — pot down when not in use.
- Sens-knob useful range depends on source level (#13 — measurements in progress;
  likely resolves as gain-staging guidance rather than a firmware change).
- Deeper presets (#5). Next up: split-tap loop+live, stereo master option,
  knob-steered internal feedback.
