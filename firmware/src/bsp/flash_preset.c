/* flash_preset.c — preset persistence backend: F429 INTERNAL FLASH (real).
 *
 * Slots live in flash SECTOR 3 (16 KB @ 0x0800C000) — the image is ~10 KB in
 * sector 0, so there's >38 KB of headroom before any collision (guarded below).
 * Reads are memory-mapped. A write = read the 4-slot region into RAM, update the
 * one slot, ERASE the sector, program it all back word-by-word.
 *
 * NOTE: sector erase (~0.5 s max) stalls flash reads, and the code executes from
 * flash — expect a brief audio hiccup during a save. The save-confirmation LED
 * twinkle covers it; the stock has no save feature at all, so no parity concern.
 */
#include "stm32f429xx.h"
#include "bsp.h"
#include "preset_store.h"   /* PRESET_SLOTS, PRESET_SLOT_BYTES (-Isrc) */

#define PRESET_FLASH_BASE  0x0800C000u   /* sector 3, 16 KB */
#define PRESET_SECTOR      3u
#define PRESET_REGION      (PRESET_SLOTS * PRESET_SLOT_BYTES)

_Static_assert(PRESET_SLOTS * PRESET_SLOT_BYTES <= 16384u,
               "preset region must fit one 16 KB sector");

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
