# 288r — Time Domain Processor

User manual for the Buchla-format **288r Time Domain Processor** and the open-source
**community firmware** in this repository.

> **Scope & status.** This manual describes the 288r module and the community firmware being
> developed here. Panel controls, switch functions, and the delay engine are documented from
> reverse-engineering the shipped firmware plus bench measurements on real hardware. Items that are
> **community-firmware** additions, or that are still being confirmed against the panel, are marked.
> Where this manual and any original 288 documentation differ, this manual describes the module as it
> actually behaves.

## Contents

1. [Overview](01-overview.md) — what the module is and its signal flow
2. [Installation & flashing](02-installation-and-flashing.md) — programming firmware over SWD, and restoring stock
3. [Front-panel reference](03-front-panel-reference.md) — every control and jack
4. [Operation](04-operation.md) — delay, looper, feedback, and modulation
5. [Time & pitch modes](05-time-and-pitch.md) — the multiplier, smooth modulation, and pitch
6. [Presets, taps & mixers](06-presets-taps-mixers.md) — the four banks, phase, tap times, output mixer
7. [Fidelity, tone & settings](07-fidelity-tone-settings.md) — bit-depth, analog tone, the settings mode
8. [Troubleshooting](08-troubleshooting.md) — recovery and common issues
9. [Firmware & contributing](09-firmware-and-contributing.md) — the community-firmware project

## Quick start

1. Patch a sound source into an **input**, and take audio from a **tap output** or the **mixed out**.
2. Choose a **preset bank** (A/B/C) and set the **cycle length** (short/full).
3. Bring up a couple of **output-mixer sliders** to hear the delay taps.
4. Turn the **multiplier** to set the delay time; in **TIME** mode you can modulate it smoothly
   (chorus/flanger); **PITCH** mode tracks pitch. See [Operation](04-operation.md).

## The module in one paragraph

The 288r is a digital recreation of the Buchla 288 "Time Domain Processor" — an 8-tap
voltage-controlled delay / looper. Audio is written into a delay memory; **eight taps** read it back
at positions set by the **phase-select** presets and scaled by the **multiplier**, each tap with its
own level/phase in the output mixer and its own physical output. It can run as a **delay** or a
**looper** (write / recirculate), with a **vintage** reduced-bit-depth mode for lo-fi character.

## For contributors & builders

Process/reference docs live alongside this manual:

- [bench-runbook.md](bench-runbook.md) — SWD/bench session procedure
- [validation-checklist.md](validation-checklist.md) — what to verify before a firmware release
- [release-notes-template.md](release-notes-template.md) — release notes template
- [CONTRIBUTING.md](../CONTRIBUTING.md) — how to contribute · `firmware/DESIGN.md` — firmware architecture

## Building this manual as a PDF
As the manual matures, a combined PDF will be attached to each
[release](https://github.com/auxren/288r/releases) (MARF-style `make manual`).
