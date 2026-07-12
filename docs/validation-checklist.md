# Experimental firmware — bench validation checklist

**Gate:** don't cut a release until every **must-pass** item below is checked on a real 288r with an
ST-Link. This applies to `re/patches/patched.hex` (the interpolation patch on the stock firmware) and
to any future experimental image. See also the safety notes in [CONTRIBUTING.md](../CONTRIBUTING.md).

> The patch is spliced at exact flash addresses against `Compiled FW/B288-REV1.0.hex`. It has been
> **statically verified only** (assembled, spliced, re-disassembled) — it has **not** been run on
> hardware. This checklist is how it earns a release.

## 1. Pre-flight (before flashing anything) — must pass
- [ ] **Dump the unit's current firmware** (`0x08000000`, full flash) and save it as the golden restore.
- [ ] **Confirm RDP is Level 0 / open** (option bytes at `0x1FFFC000`). If not open, STOP — a debug
      connect could mass-erase. Never write option bytes / set RDP.
- [ ] **Diff the dumped firmware vs `Compiled FW/B288-REV1.0.hex`.** They must match — the patch
      anchors assume this exact image. If they differ, report the diff before proceeding.

## 2. Boot + regression (patch didn't break normal operation) — must pass
- [ ] `patched.hex` programs and **verifies** over SWD.
- [ ] Module **boots and runs** — no hang / hardfault (not stuck in the default trap).
- [ ] **Baseline audio** passes and the delay/taps sound normal.
- [ ] Every mode still works: **cal/preset, A/B/C, short/full cycle, 12/16/20-bit vintage, presets,
      and recirc/looper** (the `mode==6` bank-B fetches were also patched).

## 3. The fix works — must pass
- [ ] **Slowly sweep the delay time (TIME knob / CV)** → it now **glides smoothly**, no zipper/stepping.
- [ ] **A/B vs stock:** flash stock → hear the stair-step; flash patched → hear it glide.
- [ ] **Both audio paths/channels** are smooth (we patched `sub_1968` *and* `sub_1c98`).
- [ ] A slow LFO into TIME CV gives **glitch-free chorus/flanger**, not clicks.

## 4. Real-time budget — must pass ⚠️ (most likely failure mode)
The patch adds two SDRAM reads + a float lerp per tap. **Stress it:** all taps active, both paths,
fast modulation, feedback.
- [ ] **No dropouts / crackle / pitch artifacts** under full load (would mean the audio ISR overruns
      the DMA deadline). If it glitches, this is a **no-go** — optimize the cave (or linear-only) first.
- [ ] **No unexpected loud / DC output** (safety — keep monitor levels low while testing).

## 5. Restore path — must pass
- [ ] Reflash the **stock firmware** and confirm the module is fully back to normal.

## Report back (open a "release validation" issue)
- Diff result (unit firmware vs our hex): pass/fail.
- If it won't boot: SWD halt with PC / fault registers.
- If it glitches: the conditions (tap count, modulation rate, feedback).
- Whether the smooth-modulation A/B is clearly audible.

## If all pass → release
Cut a GitHub **pre-release**: bundle `patched.hex` **+ the stock restore `.hex`** + a short
flash/restore/safety note (link this checklist and CONTRIBUTING "Flashing & safety").
