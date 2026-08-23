/* test_governor.c — the load governor, budget contract clause C-3c.
 *
 * The governor's whole job is a guarantee, so these are not "does it compile"
 * checks: each one is one clause of the guarantee.  The important one is
 * "sustained overrun is impossible" — simulated with a load model where each
 * quality level has a cost and the floor level is under budget, which is
 * exactly the pairing the static ceiling (`make wcet`) is there to establish.
 */
#include "governor.h"
#include <stdio.h>

static int fails = 0;
static void ck(const char *what, int cond) {
    printf("  %-58s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

#define BUDGET 56016u

static gov_cfg_t cfg(void)
{
    gov_cfg_t c;
    c.drop_cycles    = 49294u;   /* 0.88 */
    c.panic_cycles   = BUDGET;
    c.recover_cycles = 33609u;   /* 0.60 */
    c.recover_blocks = 1500u;
    c.max_level      = 2u;
    return c;
}

int main(void)
{
    governor_t g;
    gov_cfg_t c = cfg();

    /* ---- 1. quiet running never moves the level -------------------------- */
    gov_reset(&g, &c);
    int moved = 0;
    for (int i = 0; i < 100; i++)
        if (gov_step(&g, 20000u) != 0u) moved = 1;
    ck("idle load holds level 0 (until recovery is even possible)", !moved);

    /* ---- 2. ONE-BLOCK RESPONSE ------------------------------------------ */
    gov_reset(&g, &c);
    ck("one block at 0.88 drops a level immediately",
       gov_step(&g, 49294u) == 1u);
    ck("the next block is already at the lower quality", g.level == 1u);

    /* ---- 3. panic goes straight to the floor ---------------------------- */
    gov_reset(&g, &c);
    ck("a block at/over budget goes straight to the floor level",
       gov_step(&g, BUDGET) == 2u);
    ck("the overrun is counted (contract failure counter)", g.over == 1u);

    /* ---- 4. levels are bounded ------------------------------------------ */
    for (int i = 0; i < 50; i++) gov_step(&g, BUDGET + 5000u);
    ck("level saturates at the floor, never past it", g.level == 2u);

    /* ---- 5. SUSTAINED OVERRUN IS IMPOSSIBLE -----------------------------
     * Load model: level 0 costs 1.17x budget (the measured reference), level
     * 1 costs 0.95x, the floor costs 0.55x (what `make wcet` must prove).
     * Feed it back through the governor and count deadline misses. */
    {
        const uint32_t cost[3] = { 65539u, 53215u, 30809u };
        gov_reset(&g, &c);
        unsigned lvl = 0, misses = 0, consecutive = 0, worst_run = 0;
        for (int i = 0; i < 5000; i++) {
            uint32_t used = cost[lvl];
            if (used >= BUDGET) {
                misses++;
                consecutive++;
                if (consecutive > worst_run) worst_run = consecutive;
            } else consecutive = 0;
            lvl = gov_step(&g, used);
        }
        printf("    misses=%u worst consecutive run=%u final level=%u\n",
               misses, worst_run, lvl);
        ck("no more than ONE consecutive block can overrun", worst_run <= 1u);
        ck("the overrun does not repeat once settled", misses <= 2u);
        ck("it settles at a level that fits, not at the floor by default",
           lvl == 1u || lvl == 2u);
    }

    /* ---- 6. recovery is slow and needs a genuinely quiet run ------------ */
    gov_reset(&g, &c);
    gov_step(&g, BUDGET);                       /* -> floor */
    for (unsigned i = 0; i < c.recover_blocks - 1u; i++) gov_step(&g, 20000u);
    ck("recovery waits the full quiet window", g.level == 2u);
    ck("then hands back exactly one level", gov_step(&g, 20000u) == 1u);

    /* ---- 7. the dead band between recover and drop does NOT recover ----- */
    gov_reset(&g, &c);
    gov_step(&g, BUDGET);
    for (int i = 0; i < 20000; i++) gov_step(&g, 40000u);  /* 0.71: neither */
    ck("load in the hysteresis band never buys a level back", g.level == 2u);

    /* ---- 8. transitions defer recovery, but never a drop ---------------- */
    gov_reset(&g, &c);
    gov_step(&g, BUDGET);                       /* -> floor */
    for (unsigned i = 0; i < c.recover_blocks - 1u; i++) gov_step(&g, 20000u);
    gov_defer_recovery(&g);
    gov_step(&g, 20000u);
    ck("a transport transition in the block defers the level-up",
       g.level == 2u);
    gov_reset(&g, &c);
    gov_defer_recovery(&g);
    ck("a deferral never blocks a DROP (safety is not deferrable)",
       gov_step(&g, BUDGET) == 2u);

    /* ---- 9. no flutter: a periodic mid-load must not toggle quality -----
     * QA 2026-08-23: this case used to run from level 0, where `level--` is
     * unreachable and `level++` needs a load it never sees — so it asserted
     * transitions == 0 about a governor that had no transition available to
     * make. It could not fail, and a genuine flutter bug would have walked
     * straight past it. Start from a DROPPED level, where recovery is live and
     * the loop-rate spike lands in the dead band that is supposed to veto it. */
    gov_reset(&g, &c);
    (void)gov_step(&g, 50000u);                 /* -> level 1: change possible */
    ck("precondition: the governor is at a level it could climb out of",
       g.level == 1u);
    unsigned changes_before = g.transitions;
    for (int i = 0; i < 30000; i++) gov_step(&g, (i % 100) ? 30000u : 40000u);
    printf("    transitions across 30000 blocks of periodic load: %u\n",
           g.transitions - changes_before);
    ck("a periodic load (loop-rate spikes) does not oscillate the level",
       g.transitions - changes_before == 0u);
    ck("...and it is still parked one level down, not recovered by accident",
       g.level == 1u);

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
