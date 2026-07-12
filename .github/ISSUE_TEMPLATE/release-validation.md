---
name: Release validation (bench)
about: Report results of validating an experimental firmware on a real 288r
title: "[VALIDATION] patched.hex on <board rev>"
labels: hardware, validation
---

Ran through `docs/validation-checklist.md` on a real unit. Results:

**Setup:** board rev / firmware version / ST-Link + tool used

**1. Pre-flight**
- Dumped + backed up stock firmware: yes/no
- RDP level: (should be 0/open)
- Unit firmware vs `B288-REV1.0.hex` diff: MATCH / DIFFERS (attach diff if differs)

**2. Boot + regression:** boots? all modes (cal/preset, A/B/C, cycle, vintage, recirc) OK?

**3. The fix:** TIME sweep glides (no stepping)? both channels? A/B vs stock audible?

**4. Real-time budget:** any dropouts/crackle under full load (all taps, both paths, fast mod, feedback)?

**5. Restore:** reflashed stock OK?

**Verdict:** GO / NO-GO (+ notes; if won't boot, paste PC/fault regs; if glitches, the conditions)
