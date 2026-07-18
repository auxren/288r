# 288r panel I/O — decoded from the stock firmware (bench session 3, no hardware)

Reverse-engineered entirely from `re/binja/288r_decompiled_abridged.txt` while the bench was down.
The panel is **bit-banged shift registers on GPIO** (NOT SPI1 — SPI1 is disabled in stock). Three
independent paths: digital-in (switches), digital-out (LEDs/columns), and analog (mux → ADC).
Pin ROLES are confirmed from the disassembly; the per-bit MAPPINGS marked **[BENCH]** need a live
sweep on the unit to label. Driver implemented in `firmware/src/bsp/panel.c`.

## 1. Switch input — 74HC165  (`sub_4290` = read_panel_switches)
```
PA4 (0x10) = LATCH/LOAD    PA5 (0x20) = CLOCK    PA6 (0x40) = DATA-IN (input)
```
Pulse PA4 to load the parallel inputs, then clock 13 bits MSB-first on PA5, sampling PA6 →
`panel_switch_bits` (RAM 0x20000358). Implemented: `bsp_panel_switches_read()`.

**Bit meaning** — traced from every `sub_4310(N)` = `(panel_switch_bits>>N)&1` test site:

| Bit(s) | Function |
|---|---|
| 0/1/2 | **preset A/B/C/D select — ACTIVE-LOW priority** bit0>bit1>bit2. In the tap-position lookup: `bit0=0`→ A = linear ramp `(tap+1)*20` (=20,40,..,160); `bit1=0`→ B = `preset_phase_table[tap+16]`; `bit2=0`→ C = `preset_phase_table[tap]`; none low → D = `preset_phase_table[tap+8]`. |
| 3 | tap-target mode (phase×mult vs raw read) |
| 4 | transport/mode gate (guards `transport_mode` 0x200000d0) |
| 6 | **bank_B** (second delay buffer / recirc) |
| 7, 8 | **transport triggers** (write/recirc entry — the momentary SW14/SW16 candidates); the code they gate retunes the SAI **PLL** (`RCC 0x40023888/8c`) + advances loop pointers |
| 9/10 | **octave/rate ×1/×2/×4** — `get_mode_from_switches` (sub_1110): `if(!bit10) ×1; else if(!bit9) ×4; else ×2` (bit10 master). Also → PLL retune. |
| 11, 12 | transport / loop-window control (write ptr `0x200000c4`, loop `0x200013c0`) |

Confirms the stock coarse-delay scheme = **PLL octave retune** in the transport path (bits 7/8/9/10/11)
— exactly what the fixed-rate + fractional-read rewrite eliminates. The preset + octave **decode logic is
code-exact** (replicated in `firmware/src/panel_ctl.c` — no bench guessing needed; we drive the same 165
on PA4/5/6 so the switch→bit mapping is identical). Only the **B/C/D preset phase VALUES** are pending —
the stock fills `preset_phase_table` from the physical preset-DIP matrix (`scan_all_preset_dipswitches`,
sub_3488), so those need the matrix scan. SHORT/FULL cycle, cal/preset, resolution are *separate*
direct-GPIO reads (PB11, PB10, PD11/12) already in `gpio_panel.c`.

## 2. LED / column output — 74HC595  (`sub_3408`, 24 bits/column)
```
PC12 (0x1000) = DATA    PC10 (0x400) = CLOCK    PA15 (0x8000) = LATCH
```
Shift 24 bits MSB-first (data on PC12, rising edge on PC10), then pulse PA15 to latch. Source is a
per-column word `*(0x20000054 + col*4)`. Implemented: `bsp_panel_out(bits24)`.

**The 8 column words are constants recovered from the image `.data`** (flash `0x8006c88`→RAM
`0x20000000`; verified — same block holds `delay_ram_length=0x16120`, `bank_A=0xD0000000`,
`bank_B=0xD0360000`):
```
col0..7 = 0x000000, 0x111111, 0x222222, 0x333333, 0x444444, 0x555555, 0x666666, 0x777777
```
So the 595 is NOT a one-hot column strobe — it drives a **6×3-bit address sweep**: the 24 outputs are
six nibbles, each low-3 bits carrying the column index 0..7 (nibble MSBs = bits 3,7,11,15,19,23 stay 0).
Reads as **6 mux/DIP banks × 8 channels = 48 inputs** (≈ the 36 trimmers + sliders/pots count; the "many
4051s"). Key safety point: **no 595 bit is held HIGH across the scan**, so the codec-reset line is *not* a
hold-high 595 output in this path (it's a GPIO or one of the always-0 nibble-MSB bits). LED drive is a
*separate* shift (find its routine, or a walking-1) — that + the physical LED→bit map is the only real
**[BENCH]** left on the 595.

## 3. Analog controls — 4051 mux → ADC3/PF8, and SPI2
- **Multiplier CV + knob = SPI2 external ADC** (`sub_ecc`): CS=PB12, SCK=PB13, MISO=PB14. Two reads,
  `{0x01,0xA0,0x00}` and `{0x01,0xE0,0x00}`, 12-bit = `((rx1&0x0F)<<8)|rx2` → `slider_raw_table[0/1]`.
  **ch0 (0xA0) = Time-CV — CONFIRMED live.** ch1 (0xE0) is the coarse **knob** (best inference; read
  ~4094 stuck in our probe — likely an SPI framing/pipeline issue reading the 2nd channel; test
  `bsp_pot_read(1)` standalone). The stock multiplier (`0x20001eec`) combines table[0]·(ADC3-derived
  scale) with 128-sample averaging + hysteresis.
- **Trimmers / sliders / pots = 4051 mux → ADC3 ch6 = PF8.** Mux **address** = GPIOA {PA0,1,7,8,11}
  via `gpio_set/clear_bit_by_index` (`sub_102c/sub_fe0`). In our firmware PF8 read a floating 4095 →
  the mux **ENABLE is not asserted**; it is most likely one of the 74HC595 output bits (§2). Resolve
  by shifting 595 patterns and watching PF8 track a trimmer.

## 4. Codec reset — unresolved (probably a 595 bit, or unneeded)
We "released" the codec by driving PA{0,1,2,4,7,8,11}+PC12 high — but those are now known to be the
mux-address / 165-latch / 595-data pins, and **PA2 is never driven high in stock**. So that GPIO block
was likely incidental; the codec actually came alive because we **reordered SAI/MCLK before the codec
I²C** (the CS42888 control port needs MCLK). The true RST is probably a **74HC595 output bit**.
**Test:** remove `codec_reset_release()` (keep MCLK-first) — if the codec still ACKs, drop it; if not,
find the RST bit in the 595 pattern.

## 5. Full pin ledger (this session)
| Signal | Pin | Role |
|---|---|---|
| I²C1 SCL/SDA | PB8/PB9 | codec control |
| SAI1 MCLK/FS/SCK | PE2/PE4/PE5 | audio clocks |
| SAI1 SD_A / SD_B | PD6 / PF6 | ADC-in / DAC-out |
| SPI2 CS/SCK/MISO | PB12/PB13/PB14 | control ADC (CV+knob) |
| 165 latch/clk/data | PA4/PA5/PA6 | switches in |
| 595 data/clk/latch | PC12/PC10/PA15 | LEDs + columns out |
| 4051 mux address | PA0,PA1,PA7,PA8,PA11 | analog mux select |
| ADC3 ch6 | PF8 | muxed analog in (trimmers/sliders) |
| cal/preset, cycle, res | PB10, PB11, PD11/12 | direct-GPIO switches |
| FMC SDRAM | bank 2 @0xD0000000 (CKE1=PB5,NE1=PB6) | delay buffer |

## Next on hardware
1. `bsp_panel_switches_read()` → dump `panel_switch_bits` while toggling switches → label the 13 bits.
2. `bsp_panel_out()` walking-1 → find the 5 LED bits (and the 4051-enable bit → PF8 starts tracking).
3. `bsp_pot_read(1)` standalone → confirm ch1 = knob; combine ch0(CV)+ch1(knob) for the multiplier.
4. Confirm codec RST (drop `codec_reset_release` or find its 595 bit).
5. Reconcile pin sharing (codec-reset GPIO block overlaps panel pins) before wiring `panel.c` into main.
