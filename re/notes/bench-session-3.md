# Bench session 3 (2026-07-16) — full audio bring-up + smooth modulation

The big one: took the community firmware from "builds" to a **working multitap delay with
smooth delay-time modulation** on the real unit, all over SWD (openocd) + a Saleae for the audio.
Every constant below is confirmed on hardware (register reads / live A-B), not inferred.

## Confirmed hardware map (all now baked into `firmware/src/bsp/`)

### Codec control — I²C1
- **I²C1 pins: PB8 = SCL, PB9 = SDA** (AF4, open-drain, internal pull-ups). *(I had guessed PB6/PB7.)*
  Found by dumping stock GPIOB: OTYPER bit8/9 open-drain, AFRH nibble8/9 = AF4.
- **CS42888 address = 0x49** (7-bit). Found by an on-target I²C bus scan (only 0x49 ACKs).
- **Codec reset**: released by replicating stock's driven-high GPIO outputs — **PC12 + PA{0,1,2,4,7,8,11}**
  all driven high. One of those is the CS42888 RST (not yet narrowed; the set works).
- **TDM/24-bit init** (read back verified): `0x02=0x00` (power up), `0x03=0xF0` (slave, FM=11),
  `0x04=0x36` (**TDM**, DAC_DIF=ADC_DIF=110 — *not* 0x66), `0x06=0x10`, `0x07=0x00` (unmute).
  Chip-ID reg `0x01` reads back → bidirectional I²C confirmed. (Values from CS42888 datasheet + NXP
  fsl_cs42888.) MFREQ left 0; fine at 96 k slave.

### Audio — SAI1 TDM
- **SAI1 pins (AF6): MCLK_A=PE2, FS_A=PE4, SCK_A=PE5, SD_A (RX, ADC in)=PD6, SD_B (TX, DAC out)=PF6.**
  *(I had SD_A/SD_B on PE6/PE3 — wrong; clocks were fine but no audio data flowed.)* Found by dumping
  stock AF6 pins across all ports.
- **Codec 24-bit words are RIGHT-justified** in the SAI DR (bits [23:0], zero-extended) — sign-extend
  from bit 23, don't `>>8`. (`audio_io.c` fixed; was ~500× too quiet.)
- **Audio input is on RX TDM slot 0** (`AUDIO_IN_SLOT=0`). RX slots 1–5 carry small/noise (the codec's
  other ADC inputs); slots 6–7 unused.
- Tap→DAC slot order: 8 taps → slots 0–7 (mix = analog op-amp sum). Per-jack order not individually
  A-B'd, but the mix is correct.

### SDRAM — FMC **bank 2**, not bank 1
- **The SDRAM is on FMC bank 2 → base 0xD0000000** (stock drives **SDCKE1=PB5, SDNE1=PB6**; PC2/PC3
  bank-1 lines are NOT used). This was the "very noisy" bug — bank-1 config wrote to nothing.
- SDCR2 = `0x1d4` (NC8/NR12/MWID16/NB4/CAS3); shared SDCLK=HCLK/2, RBURST=1, **RPIPE=1** in SDCR1;
  SDTR1 TRP2/TRC7, SDTR2 TMRD2/TXSR7/TRAS4/TWR2/TRCD2; refresh 636. Full-buffer memtest = **0 errors**.
- Gotcha: `make firmware` had no header-dep tracking → a `board.h` `SDRAM_BASE` change left a stale
  `main.o` (bank-1) → imprecise bus fault. Fixed with `-MMD -MP` in the Makefile.

### TIME MULTIPLIER — SPI2 control ADC (CV works; knob pending)
- **SPI2 control-surface ADC: CS=PB12 (GPIO), SCK=PB13, MISO=PB14** (AF5). Read = CS low → send
  `{0x01, cmd, 0x00}` → 12-bit = `((rx1 & 0x0F)<<8) | rx2`. Matches stock `read_panel_i2c_sliders`
  (sub_ecc): it does exactly **2 transfers**, cmd `0xA0` then `0xE0`, into `slider_raw_table[0..1]`.
- **Channel 0 (cmd 0xA0) = Time-CV** — CONFIRMED live: swept 980→3939 as the CV was modulated, drives
  `g_time_raw01` → the delay time glides smoothly (**the headline chorus/flanger fix, working**).
- **Coarse multiplier KNOB: still unresolved.** ch1 (cmd 0xE0) read stuck ~4094 in our firmware
  (SPI-framing flakiness when two transactions ran per loop). From the decompile, the stock multiplier
  value (`0x20001eec`) is a **combined** read: internal **ADC3** (via `sub_45bc/46f4/4798`) PLUS SPI2
  `table[0]`+`table[1]`, through a 128-sample moving average + hysteresis. So the knob is likely on the
  ADC3/4051-mux path or SPI2 ch1 — needs the mux enable/address or a clean ch1 read.

### Panel front-end (partly mapped, not yet implemented)
- **4051 mux address = GPIOA {PA0,1,7,8,11}** via `gpio_set/clear_bit_by_index` (sub_102c/sub_fe0,
  masks 0x1,0x2,0x80,0x100,0x800 on GPIOA). **ADC3 ch6 = PF8** is the mux'd ADC input — read 4095
  (floating) in our firmware, i.e. the **4051 ENABLE isn't asserted** (enable pin TBD). This is the
  path for the coarse knob + the 36 trimmers.
- **SPI1 is NOT enabled** in stock (`RCC_APB2ENR` bit12 clear) → the **LEDs are not on SPI1**; the LED
  drive mechanism is still unknown. (Driving direct GPIO did nothing.)

## Status at end of session
- `make firmware` → working image; **delay + smooth CV modulation confirmed on hardware.**
- Bench interrupted: the unit was powered down (ST-Link read 0.15 V). Last flash to the unit was
  **stock** (a correlation test) — reflash `firmware/build/fw/b288-community.hex` to get the community
  build back.

## Next (when the bench is back)
1. **Multiplier knob:** flash stock, sweep the knob, and correlate stock's `0x20001eec` (mult raw),
   `0x2000035c` (SPI table[0/1]) and ADC3 `SQR3`/`DR` — whichever moves is the knob's source. Then read
   it in our firmware (find the 4051 enable if it's the mux'd ADC3 path).
2. Rest of the panel scan (sliders, trimmers, phase/mute DIPs, A/B/C, cycle) + the momentary transport.
3. LED drive mechanism (SPI1 is out — find what stock uses).
4. Strip the SWD debug scaffolding (`sdram_memtest`, `g_dbg_*`, `adc_mult`) before a release build.
