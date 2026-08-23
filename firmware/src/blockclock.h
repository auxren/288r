/* blockclock.h — measure the audio block clock against a known-rate counter.
 *
 * WHY THIS EXISTS. Two "obviously true" numbers in this codebase contradict
 * each other: src/bsp/board.h says a block is BLOCK_FRAMES (16) frames and
 * SAMPLE_RATE_HZ is 96000 (=> 6000 ISR calls/s), while the lead counted 2999
 * calls/s on the unit. Every cycle budget, every ms-denominated constant and
 * the still-open "192k head advance" bench mystery hang off which one is real.
 * Arguing about it is worthless; the firmware measures it and publishes the
 * answer where one SWD read can see it (board.h has the decode table).
 *
 * The known-rate counter is DWT_CYCCNT: it runs at HCLK (168 MHz), it is
 * already enabled for the ISR load telemetry, and it costs one load to read.
 * Nothing else on the board is a trustworthier time base at boot.
 *
 * Pure integer, no BSP, no float: host-tested by test/test_blockclock.c.
 */
#ifndef BLOCKCLOCK_H
#define BLOCKCLOCK_H

#include <stdint.h>

typedef struct {
    uint32_t cyc_hz;     /* counter rate (HCLK)                              */
    uint32_t win;        /* blocks per measurement, MUST be a power of two   */
    uint32_t win_shift;  /* log2(win) — the divide-free half of the math     */
    uint32_t t0;         /* counter value at the start of the window         */
    uint32_t n;          /* blocks counted into the current window           */
    uint8_t  primed;     /* first window is discarded (t0 was never set)     */
    /* published results (valid after the first full window) */
    uint16_t blk_hz;     /* blocks per second                                */
    uint16_t frames;     /* frames per ISR call, as delivered by the DMA     */
    uint32_t frame_hz;   /* blk_hz * frames = the true audio frame rate      */
} blkclk_t;

/* win must be a power of two (see win_shift); cyc_hz is the counter's rate. */
void bc_init(blkclk_t *b, uint32_t cyc_hz, uint32_t win);

/* Call once per audio block from the ISR, with the current free-running
 * counter value and the block's frame count. Returns 1 on the block that
 * publishes a new measurement (i.e. when a window closed), else 0.
 * Cost off the measurement block: one increment and one compare. */
int bc_tick(blkclk_t *b, uint32_t cyccnt, unsigned frames);

#endif /* BLOCKCLOCK_H */
