# 288r hardware reference (board photo + BOM)

Complements the binary RE (`architecture.md`, `delay-engine.md`) with physical-board facts.
Sources: top-side board photo, manufacturer BOM `B288-BOM-v1_0.xlsx` (REV 1.0), and marked
community/vendor reports. Items not yet visually/loupe-confirmed are flagged **VERIFY**.

## Silicon (part numbers read off the clearer board photo)
- **MCU: STM32F429Z (LQFP144)**, marked `ARM / ST 32F429Z?T6`. Cortex-M4F + DSP + FMC.
  **Confirm flash suffix: `ZE`=512 KB vs `ZI`=2 MB** — matters for `firmware/STM32F429.ld` FLASH size.
  (Image reads like `ZET6`; if 512 KB, change the linker from 2048K to 512K.) 192 KB SRAM + 64 KB CCM,
  _estack 0x20030000.
- **External SDRAM: ISSI `IS42S16400J-7TLI`** = 4M×16 = **64 Mbit / 8 MB, 16-bit, -7 (143 MHz),
  industrial.** Maps at 0xC0000000 via FMC. → density RESOLVED (8 MB). At 16-bit samples that's
  ~4M samples (~43 s mono @ 96 kHz), matching the "40 s" spec — **confirms the int16 SDRAM buffer**
  (float32 would need 15 MB and not fit). 8 MB is also why long delays use reduced rates.
- **Audio codec: Cirrus Logic `CS42888-DQZ` (48-TQFP)** — this IS the "second QFP" near the STLINK
  header; the earlier brief misread the Cirrus logo as "ST". **4 ADC-in / 8 DAC-out, 24-bit, up to
  192 kHz, TDM/I²S, I²C or SPI control.** No separate companion MCU / no second firmware / no extra
  RDP — the design is single-MCU. **This reframes the audio path (see below).**
- **HSE crystal:** discrete near the MCU. **VERIFY frequency** (needed for the clock tree).

## Audio architecture implication (CS42888 = 4-in / 8-out)
The 8 DAC channels map to the **8 taps — each tap has its own physical output** (the 288's individual
tap outs; summed outs are the analog op-amp section). The 4 ADCs are signal/CV inputs. The F429
drives the CS42888 over **SAI2 in multichannel TDM** (this is why the firmware has two paths
"A"/"B" — the 8 out + 4 in split across SAI sub-blocks / TDM slots), with codec register control over
**I²C or SPI2** (`0x40003800`). → `firmware/src/audio_io` and the engine output stage should emit
**8 independent tap channels via TDM**, not a single mixed output; per-tap level/phase/mute apply per
channel. Confirm the exact TDM slot map + control bus from the codec-init code in the F429 image.

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

## Nonvolatile storage — NONE (the "25AA512" was a BOM paste error)
**Resolved.** The full BOM (`B288-BOM-v1.0.xlsx`, PCB1+PCB2) row 38 reads
`PLD1, PLD2 | 2 | PLD 20 PIN (FEMALE) | 579-25AA512-I/P`: the *part* is a **20-pin female connector**
(it mates with `PBD1/PBD2` = "PLD 20 PIN (MALE)", Mouser 517-929836-01-10-RK, on PCB1) — the
`25AA512` MPN is simply **wrong text pasted into the Mouser column**. So there is **no 25AA512 EEPROM**.
Corroborated by the firmware: a static scan of the stock `.hex` found **no SPI-EEPROM usage** (only
SPI2 = codec; no EEPROM driver; no 25xx opcodes). → **The module has no dedicated NVM chip.**
Persistence for any new feature (settings/cal) must use **STM32F429 internal-flash EEPROM emulation**.
(The mainboard PCB3 SMD BOM is still unseen, but both the connector-paste-error and the no-EEPROM
firmware make an EEPROM very unlikely.)

## Panel switch & control inventory (from the BOM)
**Switches (PCB1 panel toggles) — momentary vs latching:**
- **Latching:** `SPDT ON-ON` ×13 (SW2–SW18 subset), `SPDT ON-OFF-ON` ×2 (SW7, SW12, 3-position — a
  center-off works as a 3-way A/B/C selector). These hold persistent state (the `cal./pre-set` and
  `A/B/C` selectors live here).
- **MOMENTARY (spring-return) — gesture candidates for mode entry:**
  `SW14 = SPDT (ON)-OFF-(ON)` (momentary both ways) and `SW16 = SPDT ON-OFF-(ON)` (momentary one
  side). Their stock runtime role is likely manual write/recirc/pulse triggers; a **power-up hold**
  on one of these is the natural way to enter a calibration/setup mode (no runtime conflict).

**DIP switches (PCB2):** SW1 = 774-2084 (4-pos, mode/config); SW27–SW32 = 774-2088 ×6 (8-pos tap-time,
10 ms); SW19/20/23/24 = 206-125ST ×4 (5-pos, phase-invert); SW21/22/25/26 = 206-124 ×4 (4-pos, mute).

**Continuous controls read via the 4051-mux → ADC (calibration targets, min/max):**
- **9× 50 K linear ALPS 45 mm sliders** (POT8–POT16) = output-mixer levels.
- **7× rotary pots** (POT1–POT7; POT1–5 log, POT6–7 lin) = input mixer / time / etc.
- **36× 50 K single-turn trimmers** (TR1–TR36) = the four PRESET banks' tap positions (set to the
  printed 0–160 scale; ADC range still worth normalizing).
- Jacks: 18× Tini-Jax (J1–J18) + banana jacks; 5× 3 mm LEDs.
- Either way: IF present it's the natural home for a persisted **glide/crossfade setting** + cal
  (mirror the MARF storage pattern, DESIGN.md "Persistence"); IF absent, fall back to internal-flash
  EEPROM emulation or keep such settings as physical controls.

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

## Bench checklist (updated after the clearer photo)
Resolved from the photo: ✅ codec = Cirrus CS42888 (4-in/8-out); ✅ SDRAM = IS42S16400 (8 MB, 16-bit);
✅ no second MCU (the "second QFP" was the codec) → no extra firmware/RDP to chase.
Still to confirm:
- [ ] **F429 flash suffix — `ZE` (512 KB) vs `ZI` (2 MB)** — sets the linker FLASH length.
- [ ] Codec **control bus: I²C or SPI2** + the **TDM slot→tap map** (read from F429 codec-init).
- [ ] Codec sample-rate mode: confirm 24/96 (spec "196KHz" is a typo).
- [ ] HSE crystal frequency (clock tree).
- [ ] Option bytes / RDP level before any erasing connect (we already have the `.hex`).
- [ ] 25AA512 EEPROM actually populated? (likely a BOM paste error; low priority now.)
