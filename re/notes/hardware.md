# 288r hardware reference (board photo + BOM)

Complements the binary RE (`architecture.md`, `delay-engine.md`) with physical-board facts.
Sources: top-side board photo, manufacturer BOM `B288-BOM-v1_0.xlsx` (REV 1.0), and marked
community/vendor reports. Items not yet visually/loupe-confirmed are flagged **VERIFY**.

## Silicon
- **MCU: STM32F429, LQFP144** — almost certainly **STM32F429ZIT6** (2 MB flash, 192 KB SRAM +
  64 KB CCM). Cortex-M4F + DSP + FMC. **VERIFY exact suffix with a loupe.**
  → confirms `firmware/STM32F429.ld` FLASH=2048K and _estack 0x20030000 (192 KB SRAM top).
- **External SDRAM: ISSI, TSOP-54, 16-bit** (marking "…611", date 1815) on the FMC — the delay/loop
  buffer. Density ≥64 Mbit; likely IS42S16xxx (**8 MB or 32 MB — VERIFY**). Maps at 0xC0000000.
- **Second ST QFP (LQFP48/64) near the STLINK header — role UNCONFIRMED.** Possibly a companion
  MCU (USB/UI) or codec controller. **May have its own firmware + RDP — must be dumped/checked
  separately.** The RE so far covers only the F429 image (`B288-REV1.0.hex`).
- **Audio codec:** part not identified from the photo. Firmware config says 24-bit; sample rate
  **96 kHz** (vendor copy "196KHz" is a typo — reconcile against codec/SAI init). **VERIFY part** via
  the I²C/SPI codec init in the F429 image.
- **HSE crystal:** discrete HC-49 near the MCU. **VERIFY frequency** (needed for the clock tree).

## Config / UI front end — a hardware scan chain (no preset NVM expected)
Presets are **read live from hardware**, not stored. The MCU clocks DIP banks in via **74HC595**
shift registers and muxes trimmers to an ADC via **74HC4051**. Matches the vendor pitch ("no need to
wire/change resistors") and the RE (`read_mode_switches`, `read_panel_i2c_sliders`,
`scan_all_preset_dipswitches`). Implication: **no preset file format to reverse; RE = scan loop +
DSP engine.**

| Part (BOM) | Device | Qty | Function |
|---|---|---|---|
| 774-2088 | CTS 208-8 (8-pos DIP) | 6 | **tap-time presets, binary, 10 ms steps** |
| 774-2084 | CTS 208-4 (4-pos DIP) | 1 | mode/config bits |
| 206-125ST | CTS 206-125 (5-pos DIP) | 4 | phase-invert per tap |
| 206-124 | CTS 206-124 (4-pos DIP) | 4 | channel mute per tap |
| 652-3362P-1-503 | Bourns 50K trimmer | 36 | tap mix levels (4 banks × 9) |

→ Refines the engine model: tap **time** comes from binary DIP in 10 ms units (not just the
`preset_phase_table` 20..160 values seen in the binary — reconcile which is time vs. phase);
phase-invert and mute are per-tap DIP bits; the 36 trimmers are the OUTPUT MIXER levels
(4 preset banks × 9 = 8 taps + master), muxed to ADC. `firmware/src/panel.*` (to build) is this scan.

## Storage anomaly — VERIFY on the board
BOM lists MPN `579-25AA512-I/P` (Microchip **25AA512, 512 Kbit SPI EEPROM**) in a cell labeled
"PLD 20 PIN (FEMALE)" for the PCB1↔PCB2 connector — almost certainly a paste error over the
connector MPN. **But if a 25AA512 is actually populated (SOIC-8 marked `25512`/`25AA512`), it is
NVM and contradicts the stateless-preset model** (could hold calibration/presets). **Physically
check the mainboard.**

## Programming / debug — SWD, open
- Boxed **2×10 STLINK** IDC header + **2×3 DEBUG** header, right edge. Standard SWD.
- Kit shipped an **ST-Link/V2** (`511-ST-LINK/V2`) — **no proprietary bootloader**; flashing is raw
  SWD (OpenOCD / STM32CubeProgrammer). Community builders reflashed these "painlessly".
- **RDP (read-protect) level is the gating unknown for dumping** — almost certainly **Level 0 (open)**
  given the shipped ST-Link. Check option bytes (`0x1FFFC000`) before any connect that could
  mass-erase. (We already have the `.hex`, so this matters mainly for the second MCU / re-dumps.)

## BOM scope caveat
`B288-BOM-v1_0.xlsx` covers only **PCB1 (front panel)** and **PCB2 (trimmer/config board)** — the
**mainboard (PCB3) with the F429/SDRAM/codec/595/4051 is NOT in this file.** The mainboard BOM is the
**highest-value missing artifact** (pins down F429 suffix, codec part, SDRAM density). Source it.

## Known behavioral bugs (community; firmware-side)
1. **Pitch/Time mode buffer-wrap glitch** — in pitch mode a saw-LFO modulates delay time; at buffer
   wrap it resets, causing an audible discontinuity. → engine fix: crossfade/hide the wrap (classic
   dual-head crossfaded pitch tap). Intersects our delay engine directly.
2. **Narrow usable Time CV range** — small effective range from full CV swing → ADC input
   scaling/calibration (relates to `time_control` mapping — a calibration item here anyway).
3. **Preset recall / general polish.**

## Bench checklist (resolves the remaining unknowns)
- [ ] F429 exact suffix (flash/RAM) — loupe.
- [ ] SDRAM part# + density (ISSI marking) — sets max delay & informs buffer sample format.
- [ ] Second ST QFP role; separate firmware + RDP?
- [ ] Option bytes / RDP level before any erasing connect.
- [ ] 25AA512 EEPROM present on mainboard? (preset-storage question)
- [ ] HSE crystal frequency.
- [ ] Locate mainboard (PCB3) BOM.
- [ ] Confirm codec part + that SAI runs 24/96 (not 192).
