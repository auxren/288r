/* test_blockclock.c — the block-clock self-check (blocker #0).
 *
 * The point of this module is to settle "96 kHz with 16-frame blocks" versus
 * "48 kHz with 16-frame blocks" versus "32-frame blocks" on the actual unit,
 * so the test drives it with each of those three worlds and asserts it names
 * the right one. If this arithmetic is wrong, the SWD reading that settles the
 * argument is wrong, and every cycle budget downstream inherits the error.
 */
#include "blockclock.h"
#include <stdio.h>

static int fails = 0;
static void ck(const char *what, int cond) {
    printf("  %-58s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

#define HCLK 168000000u

/* Run `n` blocks of a world where the frame rate is fs and a block is
 * `frames` frames, and return the published measurement. */
static void run(blkclk_t *b, unsigned fs, unsigned frames, unsigned n,
                uint32_t start)
{
    uint32_t cyc = start;
    const unsigned blocks_per_s = fs / frames;
    const uint32_t per_block = HCLK / blocks_per_s;
    for (unsigned i = 0; i < n; i++) {
        bc_tick(b, cyc, frames);
        cyc += per_block;
    }
}

int main(void)
{
    blkclk_t b;

    /* ---- the predicted world: 48 kHz, 16-frame blocks ------------------- */
    bc_init(&b, HCLK, 1024u);
    run(&b, 48000u, 16u, 2100u, 0u);
    printf("    48k/16: blk_hz=%u frames=%u frame_hz=%u\n",
           b.blk_hz, b.frames, b.frame_hz);
    ck("48 kHz / 16-frame blocks reads ~3000 blocks/s",
       b.blk_hz >= 2990u && b.blk_hz <= 3010u);
    ck("...and ~48000 frames/s", b.frame_hz >= 47800u && b.frame_hz <= 48200u);
    ck("...and reports the DMA's own frame count", b.frames == 16u);

    /* ---- the rival world: 96 kHz, 16-frame blocks ----------------------- */
    bc_init(&b, HCLK, 1024u);
    run(&b, 96000u, 16u, 2100u, 0u);
    printf("    96k/16: blk_hz=%u frames=%u frame_hz=%u\n",
           b.blk_hz, b.frames, b.frame_hz);
    ck("96 kHz / 16-frame blocks reads ~6000 blocks/s",
       b.blk_hz >= 5980u && b.blk_hz <= 6020u);
    ck("...and ~96000 frames/s", b.frame_hz >= 95600u && b.frame_hz <= 96400u);

    /* ---- the other rival: 96 kHz with 32-frame blocks ------------------- */
    bc_init(&b, HCLK, 1024u);
    run(&b, 96000u, 32u, 2100u, 0u);
    printf("    96k/32: blk_hz=%u frames=%u frame_hz=%u\n",
           b.blk_hz, b.frames, b.frame_hz);
    ck("32-frame blocks are distinguishable by frames alone", b.frames == 32u);
    ck("...3000 blocks/s but 96000 frames/s — the case that would fool a "
       "blocks-only reading",
       b.blk_hz >= 2990u && b.blk_hz <= 3010u && b.frame_hz > 95000u);

    /* ---- publication cadence ------------------------------------------- */
    {
        blkclk_t c;
        bc_init(&c, HCLK, 1024u);
        int pubs = 0;
        uint32_t cyc = 0;
        for (unsigned i = 0; i < 1024u; i++) { pubs += bc_tick(&c, cyc, 16u);
                                               cyc += 56000u; }
        ck("nothing is published before a full window closes", pubs == 0);
        for (unsigned i = 0; i < 1024u; i++) { pubs += bc_tick(&c, cyc, 16u);
                                               cyc += 56000u; }
        ck("exactly one publication per window", pubs == 1);
    }

    /* ---- the 32-bit counter wraps every 25 s: that must not matter ------ */
    bc_init(&b, HCLK, 1024u);
    run(&b, 48000u, 16u, 2100u, 0xFFFF0000u);   /* window straddles the wrap */
    printf("    wrap:   blk_hz=%u frame_hz=%u\n", b.blk_hz, b.frame_hz);
    ck("a DWT wrap inside the window does not corrupt the reading",
       b.blk_hz >= 2990u && b.blk_hz <= 3010u);

    /* ---- a stalled counter must not divide by zero ---------------------- */
    {
        blkclk_t c;
        bc_init(&c, HCLK, 4u);
        for (int i = 0; i < 20; i++) bc_tick(&c, 12345u, 16u);
        ck("a stopped cycle counter reads 0 Hz instead of faulting",
           c.blk_hz == 0u && c.frame_hz == 0u);
    }

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
