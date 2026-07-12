<!-- Thanks for contributing! Keep PRs focused. -->

## What & why


## Checklist
- [ ] `cd firmware && make test` passes (added/updated a `test_*.c` if engine behavior changed)
- [ ] `make engine` still cross-compiles for the F429
- [ ] Clone-first: matches stock behavior, or clearly a post-clone feature; new constants parameterized / marked `calibrate on hardware`
- [ ] No vendor source redistributed; no large binaries / venv committed
- [ ] Docs updated if behavior/plan changed (`CLAUDE.md` / `firmware/DESIGN.md` / `re/notes/`)

## Tested on hardware?
<!-- If this touches anything flashable: did you run it on a real 288r? Firmware that hasn't been
     run on hardware must not be merged into a release. -->
