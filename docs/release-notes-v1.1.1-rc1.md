# 288r community firmware — v1.1.1-rc1 (TEST BUILD / pre-release)

⚠️ **Release candidate for field testing — not yet bench-verified on the reference unit.**
Don't take this to a gig; that's what v1.1.1 final will be for. Updating preserves saved
presets as always, and reflashing v1.1.0 (or the stock image) rolls back cleanly.

## What needs testing (the point of this RC)

- **Pulse CV inputs** ([#1](https://github.com/auxren/288r/issues/1)): `write`, `recirc`, and
  `next sound` jacks now edge-latch at ~3 kHz — **millisecond pulses (281e/251e) should
  trigger reliably**, envelopes/gates unchanged. Please test with real pulse sources.
- **cal. releases the knob pin** ([#2](https://github.com/auxren/288r/issues/2)): switching to
  cal. should give immediate multiplier-knob response — no sweep-to-catch required.
- **Rear DIP 1 is now ×4, "+2 octaves"** ([#4](https://github.com/auxren/288r/issues/4)) —
  changed from ×10, which exceeded three subsystem envelopes (saturated octave switch, clamped
  pitch echoes, 10–40 s looper cycles = the reported "unpredictable behavior"). ×4 stacks in
  pure octaves with the ×1/×2/×4 switch; DIP + ×4 reaches the full ~19 s memory depth cleanly.
  Please exercise DIP 1 with the octave switch, looper, and pitch mode.
- **Flasher self-install** ([#6](https://github.com/auxren/288r/issues/6)): if no flashing tool
  is installed, the launcher now offers to install one (brew/apt/dnf/winget) and continues.

## Everything else

Identical to [v1.1.0](https://github.com/auxren/288r/releases/tag/v1.1.0) (string mode,
concert-grade codec bring-up, and all prior features).

Report results on the linked issues — working or broken, both are useful.
