# 2. Installation & flashing


> **Your presets survive updates.** Saved presets (and future calibration data) live in a
> dedicated flash sector at the top of the chip, physically separate from the firmware image.
> Reflashing any release — by any of the supported tools — never touches it, and the build
> system refuses to produce an image large enough to collide with it.

## Module installation

The 288r is a Buchla-format (4U, 1.2 A / 15 V rail) module. Install it in a powered-down Buchla
case, seat the power connector in the correct orientation, and power up. Nothing about the community
firmware changes the power or mechanical fit.

## Firmware: what you're flashing

The stock firmware shipped from the factory is preserved in this repo as the **golden restore image**
(`Compiled FW/B288-REV1.0.hex`). The community firmware and the experimental interpolation patch are
separate images. **Flashing is optional and reversible** — the stock image can always be restored.

| Image | What it is |
|---|---|
| `Compiled FW/B288-REV1.0.hex` | Factory firmware, unmodified. Your safe restore point. |
| `Compiled FW/B288-REV1.0-interp-EXPERIMENTAL.hex` | Stock firmware + the fractional-interpolation patch (smooth TIME-mode modulation). Experimental. |
| Community firmware (`firmware/`) | The full rewrite — **released (v1.0.1)** and running on hardware. Take the `.hex`/`.bin` or the one-click flasher zip from the tagged GitHub release, or build it with `make firmware`. |

## What you need

- An **ST-Link/V2** (shipped with the kit) or compatible SWD programmer.
- The **2×10 STLINK** header (or the **2×3 DEBUG** header) on the right edge of the mainboard.
- **OpenOCD** or **STM32CubeProgrammer** on your computer.

The MCU is an **STM32F429ZET6**. SWD is open (read-protect Level 0) on shipped units, so no unlock is
needed. **BOOT0** + the ROM bootloader is an additional recovery path if SWD ever fails.

## Flashing (OpenOCD)

> Always keep a copy of `Compiled FW/B288-REV1.0.hex` before you flash anything.

**Back up the running image first** (optional but recommended):

```bash
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
  -c "init; reset halt; flash read_bank 0 my-backup.bin; reset run; shutdown"
```

**Flash an image:**

```bash
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
  -c "program 'Compiled FW/B288-REV1.0-interp-EXPERIMENTAL.hex' verify reset exit"
```

**Restore stock** (the same command with the golden image):

```bash
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
  -c "program 'Compiled FW/B288-REV1.0.hex' verify reset exit"
```

`verify` re-reads the flash and checks it byte-for-byte; `reset exit` restarts the module. If the
codec/SDRAM don't initialize on boot, power-cycle the case once after flashing.

## Is there a brick risk?

Low. The F429's SWD and BOOT0 ROM bootloader are independent of whatever is in flash, so a bad
application image can always be overwritten. You would only lose recoverability by **setting
read-protect (RDP) to Level 2**, which is permanent — *do not* change option bytes. Ordinary flashing
never touches them.

See [Troubleshooting](08-troubleshooting.md) for recovery if a flash goes wrong.

> **No flashing tool installed? The flasher handles it.** If none of the supported tools is
> present, the launcher offers to install one for you (Homebrew/apt/dnf/winget where
> available, or a pip-installed stack that needs no admin rights) — and it bundles a driver
> that supports the older ST-Link dongles that ship with 288r kits. Folders with spaces in
> the name (hello, "flasher 3") are fine.
