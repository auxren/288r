# 288r community firmware — v1.1.1-rc2 (TEST BUILD / pre-release)

⚠️ Release candidate for field testing. Presets survive updating, as always.

## New since rc1 — the headline discovery

**The rear DIPs never worked reliably — on any prior build.** They are wired *through the
scanned panel matrix*, not as direct straps: before the panel driver parks the 74HC595 chain,
the switches are electrically disconnected from the MCU, so every boot-time read saw a floating
pin. Whether "long mode" engaged was literally a coin flip per power-on (a likely contributor
to earlier "×10 is unpredictable" reports). All three DIP functions now latch deterministically
after the panel initializes:

- **DIP 1 — delay extend (×4, "+2 octaves")** — verified engaging on the reference unit
- **DIP sw3/sw4 — resolution (24/12/8/4-bit)** — needs fresh field verification: past
  "confirmed" behavior may have been the same coin flip
- **DIP sw2 — bandwidth limit (~11 kHz)** — same: please test flip → power-cycle → listen

## Also since rc1

- **Fully hardened flasher**: staged path-free execution (folders with spaces are fine),
  pip-installable fallback stack, bundled driver for old kit ST-Links (bench-proven with a
  full flash on the reference dongle), correct post-flash reset.
- **Deep-memory click investigation closed** ([#8](https://github.com/auxren/288r/issues/8)):
  reference unit ran 5 h in genuine ×4 with zero events — the reported clicks are that unit's
  RAM, not firmware. A despike mitigation is on offer if wanted.

## Carried from rc1 (already field-verified ✓)

Pulse-jack triggers (#1), cal-mode pin release (#2), ×4 as the extend factor (#4).

## What to test

1. **Rear DIPs, all four**: does each function engage every single power-cycle now?
2. Resolution: sw3 = clearly crunchy 12-bit, sw3+sw4 = destroyed 4-bit — consistent per boot?
3. Bandwidth: sw2 = dulled highs — real or placebo?
4. Everything you normally play — regressions welcome news too.

Report on the issues; working/broken both useful.
