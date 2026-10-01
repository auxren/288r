/* test_capture_budget.c — the loop-capture one-shot (bench session 9).
 *
 * Every loop capture cost one over-budget audio block on the unit (gov_over++
 * on each, in every patch state). Host accounting of SDRAM words per 16-frame
 * block around a capture showed why: steady blocks move ~200 words (the
 * dl_cache serves nearly every tap read), but for the whole seam-splice window
 * (~7 blocks) the cache was switched OFF — engine.c's per-frame rule treated
 * "the splice job is writing somewhere" as "no cached line can be trusted" —
 * so every block paid 512 words of direct tap reads on top of the 256-word
 * splice RMW: 768 words, 3.8x steady, 103-106% of budget on the unit.
 *
 * The splice writes `spl_quota` consecutive samples per frame at the seam
 * tail; at most ONE lane's 96-word line can overlap that. This suite pins the
 * fix: during the splice window the tap reads keep being served from the
 * cache (no bypass), and only the overlapping lane is dropped. Correctness
 * (no stale word ever served) is the separate `make cachecheck` gate: cache
 * on vs off must stay bit-identical through the golden scenario's captures.
 */
#include "engine.h"
#include "dl_cache.h"
#include <stdio.h>
#include <math.h>

static int fails = 0;
static void ck(const char *name, int cond) {
    printf("  %-56s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

#define LEN 200000u
#define BF  16u
static float buf[LEN];

int main(void)
{
    printf("test_capture_budget\n");
    engine_t e; float chan[NUM_TAPS];
    engine_init(&e, buf, LEN, 24000.0f, 0.4f, 1.6f, 0.01f);
    e.interp = DL_INTERP_HERMITE; e.varispeed = 1;
    for (unsigned i = 0; i < 120000; i++)
        engine_process_multi(&e, (float)sin(i * 0.017) * 0.5f, 0.62f, chan);

    double steady = 0.0; unsigned nsteady = 0;
    double worst_splice = 0.0; unsigned splice_blocks = 0, bypass_frames = 0;
    unsigned worst_direct_miss = 0;
    for (unsigned blk = 0; blk < 60; blk++) {
        uint32_t f0 = e.dc.fill, m0 = e.dc.miss;
        unsigned spl_frames = 0;
        if (blk == 20) engine_recirc_window(&e, 23728u);
        for (unsigned f = 0; f < BF; f++) {
            if (e.spl_active) spl_frames++;
            if (!e.dc.on) bypass_frames++;
            engine_process_multi(&e, (float)sin((blk * BF + f) * 0.017) * 0.5f, 0.62f, chan);
        }
        unsigned fills = e.dc.fill - f0, miss = e.dc.miss - m0;
        double words = fills * (double)DC_W + 4.0 * miss + 2.0 * e.spl_quota * spl_frames;
        if (blk < 18) { steady += words; nsteady++; }
        if (spl_frames) {
            splice_blocks++;
            if (words > worst_splice) worst_splice = words;
            if (miss > worst_direct_miss) worst_direct_miss = miss;
        }
    }
    steady /= (double)nsteady;
    printf("      steady %.0f words/block, worst splice block %.0f words (%u splice blocks),"
           " worst direct misses in a splice block %u, cache-off frames %u\n",
           steady, worst_splice, splice_blocks, worst_direct_miss, bypass_frames);
    ck("capture happened: splice window spans several blocks", splice_blocks >= 4);
    ck("cache never switched off during the splice", bypass_frames == 0);
    /* the only legitimate extra traffic in a splice block: the splice's own
     * RMW (2 words x quota x 16 frames) plus the refill-rate cap (2 lines) */
    double allowed = steady + 2.0 * e.spl_quota * BF + 2.0 * DC_W + 64.0;
    printf("      allowed per splice block = %.0f words\n", allowed);
    ck("splice block = steady + splice RMW + <= 2 refills", worst_splice <= allowed);
    ck("direct tap reads in a splice block stay rare (< 32)", worst_direct_miss < 32);
    printf(fails ? "FAILURES: %d\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
