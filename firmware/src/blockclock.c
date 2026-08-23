/* blockclock.c — see blockclock.h. */
#include "blockclock.h"

void bc_init(blkclk_t *b, uint32_t cyc_hz, uint32_t win)
{
    uint32_t s = 0;
    if (win < 2u) win = 2u;
    while ((1u << (s + 1u)) <= win) s++;   /* floor(log2) — win is a pow2 */
    b->cyc_hz    = cyc_hz;
    b->win_shift = s;
    b->win       = 1u << s;
    b->t0        = 0u;
    b->n         = 0u;
    b->primed    = 0u;
    b->blk_hz    = 0u;
    b->frames    = 0u;
    b->frame_hz  = 0u;
}

int bc_tick(blkclk_t *b, uint32_t cyccnt, unsigned frames)
{
    if (!b->primed) {                      /* first call defines the origin */
        b->primed = 1u;
        b->t0 = cyccnt;
        b->n = 0u;
        return 0;
    }
    if (++b->n < b->win) return 0;

    /* Unsigned wrap is correct here: the counter is free-running 32-bit and a
     * window is ~56 M cycles, far inside its 25.6 s period. */
    uint32_t dt = cyccnt - b->t0;
    b->t0 = cyccnt;
    b->n  = 0u;

    /* Shift FIRST, then one 32-bit UDIV: cyc_hz * win would overflow, and a
     * 64-bit divide in the audio ISR would pull in __aeabi_uldivmod. Truncating
     * dt to cycles-per-block costs at most 1 part in 56,000 of resolution. */
    uint32_t per_blk = dt >> b->win_shift;
    uint32_t hz = per_blk ? (b->cyc_hz / per_blk) : 0u;
    if (hz > 0xFFFFu) hz = 0xFFFFu;

    b->blk_hz   = (uint16_t)hz;
    b->frames   = (uint16_t)((frames > 0xFFFFu) ? 0xFFFFu : frames);
    b->frame_hz = hz * (uint32_t)frames;
    return 1;
}
