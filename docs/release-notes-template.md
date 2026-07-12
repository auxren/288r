# Release notes template (experimental firmware)

Copy this into the GitHub Release body when cutting an experimental build. Mark the release as a
**pre-release**. Attach **both** the experimental `.hex` and the **stock restore** `.hex`.

Cut with: `gh release create vX.Y.Z-exp --prerelease --title "..." --notes-file notes.md \
  re/patches/patched.hex "Compiled FW/B288-REV1.0.hex"`

---

## 288r community firmware — vX.Y.Z (EXPERIMENTAL / pre-release)

⚠️ **Experimental.** Flash at your own risk. See the safety notes below and
[`docs/validation-checklist.md`](validation-checklist.md).

### What this is
<!-- e.g. "Stock 288r firmware + interpolated delay taps: smooth delay-time modulation
     (chorus/flanger) on both audio paths. Nothing else changed." -->

### What changed vs stock
- <!-- e.g. delay tap read is now fractional/interpolated (was integer -> stepped) -->

### Validation status
- Tested on hardware: **yes/no** — by @<who>, board rev <…>, on <date>
- Result vs `docs/validation-checklist.md`: <!-- GO, with notes / which items passed -->
- Known limitations: <!-- e.g. recirc loop-boundary neighbour is length-wrapped (≤1 sample) -->

### Files
- `patched.hex` — the experimental firmware
- `B288-REV1.0.hex` — **stock restore image** (reflash this to go back)

### Install (SWD / ST-Link)
1. Back up your unit's current firmware first (dump `0x08000000`).
2. Flash `patched.hex`:
   `openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "program patched.hex verify reset exit"`
3. To restore: flash `B288-REV1.0.hex` the same way.

### Safety
- You basically **can't brick it** — SWD + the stock hex always restores it. **Never touch the
  RDP / option bytes** (RDP Level 2 is the only true brick).
- Experimental firmware can output **loud / DC** signal — keep monitor levels **low** while testing.
- Report problems as a **"Release validation (bench)"** issue.
