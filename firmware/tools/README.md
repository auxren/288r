# tools/ — host-side tools (not firmware)

## render.c — offline audio proof of the interpolation fix
Runs a signal through the real `delay_line` as an LFO-swept feedback delay, two ways, and writes WAVs
so you can A/B the interpolation fix **by ear** before any hardware:

```bash
mkdir -p out
cc -std=c11 -O2 -I../src render.c ../src/delay_line.c -o /tmp/render -lm
(cd out && /tmp/render)
```

Outputs (48 kHz mono):
- `dry.wav` — input, for reference.
- `flanger_smooth.wav` / `flanger_stepped.wav` — 110 Hz saw → flanger; **smooth** = fractional
  Hermite interpolation (the fix), **stepped** = nearest-sample read (emulates the stock zipper).
- `glide_smooth.wav` / `glide_stepped.wav` — 330 Hz sine → fast delay sweep (pitch glide); the
  starkest A/B: smooth glides cleanly, stepped has stepped pitch + zipper.

The *only* difference between smooth and stepped is fractional-vs-nearest tap reads — exactly what
Patch 1 / the rewrite change. This is also the seed of the AU/VST path: the same engine, wrapped.
