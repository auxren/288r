/* flash_preset.c — preset persistence backend: F429 INTERNAL FLASH (real).
 *
 * HARD REQUIREMENT (owner, matching the MARF firmware): presets/calibration
 * SURVIVE every firmware update. Three-layer guarantee:
 *   1. the store lives in the TOP sector (7, 128 KB @ 0x08060000) — as far
 *      from the growing image as this part allows;
 *   2. the linker caps the image at 384 KB (STM32F429.ld FLASH LENGTH), so an
 *      image that would touch the store is a BUILD ERROR, not a silent wipe;
 *   3. flash tools (openocd `program`, st-flash, CubeProgrammer -w) erase only
 *      the sectors the image covers — never the top sector.
 * A one-time boot migration (bsp_preset_flash_migrate) carries records saved
 * by older builds at the previous home (sector 3 @ 0x0800C000) into sector 7.
 * Reads are memory-mapped. A write = read the region into RAM, update the
 * one slot, ERASE the sector, program it all back word-by-word.
 *
 * NOTE: sector erase (~0.5 s max) stalls flash reads, and the code executes from
 * flash — expect a brief audio hiccup during a save. The save-confirmation LED
 * twinkle covers it; the stock has no save feature at all, so no parity concern.
 */
#include "stm32f429xx.h"
#include "bsp.h"
#include "preset_store.h"   /* PRESET_SLOTS, PRESET_SLOT_BYTES (-Isrc) */

#define PRESET_FLASH_BASE  0x08060000u   /* sector 7, 128 KB (top) */
#define PRESET_SECTOR      7u
#define PRESET_OLD_BASE    0x0800C000u   /* pre-migration home (sector 3) */
#define PRESET_REGION      (PRESET_SLOTS * PRESET_SLOT_BYTES)

_Static_assert(PRESET_SLOTS * PRESET_SLOT_BYTES <= 131072u,
               "preset region must fit the 128 KB top sector");

const uint8_t *bsp_preset_flash_base(void)
{
    return (const uint8_t *)PRESET_FLASH_BASE;
}

static void flash_unlock(void)
{
    if (FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR = 0x45670123u;
        FLASH->KEYR = 0xCDEF89ABu;
    }
}

static void flash_wait(void)
{
    while (FLASH->SR & FLASH_SR_BSY) { }
}

int bsp_preset_flash_write(unsigned slot, const uint8_t *blob, unsigned len)
{
    if (slot >= PRESET_SLOTS || len > PRESET_SLOT_BYTES) return -1;

    /* snapshot the whole region, patch the one slot */
    static uint8_t cache[PRESET_REGION];
    const uint8_t *rd = (const uint8_t *)PRESET_FLASH_BASE;
    for (unsigned i = 0; i < PRESET_REGION; i++) cache[i] = rd[i];
    for (unsigned i = 0; i < len; i++)
        cache[slot * PRESET_SLOT_BYTES + i] = blob[i];
    for (unsigned i = len; i < PRESET_SLOT_BYTES; i++)
        cache[slot * PRESET_SLOT_BYTES + i] = 0xFFu;

    flash_unlock();
    flash_wait();
    FLASH->SR = FLASH_SR_EOP | FLASH_SR_SOP | FLASH_SR_WRPERR |
                FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR;

    /* erase sector 3 (PSIZE = x32: VDD is 3.3 V) */
    FLASH->CR = (FLASH->CR & ~(FLASH_CR_PSIZE | FLASH_CR_SNB)) |
                FLASH_CR_PSIZE_1 | (PRESET_SECTOR << FLASH_CR_SNB_Pos) |
                FLASH_CR_SER;
    FLASH->CR |= FLASH_CR_STRT;
    flash_wait();
    FLASH->CR &= ~(FLASH_CR_SER | FLASH_CR_SNB);

    /* program the region back, one word at a time */
    FLASH->CR = (FLASH->CR & ~FLASH_CR_PSIZE) | FLASH_CR_PSIZE_1 | FLASH_CR_PG;
    volatile uint32_t *dst = (volatile uint32_t *)PRESET_FLASH_BASE;
    const uint32_t *src = (const uint32_t *)cache;
    for (unsigned i = 0; i < PRESET_REGION / 4u; i++) {
        dst[i] = src[i];
        flash_wait();
    }
    FLASH->CR &= ~FLASH_CR_PG;
    FLASH->CR |= FLASH_CR_LOCK;

    /* verify */
    for (unsigned i = 0; i < PRESET_REGION; i++)
        if (rd[i] != cache[i]) return -2;
    return 0;
}

/* One-time migration from the old sector-3 home: if the NEW region is blank
 * (all 0xFF) and the OLD region holds anything, copy it across. Runs once per
 * boot before the first preset read; a no-op forever after (new region only
 * blank on the first boot of this layout — or after a full chip erase, when
 * the old region is blank too). */
void bsp_preset_flash_migrate(void)
{
    const uint8_t *nw = (const uint8_t *)PRESET_FLASH_BASE;
    const uint8_t *od = (const uint8_t *)PRESET_OLD_BASE;
    int new_blank = 1, old_blank = 1;
    for (unsigned i = 0; i < PRESET_REGION; i++) {
        if (nw[i] != 0xFFu) { new_blank = 0; break; }
    }
    if (!new_blank) return;
    for (unsigned i = 0; i < PRESET_REGION; i++) {
        if (od[i] != 0xFFu) { old_blank = 0; break; }
    }
    if (old_blank) return;

    flash_unlock();
    flash_wait();
    FLASH->SR = FLASH_SR_EOP | FLASH_SR_SOP | FLASH_SR_WRPERR |
                FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR;
    FLASH->CR = (FLASH->CR & ~(FLASH_CR_PSIZE | FLASH_CR_SNB)) |
                FLASH_CR_PSIZE_1 | (PRESET_SECTOR << FLASH_CR_SNB_Pos) |
                FLASH_CR_SER;
    FLASH->CR |= FLASH_CR_STRT;
    flash_wait();
    FLASH->CR &= ~(FLASH_CR_SER | FLASH_CR_SNB);
    FLASH->CR = (FLASH->CR & ~FLASH_CR_PSIZE) | FLASH_CR_PSIZE_1 | FLASH_CR_PG;
    volatile uint32_t *dst = (volatile uint32_t *)PRESET_FLASH_BASE;
    const uint32_t *src = (const uint32_t *)PRESET_OLD_BASE;
    for (unsigned i = 0; i < PRESET_REGION / 4u; i++) {
        dst[i] = src[i];
        flash_wait();
    }
    FLASH->CR &= ~FLASH_CR_PG;
    FLASH->CR |= FLASH_CR_LOCK;
}
