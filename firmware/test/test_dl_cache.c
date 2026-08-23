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
#include "bsp/board.h"   /* BLOCK_FRAMES: the burst ceiling is per BLOCK */
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
     * both ways. Direct costs 4 SDRAM words per frame; refills must cost less.
     *
     * QA 2026-08-23 — THE ACCOUNTING WAS HALF THE STORY. Fill traffic alone was
     * compared against 4 words/frame, and the rate limiter does bound that at
     * DC_W/DC_MIN_SPAN = 3. But a lane that is refused a refill does not stop
     * reading: it falls through to the DIRECT 4-word stencil, and the two costs
     * ADD. The honest figure is
     *
     *      total = fills * DC_W + 4 * (misses not served)
     *
     * and at a drift the line cannot outrun it exceeds 4.00 — i.e. the header's
     * "cannot be worse than no cache, ... varispeed at the 4.0 rail" is false at
     * exactly the rail it names. Both figures are printed below; the engine-level
     * measurement and the enforcement live in test_isr_budget.c. */
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
            double fillw = (double)c.fill * DC_W / frames;
            double total = ((double)c.fill * DC_W
                            + 4.0 * (double)(c.miss - c.fill)) / frames;
            printf("      thrash (batch %2u): %u fills, %u misses in %u frames"
                   " = %.2f fill + %.2f direct = %.2f SDRAM words/frame"
                   " (direct path: 4.00)\n",
                   batches[b], c.fill, c.miss, frames, fillw, total - fillw,
                   total);
            ck("refill traffic alone stays under 4 words/frame", fillw < 4.0);
        }
        ck("runaway drift: words still correct", ok);

        /* The worst drift a shipped feature can produce: varispeed at the 4.0
         * rail moves a tap's read position 4 samples per frame, which walks off
         * a DC_W line in (DC_W-4)/4 = 23 frames while the rate limiter holds the
         * refill for DC_MIN_SPAN = 32. The 9 frames in between are direct reads
         * ON TOP of the fill. This is a measurement, not a threshold: it prints
         * the number the design claim has to beat. */
        {
            unsigned frames = 0;
            int ok2 = 1;
            dc_init(&c);
            /* a0 must stay inside the buffer (rule 5 declines at the ends
             * without counting a miss, which would flatter the figure) and
             * d_int must clear both halves of rule 1 for LEN 4096. */
            while (frames < 3200u) {
                dc_block_begin(&c, 1, 1u);
                (void)probe(100u + (frames * 4u) % 3700u, 2000u, &ok2);
                frames++;
            }
            double total = ((double)c.fill * DC_W
                            + 4.0 * (double)(c.miss - c.fill)) / frames;
            printf("      SUSTAINED drift 4/frame (varispeed rail):"
                   " %.3f SDRAM words/frame vs 4.000 direct  %s\n",
                   total, total >= 4.0 ? "<-- NO BETTER THAN NO CACHE" : "");
            ck("drift-4 words are still correct", ok2);
        }
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

    /* ---- 7. OFF MEANS OFF, including for lines that are already valid ----
     * `enable` is the coherence kill switch (rule 3): the caller is saying
     * "something else is writing where the taps read". A cached line is exactly
     * what must NOT be served then.
     *
     * The previous version of this case called dc_invalidate() BEFORE
     * dc_block_begin(&c, 0, 1) and then asserted that nothing was served — so
     * it tested the invalidate, not the disable, and could not fail. It could
     * not see that dc_stencil's hit test ran before its `on` check and happily
     * handed back a stale line from a disabled cache. Establish a line, mutate
     * the buffer underneath it, disable, and demand the fresh value. */
    {
        int ok = 1;
        dc_init(&c);
        dc_block_begin(&c, 1, 1u);
        (void)probe(2000u, DEEP, &ok);
        ck("precondition: the line is live and serving", 
           dc_stencil(&c, 0u, buf, LEN, 2000u, DEEP) != 0);
        float save = buf[2000u];
        buf[2000u] = -999.0f;                 /* a foreign writer */
        dc_block_begin(&c, 0, 1u);
        ck("disabled: a valid line is not served",
           dc_stencil(&c, 0u, buf, LEN, 2000u, DEEP) == 0);
        dc_block_begin(&c, 1, 1u);
        {   /* and it is not resurrected when the cache comes back on */
            const float *p = dc_stencil(&c, 0u, buf, LEN, 2000u, DEEP);
            ck("re-enabled: no stale line survives the disable",
               p == 0 || p[2] == -999.0f);
        }
        buf[2000u] = save;
        ck("disabled path words correct", ok);
    }

    /* ---- 9. RULE A: a lane that cannot hold a line goes direct ----------
     * The bound the cache claims rests on this: a fill costs DC_W words and is
     * only worth issuing if the line survives DC_W/4 frames of service. Drag a
     * lane's read position fast enough that no line survives, and the lane must
     * stop refilling (bar the cheap periodic re-probe) instead of paying for a
     * new line every DC_MIN_SPAN frames on top of the direct reads it is doing
     * anyway. That "on top of" is the arithmetic the first version of this
     * module got wrong. */
    {
        int ok = 1;
        dc_init(&c);
        uint32_t a0 = 1000u;
        unsigned fills = 0;
        for (uint32_t f = 0; f < DC_REPROBE - 1u; f++) {
            dc_block_begin(&c, 1, 1u);
            a0 += DC_W;                       /* one whole line every frame */
            if (a0 > LEN - DC_W - 8u) a0 = 1000u;
            uint32_t f0 = c.fill;
            const float *p = dc_stencil(&c, 0u, buf, LEN, a0, DEEP);
            if (p && !words_match(p, a0)) ok = 0;
            fills += (c.fill != f0);
        }
        printf("    dragged lane: %u fills in %u frames (rate-limit alone would"
               " allow %u)\n", fills, DC_REPROBE - 1u,
               (DC_REPROBE - 1u) / DC_MIN_SPAN);
        ck("a lane that cannot hold a line stops refilling", fills <= 2u);
        ck("...and still returns correct words while it does", ok);
    }

    /* ---- 10. RULE B: the per-block refill burst is capped ---------------
     * Every transport entry invalidates all eight lanes at once, so without a
     * ceiling they re-phase onto the same frame: 8 x DC_W = 768 SDRAM words in
     * ONE frame, 21% of a whole block budget in refills. The deadline is per
     * block, so this is the bound that matters. */
    {
        dc_init(&c);
        dc_block_begin(&c, 1, 1u);
        dc_invalidate(&c);
        uint32_t f0 = c.fill;
        for (unsigned l = 0; l < DC_LANES; l++)
            (void)dc_stencil(&c, l, buf, LEN, 2000u + l * 500u, DEEP);
        printf("    8 lanes re-phased onto one frame: %u fills\n", c.fill - f0);
        ck("no more than the token bucket allows can refill in one frame",
           c.fill - f0 <= 1u + DC_FILL_BURST / DC_FILL_COST);
        {   /* and over a whole block */
            unsigned tot = c.fill - f0;
            for (unsigned f = 1; f < BLOCK_FRAMES; f++) {
                dc_block_begin(&c, 1, 1u);
                for (unsigned l = 0; l < DC_LANES; l++)
                    (void)dc_stencil(&c, l, buf, LEN, 2000u + l * 500u + f, DEEP);
            }
            tot = c.fill - f0;
            printf("    over one %u-frame block: %u fills (ceiling %u)\n",
                   (unsigned)BLOCK_FRAMES, tot,
                   (DC_FILL_BURST + BLOCK_FRAMES) / DC_FILL_COST);
            ck("the per-block refill ceiling holds",
               tot <= (DC_FILL_BURST + BLOCK_FRAMES) / DC_FILL_COST);
        }
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
