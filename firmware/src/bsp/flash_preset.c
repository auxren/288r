/* flash_preset.c — preset persistence backend.
 *
 * [BENCH] RAM-backed PLACEHOLDER. It lets the save-chord + A/B/C/D recall UX be
 * validated on hardware within one power cycle, but is NOT persistent across reboot.
 * To make presets reboot-proof, replace this with the F429 internal-flash EEPROM
 * emulation: reserve a flash sector (e.g. a high 128 KB sector, well clear of the
 * ~16 KB image), and on write do FLASH unlock -> erase sector -> program `blob` at
 * slot*PRESET_SLOT_BYTES -> lock. Reads stay memory-mapped, so bsp_preset_flash_base
 * would return the sector's 0x080x_xxxx address instead of this RAM array.
 */
#include "bsp.h"
#include "preset_store.h"   /* PRESET_SLOTS, PRESET_SLOT_BYTES (-Isrc) */

/* Zero-initialised: an all-zero slot fails the storage magic check -> preset_load
 * returns defaults, so unwritten slots recall the A-ramp. */
static uint8_t g_preset_ram[PRESET_SLOTS * PRESET_SLOT_BYTES];

const uint8_t *bsp_preset_flash_base(void)
{
    return g_preset_ram;
}

int bsp_preset_flash_write(unsigned slot, const uint8_t *blob, unsigned len)
{
    if (slot >= PRESET_SLOTS || len > PRESET_SLOT_BYTES) return -1;
    uint8_t *d = &g_preset_ram[slot * PRESET_SLOT_BYTES];
    for (unsigned i = 0; i < len; i++) d[i] = blob[i];
    return 0;
}
