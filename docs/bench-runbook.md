# Bench-day runbook (first hardware/SWD session)

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
