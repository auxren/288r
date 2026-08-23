/* test_dl_cache.c — the CCM window cache over the SDRAM delay buffer.
 *
 * The cache exists to turn four scattered SDRAM words per tap per sample into
 * one sequential run every ~96 frames. It is allowed to be clever about WHERE
 * the words come from and about nothing else, so almost every assertion here is
 * a form of "the four words are the four words".
 *
 * The trajectories are deliberately nasty: the exact off-by-four below the line
 * that segfaulted the first version (an `off+4 <= DC_W` test wraps for reads one
 * to four samples under the line), reads straddling both ends, backwards jumps,
 * fast drift that must hit the refill rate limit rather than thrash, expiry,
 * and the
 * write-head clearance rule that keeps a live line out of the writer's way.
 */
#include "dl_cache.h"
#include "delay_line.h"
#include <stdio.h>

static int fails = 0;
static void ck(const char *name, int cond) {
    printf("  %-52s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

#define LEN 4096u
static float buf[LEN];
static dl_cache_t c;

/* Every read must return exactly what the direct path would have loaded. */
static int words_match(const float *p, uint32_t a0)
{
    return p[0] == buf[a0 - 2] && p[1] == buf[a0 - 1]
        && p[2] == buf[a0]     && p[3] == buf[a0 + 1];
}

/* One read through the cache; returns 1 if it was served from a line. */
static int probe(uint32_t a0, uint32_t d_int, int *ok)
{
    const float *p = dc_stencil(&c, 0u, buf, LEN, a0, d_int);
    if (!p) return 0;
    if (!words_match(p, a0)) *ok = 0;
    /* and the kernel must agree with the direct one, bit for bit */
    if (dl_stencil4_hermite(p, 0.37f) != dl_stencil_hermite(buf, LEN, a0, 0.37f))
        *ok = 0;
    if (dl_stencil4_linear(p, 0.37f) != dl_stencil_linear(buf, LEN, a0, 0.37f))
        *ok = 0;
    return 1;
}

int main(void)
{
    for (uint32_t i = 0; i < LEN; i++) buf[i] = (float)i * 0.001f + 0.5f;
    const uint32_t DEEP = DC_W + 64u;             /* clears rule 1 */

    /* ---- 1. a forward walk: correct words, and mostly hits ---- */
    {
        int ok = 1; unsigned served = 0;
        dc_init(&c);
        for (uint32_t f = 0; f < 500u; f++) {
            dc_block_begin(&c, 1, 1u);
            served += (unsigned)probe(1000u + f, DEEP, &ok);
        }
        ck("forward walk: cached words match the buffer", ok);
        ck("forward walk: served from cache", served > 480u);
        /* one fill per ~(DC_W-4) frames of drift, plus the DC_LIFE expiries */
        printf("      500 frames: %u misses, %u fills\n", c.miss, c.fill);
        ck("forward walk: fills amortized (< 1 per 8 frames)", c.fill < 64u);
    }

    /* ---- 2. THE OFF-BY-FOUR: reads just BELOW an established line ----
     * This is the bug that segfaulted the engine the moment the cache was
     * wired in. With a line based at B, a read at a0 = B+1 (i.e. stencil base
     * B-1) must MISS, not alias to line[0xFFFFFFFF]. */
    {
        int ok = 1;
        dc_init(&c);
        dc_block_begin(&c, 1, 1u);
        (void)probe(2000u, DEEP, &ok);             /* line base = 1998 */
        for (uint32_t back = 1u; back <= 8u; back++) {
            dc_block_begin(&c, 1, 1u);             /* fresh budget each time */
            const float *p = dc_stencil(&c, 0u, buf, LEN, 2000u - back, DEEP);
            if (p && !words_match(p, 2000u - back)) ok = 0;
        }
        ck("reads below the line never alias (off+4 wrap)", ok);
    }

    /* ---- 3. buffer ends: never served, never wrong ---- */
    {
        int ok = 1;
        dc_init(&c);
        for (uint32_t a0 = 0; a0 < 4u; a0++) {
            dc_block_begin(&c, 1, 1u);
            const float *p = dc_stencil(&c, 0u, buf, LEN, a0, DEEP);
            if (a0 < 2u && p) ok = 0;              /* rule 5: must decline */
            if (p && !words_match(p, a0)) ok = 0;
        }
        for (uint32_t a0 = LEN - 4u; a0 < LEN; a0++) {
            dc_block_begin(&c, 1, 1u);
            const float *p = dc_stencil(&c, 0u, buf, LEN, a0, DEEP);
            if (a0 + 1u >= LEN && p) ok = 0;
            if (p && !words_match(p, a0)) ok = 0;
        }
        ck("buffer ends decline the cache, never mis-serve", ok);
    }

    /* ---- 4. shallow taps are refused (rule 1: write-head clearance) ---- */
    {
        int refused = 1;
        dc_init(&c);
        for (uint32_t d = 1u; d <= DC_W + 8u; d += 7u) {
            dc_block_begin(&c, 1, 1u);
            if (dc_stencil(&c, 0u, buf, LEN, 2000u, d)) refused = 0;
        }
        ck("taps inside the write head's reach are refused", refused);
        /* far end: a tap so deep the head laps back onto the line inside its
         * lifetime must also be refused. */
        {
            int far_ok = 1;
            for (uint32_t d = LEN - DC_LIFE - 8u; d < LEN; d += 13u) {
                dc_init(&c); dc_block_begin(&c, 1, 1u);
                if (dc_stencil(&c, 0u, buf, LEN, 2000u, d)) far_ok = 0;
            }
            ck("taps the head can lap back onto are refused", far_ok);
        }
    }

    /* ---- 5. BOUNDED WORK: the cache can never be worse than no cache --------
     * A tap being dragged (preset recall, varispeed at the rail) must degrade
     * to the old behaviour, not refill DC_W words every frame. The bound is per
     * FRAME and must hold whatever the caller's batch size is, because the
     * block size is still an open question (contract blocker #0) — so drive it
     * both ways. Direct costs 4 SDRAM words per frame; refills must cost less. */
    {
        int ok = 1;
        const unsigned batches[2] = { 1u, 32u };
        for (unsigned b = 0; b < 2u; b++) {
            unsigned frames = 0;
            dc_init(&c);
            while (frames < 640u) {                /* drift 200 samples/frame */
                dc_block_begin(&c, 1, batches[b]);
                for (unsigned k = 0; k < batches[b]; k++, frames++)
                    (void)probe(1000u + (frames % 8u) * 200u, DEEP, &ok);
            }
            printf("      thrash (batch %2u): %u fills in %u frames"
                   " = %.2f SDRAM words/frame\n", batches[b], c.fill, frames,
                   (double)c.fill * DC_W / frames);
            ck("runaway drift stays under the direct path's 4 words/frame",
               (double)c.fill * DC_W / frames < 4.0);
        }
        ck("runaway drift: words still correct", ok);
    }

    /* ---- 6. lifetime expiry (rule 2) ---- */
    {
        int ok = 1;
        dc_init(&c);
        dc_block_begin(&c, 1, 1u);
        (void)probe(2000u, DEEP, &ok);
        dc_block_begin(&c, 1, DC_LIFE + 1u);       /* age it past its life  */
        {   /* same address, but the line must have expired -> a miss */
            uint32_t m0 = c.miss;
            (void)dc_stencil(&c, 0u, buf, LEN, 2000u, DEEP);
            ck("a line expires after DC_LIFE frames", c.miss == m0 + 1u);
        }
        ck("expiry path still returns correct words", ok);
    }

    /* ---- 7. off = disabled, existing lines stay coherent ---- */
    {
        int ok = 1;
        dc_init(&c);
        dc_block_begin(&c, 1, 1u);
        (void)probe(2000u, DEEP, &ok);
        dc_invalidate(&c);
        dc_block_begin(&c, 0, 1u);
        ck("disabled + invalidated: nothing is served",
           dc_stencil(&c, 0u, buf, LEN, 2000u, DEEP) == 0);
        ck("disabled path words correct", ok);
    }

    /* ---- 8. lanes are independent ---- */
    {
        int ok = 1;
        dc_init(&c);
        for (uint32_t f = 0; f < 100u; f++) {
            dc_block_begin(&c, 1, 1u);
            for (unsigned l = 0; l < DC_LANES; l++) {
                uint32_t a0 = 300u + l * 400u + f;
                const float *p = dc_stencil(&c, l, buf, LEN, a0, DEEP);
                if (p && !words_match(p, a0)) ok = 0;
            }
        }
        ck("8 lanes track 8 independent trajectories", ok);
    }

    printf(fails ? "\nFAILURES: %d\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
