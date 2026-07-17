# Bench-day runbook (first hardware/SWD session)

> **For the 2026-07-17 session (panel + config-DIP + pitch bring-up on the *community*
> firmware), jump to [§ Session: panel + config + pitch bring-up](#session-panel--config--pitch-bring-up)
> at the bottom.** Sections 0–4 below are the original stock-patch session (mostly resolved by
> bench sessions 1–3) — do §0 preflight first, then skip to the new section.

Goal: with a 288r + ST-Link in front of you, resolve the remaining unknowns and validate the
interpolation patch in one efficient pass. Work top to bottom. **Always keep the stock hex as restore.**

Tools: ST-Link V2, `openocd` (or STM32CubeProgrammer), and — for the codec question — a logic
analyzer / scope. Repo checked out.

---

## 0. Don't-brick preflight
- [ ] Connect ST-Link, power the module, `openocd -f interface/stlink.cfg -f target/stm32f4x.cfg`.
- [ ] In a second terminal: `telnet localhost 4444` → `reset halt`.
- [ ] **Read RDP / option bytes** before anything that could erase: `mdw 0x1FFFC000 1`
      (RDP is bits [15:8]; `0xAA` = Level 0 / open). If not open, STOP — do not proceed.

## 1. Dump + diff the stock firmware (the patch go/no-go)
- [ ] `dump_image stock_dump.bin 0x08000000 0x80000`   (512 KB — F429ZET6)
- [ ] Convert our reference hex to bin and diff:
      `arm-none-eabi-objcopy -I ihex -O binary "Compiled FW/B288-REV1.0.hex" ref.bin`
      `cmp -l stock_dump.bin ref.bin | head`  (expect: identical over the used range 0x0..0x6D08)
- [ ] **MATCH → the patch is valid for this unit. DIFFERS → capture the diff and report it before flashing.**

## 2. Harvest the facts that unblock the firmware (do this while you're in there)
- [ ] **HSE crystal frequency** — read the can marking (or scope OSC_IN). Note it (clock tree needs it).
- [ ] **Codec control bus** (resolves the open question): put the logic analyzer on the CS42888 at
      **power-up**:
      - I²C candidate: SDA/SCL. SPI candidate: CS/SCLK/SDO. Capture the first ~100 ms after reset.
      - If you see transactions → note the **device address + the exact register writes** (this is
        gold — it's the codec init we couldn't get statically). If the lines are idle → it runs on
        defaults/strapping.
      - Also read the CS42888 **mode-select + address strap pins** (tied hi/lo) → I²C-vs-SPI + address.
- [ ] **SAI/TDM config** — while halted, dump SAI2 regs (`mdw 0x40015804 8` block A, `0x40015824` block
      B) to read the live TDM slot count / frame / data size. Cross-check the channel→tap order.
- [ ] **Pinout spot-checks** — confirm a few switch→pin mappings from `re/notes/hardware.md`
      (cal.=GPIOB10, cycle=GPIOB11, resolution=GPIOD11/12) by toggling switches and reading
      `mdw 0x40020410 1` (GPIOB IDR) / `0x40020c10` (GPIOD IDR).
- [ ] **Control ADC ranges** (for calibration) — with the SPI2 ADC path, log slider/pot raw values at
      min and max travel.

## 3. Validate the interpolation patch (see docs/validation-checklist.md)
- [ ] Flash: `program "Compiled FW/B288-REV1.0-interp-EXPERIMENTAL.hex" verify reset exit`
- [ ] Run the full checklist: boots, all modes OK, **TIME sweep now glides (A/B vs stock)**, both
      channels, and **no dropouts under load** (all taps + both paths + fast mod + feedback).
- [ ] Restore: `program "Compiled FW/B288-REV1.0.hex" verify reset exit`
- [ ] File a **"Release validation (bench)"** issue with the results.

## 4. Report back (unblocks the community firmware)
Post/commit: HSE freq, codec bus + address + register capture, SAI/TDM slot map, any corrected
switch pins, ADC min/max ranges, and the patch verdict. That closes essentially every remaining
`TODO(bench)`.

---

## Reference — SDRAM timings (IS42S16400J-7TLI, so FMC init is ready)
4M × 16 (64 Mbit), 4 banks, -7 grade (143 MHz / 7 ns), CL2/CL3. Datasheet mins (ns):
tRCD 20, tRP 20, tRAS 42 (max 100 µs), tRC 63, tRFC 66, tMRD 2 clk, tWR/tDPL 1 clk + 7 ns, tXSR 70.
Refresh: 8192 rows / 64 ms → 7.8125 µs/row. **Compute FMC_SDTR/SDCR cycle counts once SDCLK is fixed**
(SDCLK = HCLK/2; e.g. at 90 MHz SDCLK, 1 clk ≈ 11.1 ns → tRCD/tRP≈2, tRAS≈4, tRC≈6, tRFC≈6; refresh
count = 7.8125 µs × SDCLK − 20). CAS latency 2 or 3 (test). Data bus 16-bit, so a 32-bit sample =
2 accesses (another reason the delay buffer stores int16 — see firmware/DESIGN.md).

## Reference — quick openocd cheatsheet
```
reset halt
mdw <addr> <count>          # read words
dump_image f.bin 0x08000000 0x80000
program <file> verify reset exit
```

---

# Session: panel + config + pitch bring-up

For the community firmware (`firmware/build/fw/b288-community.hex`) after the 2026-07-17 build-out
(config DIP sw1/sw2, live 165 scan, LED framework, gated pitch voice). Do §0 preflight first.
Everything here is labelling the `[BENCH]` markers those commits left. **Keep the stock hex as restore.**

The firmware exposes a live decode over SWD — read it with gdb `p g_dbg_panel` (or
`mdw &g_dbg_panel <n>`) while toggling controls. That struct is the labelling tool for steps 2–5.

**Even better for the shift-register chains — capture with the Saleae and self-label:** probe the
165 (PA4/5/6) and SPI2 (PB12/13/14 + MOSI) buses, export a Logic 2 digital CSV, and run
`python3 re/scripts/saleae_decode.py 165 cap.csv --latch 0 --clk 1 --data 2 --changes` (toggle one
switch → it prints exactly which bit moved) / `... spi cap.csv --cs 0 --sck 1 --miso 2 --mosi 3`
(prints the 12-bit ADC value + channel). This is hardware ground truth, not the firmware's guessed
decode. (`--selftest` validates the decoders offline.)

## A. Flash + regression-check (must pass before anything else)
- [ ] `cd firmware && make firmware` → flash `build/fw/b288-community.hex`.
- [ ] **Audio still works**: input passes, multitap delay echoes, multiplier knob is smooth.
- [ ] **Knob at noon = 1.0×** (taper calibration) — read `g_dbg_panel.mult` ≈ 1.0 at 12 o'clock.
- [ ] **Audio SURVIVES the live 165 scan.** If audio drops on this build (it scans PA4/5/6 now),
      PA4/5/6 overlap the codec-reset-release block → set `PANEL_SCAN_ENABLE 0` in main.c, reflash,
      and note it. (This is the one regression risk in the new build.)

## B. Label the 13-bit 165 switch map (unblocks octave/preset/mode/transport)
- [ ] Watch `g_dbg_panel.sw165` (raw) + `.preset/.octave/.bank_b/.write_trig/.recirc_trig` (decoded).
- [ ] Toggle **A/B/C** → confirm which raw bits move and that `.preset` follows; fix `B_PRESET_*` +
      polarity in `panel_ctl.c` if not. Verify presets audibly reposition the taps.
- [ ] Toggle **octave ×1/×2** (and ×4 if present) → confirm `B_OCT0/1` + the encoding; verify the
      echo spacing halves/doubles smoothly (no glitch — that's the fixed-rate win).
- [ ] Toggle the **TIME/pitch mode** switch and the momentary **SW14/SW16** → find their bits in
      `sw165` → they become the mode gate (step E) and WRITE/RECIRC transport (future).
- [ ] Commit the corrected bit map + polarity to `panel_ctl.c` and `re/notes/panel-scan.md`.

## C. Trace config DIP sw1 (×10 extend) + sw2 (bandwidth) pins
- [ ] Flip **sw1** alone; find the GPIO that changes (scope, or `mdw` the GPIO IDRs while toggling).
      Set `SW_EXTEND_PORT/PIN` + `SW_EXTEND_MAPPED 1` in `board.h`. Reflash → confirm delay time ×10.
- [ ] Flip **sw2** alone; likewise set `SW_BANDWIDTH_*` + `_MAPPED 1`. Reflash → confirm audible
      high-frequency rolloff (11025 Hz). Tune `BANDWIDTH_LIMIT_HZ` / filter order to taste vs stock.

## D. Find the 595 LED bits (walking-1 sweep) — CAREFUL, output side
- [ ] Set `PANEL_LED_WALK 1` + `PANEL_LED_ENABLE 1` in main.c, reflash.
- [ ] One bit walks ~1/s. Note step→LED to fill `LED_BIT[]` in `led.c`.
- [ ] **If audio drops at a particular step, that bit is codec-reset / mux-enable — NOT an LED.**
      Record it as a must-hold bit (add to the `extra` mask passed to `led_word`), never toggle it.
- [ ] Set `PANEL_LED_WALK 0`, map real LED meanings, reflash, confirm.

## E. Hear the pitch voice
- [ ] Calibrate Pitch-CV: read `g_dbg_panel.spi_cv` at 0 V and at a known voltage → set
      `PITCH_CV_CENTER` + `PITCH_CV_VOLTS_PER_CODE` in `board.h`.
- [ ] Set `PITCH_VOICE_ENABLE 1`, reflash. Feed Pitch-CV; listen on the mixed out (voice sums into
      ch0). Confirm **1.2 V/oct**: +1.2 V should be one octave up (ratio 2), not +1.0 V.
- [ ] Gate CV routing on the TIME/pitch mode bit found in step B (so the Time-CV drives pitch in
      pitch mode, delay-time otherwise).

## F. Report back
Update `panel_ctl.c` (bit map/polarity), `board.h` (pins + cal), `led.c` (LED_BIT + must-hold),
`re/notes/panel-scan.md`, and `CLAUDE.md`. Then strip the `g_dbg_*` scaffolding before any release.
