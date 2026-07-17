# 288r hardware reference (board photo + BOM)

Complements the binary RE (`architecture.md`, `delay-engine.md`) with physical-board facts.
Sources: top-side board photo, manufacturer BOM `B288-BOM-v1_0.xlsx` (REV 1.0), and marked
community/vendor reports. Items not yet visually/loupe-confirmed are flagged **VERIFY**.

## Silicon (part numbers read off the clearer board photo)
- **MCU: STM32F429ZET6 (LQFP144)** — confirmed from the chip marking (`32F429ZET6`). `ZE` = **512 KB
  flash**, 192 KB SRAM + 64 KB CCM, _estack 0x20030000. Cortex-M4F + DSP + FMC.
  (`firmware/STM32F429.ld` FLASH = 512K.)
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
drives the CS42888 over **SAI2 in multichannel TDM** (HAL-style SAI slot/frame config; this is why the
firmware has two paths "A"/"B" — 8 out + 4 in split across SAI sub-blocks / TDM slots). →
`firmware/src/audio_io` and the engine output stage should emit **8 independent tap channels via TDM**.

### Peripheral roles (traced statically — corrects earlier rename labels)
- **SAI2** (`0x40015800`): the CS42888 TDM audio (data path).
- **SPI2 as SPI** (`0x40003800`, handle @`0x20001368`, chip-select on **GPIOB12**): reads the **analog
  control surface** (sliders/pots), 3 bytes per transfer — i.e. an external SPI ADC. (The rename's
  "read_panel_i2c_sliders / I²C sliders" is a mislabel — it's **SPI2**, not I²C.)
- **I²C1** (`0x40005400`, handle @`0x200013c4`): a configured HAL I²C bus (analog/digital filters set)
  talking to some device — but see below.

### CS42888 control bus — RESOLVED (bench, 2026-07-16): **I²C1**
Live SWD read of the running unit (`re/notes/bench-session-1.md`): **I²C1 is enabled and configured**
(`CR1=0x1` PE, `CR2 FREQ=42 MHz`) → it's the CS42888 control port. **SPI2 is in SPI mode**
(`I2SCFGR I2SMOD=0`, master) → it's the **control-surface SPI ADC** (sliders/pots, CS on GPIOB12),
not the codec. That settles the earlier ambiguity (static analysis couldn't isolate the I²C address —
it's computed/runtime, not a catchable immediate). Still open: the exact codec **I²C address +
register values** (needs an I²C sniff at power-up — SWD can't sniff the bus).

### Audio: it's **SAI1** (not SAI2), TDM 8×32-bit / 24-bit — RESOLVED (bench)
Correction: the SAI @ `0x40015800` is **SAI1** (F429 has only SAI1). Live regs: Block A = Master RX
(inputs), Block B = Slave TX (the 8 DAC channels); **8 slots × 32-bit slot, 24-bit data, 256-bit
frame, all 8 enabled**. Confirms `firmware/src/audio_io.c`. Slot→tap order still needs a live test.

### Clock tree — RESOLVED (bench): **HSE = 8 MHz**, SYSCLK 168 MHz
`RCC_PLLCFGR` M=8/N=336/P=2 from HSE → **168 MHz** SYSCLK, APB1 42 / APB2 84 MHz. StdPeriph init can
be written for real now.

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

## Switch → firmware function map (traced from the `.hex`)
Two groups: GPIO-read mode switches (`read_mode_switches` sub_f64) and a packed
`panel_switch_bits` word @`0x20000358` bit-tested by `sub_4310(N) = (bits >> N) & 1`.

**GPIO mode switches** (base: 0x40020400=GPIOB, 0x40020c00=GPIOD):
| Panel switch | GPIO pin | RAM | Effect (confirmed) |
|---|---|---|---|
| **cal. / pre-set** | GPIOB **10** | `0x20000362` | cal=1 → **live setup mode**: short buffers, ×2 time mult, taps track the **raw** control (vs the hysteresis-committed value in pre-set). |
| **SHORT / FULL cycle** | GPIOB **11** | `0x20000361` | scales every tap position 4:1 — per-unit = **44** (FULL) vs **11** (SHORT) samples (the 44140/11035 divisor). |
| resolution (2-bit) | GPIOD **11/12** | `0x20000360` | bit-depth: 0→20-bit, 1→16-bit, 2→**12-bit vintage**. |

**`panel_switch_bits` (0x20000358)** — packed switch/DIP word:
| Bit(s) | Effect |
|---|---|
| **0 / 1 / 2** | **preset bank select** in `lookup_preset_tap_position`: bit0 off → default even spacing (20,40,…,160); bit1 off → phase-table row +16; bit2 off → row 0; else row +8. This is the **A / B / C** selection (three stored rows + a default). |
| 3 | tap-target mode (phase×mult vs raw). |
| 6 | enables **bank_B** (second delay buffer / recirc path). |
| 9 / 10 | octave/rate mode (×1/×2/×4) → `output_resolution_mode` → SAI PLL. |

→ **Takeaway for our cal routine:** the `cal.` toggle (GPIOB10) is already a *live* mode, so for a
*stored* min/max calibration prefer the **momentary SW14/SW16 power-up hold** (distinct entry, no
conflict). The A/B/C toggle just picks which trimmer-derived phase row feeds the taps.

**Continuous controls (ADC-read — sliders/pots via the SPI2 ADC per above, trimmers likely via the
4051 mux; calibration targets, min/max):**
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
  mass-erase. (We already have the `.hex`, and there's no second MCU, so RDP matters only for re-dumps.)

## BOM scope caveat
`B288-BOM-v1.0.xlsx` covers only **PCB1 (front panel)** and **PCB2 (trimmer/config board)** — the
**mainboard (PCB3) with the F429/SDRAM/codec/595/4051 is NOT in this file.** Its main silicon is
already identified from the board photo (F429Z / CS42888 / IS42S16400), so the only thing the PCB3 BOM
would still add is the **exact F429 flash suffix** and any parts not readable in the photo — nice to
have, not blocking.

## Known behavioral bugs (community; firmware-side)
1. **Pitch/Time mode buffer-wrap glitch** — in pitch mode a saw-LFO modulates delay time; at buffer
   wrap it resets, causing an audible discontinuity. → engine fix: crossfade/hide the wrap (classic
   dual-head crossfaded pitch tap). Intersects our delay engine directly.
2. **Narrow usable Time CV range** — small effective range from full CV swing → ADC input
   scaling/calibration (relates to `time_control` mapping — a calibration item here anyway).
3. **Preset recall / general polish.**

**Corroboration (performer report, 2026-07):** a user who gigged with it described exactly the
time-control problem as the deal-breaker for live use — *"the thing that made it not possible to use
live was the time thing, it always did the same sound."* Matches our RE: the TIME parameter is
hysteresis-quantized (`time_multiplier_committed`) and coarse delay re-tunes the PLL in octave steps,
so the control isn't a smooth/continuous delay time — it locks to a few discrete values. **This is
the priority fix** — the fixed-rate rewrite (continuous slewed fractional delay, no hysteresis/PLL
stepping) targets it directly; the interpolation patch only smooths the *fine* read, not the upstream
hysteresis.

## Bench checklist (updated after the clearer photo)
Resolved from the photo: ✅ codec = Cirrus CS42888 (4-in/8-out); ✅ SDRAM = IS42S16400 (8 MB, 16-bit);
✅ no second MCU (the "second QFP" was the codec) → no extra firmware/RDP to chase.
Also resolved from chip close-ups: ✅ MCU = **STM32F429ZET6 (512 KB flash)**; ✅ codec = CS42888-DQZ;
✅ SDRAM = IS42S16400J-7TLI (8 MB, -7).
Still to confirm:
- [ ] Codec **control bus: I²C or SPI2** + the **TDM slot→tap map** (read from F429 codec-init).
- [ ] Codec sample-rate mode: confirm 24/96 (spec "196KHz" is a typo).
- [ ] HSE crystal frequency (clock tree).
- [ ] Option bytes / RDP level before any erasing connect (we already have the `.hex`).
- [ ] 25AA512 EEPROM actually populated? (likely a BOM paste error; low priority now.)
