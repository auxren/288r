/* test_governor.c — the load governor, budget contract clause C-3c.
 *
 * The governor's whole job is a guarantee, so these are not "does it compile"
 * checks: each one is one clause of the guarantee. Two of them are the clauses
 * an adversarial review broke the previous design on, and they are first,
 * because they are the ones that decide whether the instrument sounds like
 * itself:
 *
 *   * the loads the HEALTHY unit actually measures must cost nothing. The old
 *     thresholds dropped a level at 0.88 of budget, against a measured healthy
 *     steady state of 0.88-0.90 and a worst of 0.94 — so the shipped firmware
 *     would have latched to its floor on the first audio block and never come
 *     back (recovery wanted 0.60, and TIME mode has never measured below 0.76).
 *   * a periodic spike must not turn into a metronome of quality changes. The
 *     old design argued that a slow recovery prevented that; it measured 41
 *     level changes in 20 s of a once-per-second spike.
 *
 * The config below is the SHIPPED one (bsp/board.h) rather than a hand-picked
 * set, so these cases fail if the constants drift back into the operating
 * range.
 */
#include "governor.h"
#include "bsp/board.h"
#include <stdio.h>

static int fails = 0;
static void ck(const char *what, int cond) {
    printf("  %-58s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

#define BUDGET ISR_BUDGET_CYCLES
static uint32_t pct(double f) { return (uint32_t)(f * (double)BUDGET); }

static gov_cfg_t cfg(void)
{
    gov_cfg_t c;
    c.drop_cycles    = GOV_DROP_CYCLES;
    c.drop_streak    = GOV_DROP_STREAK;
    c.panic_cycles   = GOV_PANIC_CYCLES;
    c.absurd_cycles  = GOV_ABSURD_CYCLES;
    c.recover_cycles = GOV_RECOVER_CYCLES;
    c.recover_blocks = GOV_RECOVER_BLOCKS;
    c.hold_blocks    = GOV_HOLD_BLOCKS;
    c.backoff_max    = GOV_BACKOFF_MAX;
    c.max_level      = GOV_MAX_LEVEL;
    return c;
}

int main(void)
{
    governor_t g;
    gov_cfg_t c = cfg();

    printf("  shipped thresholds: drop %.2f (x%u), panic %.2f, recover %.2f,"
           " absurd %.2f\n",
           (double)c.drop_cycles / BUDGET, c.drop_streak,
           (double)c.panic_cycles / BUDGET, (double)c.recover_cycles / BUDGET,
           (double)c.absurd_cycles / BUDGET);

    /* ---- 1. THE MEASURED HEALTHY IMAGE COSTS NOTHING --------------------
     * Every one of these is a real reading from the unit. If any of them moves
     * the level, the governor is not a safety net, it is a permanent quality
     * cut that ships silently. */
    {
        const struct { const char *what; double load; unsigned blocks; } run[] = {
            { "v1.3.0 steady TIME (0.89)",        0.89, 20000u },
            { "idle TIME-recirc (0.93)",          0.93, 20000u },
            { "worst healthy: AA engage (0.94)",  0.94,  2000u },
            { "best-ever TIME mode (0.76)",       0.76, 20000u },
        };
        for (unsigned i = 0; i < sizeof run / sizeof run[0]; i++) {
            gov_reset(&g, &c);
            for (unsigned b = 0; b < run[i].blocks; b++) gov_step(&g, pct(run[i].load));
            char nm[80];
            snprintf(nm, sizeof nm, "%s never costs a level", run[i].what);
            ck(nm, g.level == 0u && g.transitions == 0u);
        }
    }

    /* ---- 2. quiet running never moves the level -------------------------- */
    gov_reset(&g, &c);
    int moved = 0;
    for (int i = 0; i < 100; i++)
        if (gov_step(&g, 20000u) != 0u) moved = 1;
    ck("idle load holds level 0 (until recovery is even possible)", !moved);

    /* ---- 3. a SPIKE is not a load; a RUN of them is --------------------- */
    gov_reset(&g, &c);
    for (unsigned i = 0; i < c.drop_streak - 1u; i++) gov_step(&g, c.drop_cycles);
    ck("a burst shorter than the streak costs nothing (wrap/splice spikes)",
       g.level == 0u);
    gov_step(&g, 20000u);                                  /* one cheap block */
    for (unsigned i = 0; i < c.drop_streak - 1u; i++) gov_step(&g, c.drop_cycles);
    ck("the streak has to be CONSECUTIVE", g.level == 0u);
    for (unsigned i = 0; i < c.drop_streak; i++) gov_step(&g, c.drop_cycles);
    ck("a sustained near-miss run does cost a level", g.level == 1u);

    /* ---- 4. panic goes straight to the floor ---------------------------- */
    gov_reset(&g, &c);
    ck("a block at/over budget goes straight to the floor level",
       gov_step(&g, BUDGET) == 2u);
    ck("the overrun is counted (contract failure counter)", g.over == 1u);

    /* ---- 5. levels are bounded ------------------------------------------ */
    for (int i = 0; i < 50; i++) gov_step(&g, BUDGET + 5000u);
    ck("level saturates at the floor, never past it", g.level == 2u);

    /* ---- 6. A STALL IS NOT A LOAD --------------------------------------
     * bsp/flash_preset.c erases a 128 KB sector to save a preset; the ISR keeps
     * running and its DWT delta for that block is in the tens of millions. Two
     * independent defences, and both are checked: the absurd-value filter, and
     * the explicit suspend bracket. */
    gov_reset(&g, &c);
    ck("a 0.5 s flash-erase block is discarded, not acted on",
       gov_step(&g, 84000000u) == 0u && g.artefacts == 1u && g.transitions == 0u);
    ck("...and the peak telemetry is not poisoned by it", g.peak == 0u);
    /* gov_suspend()/gov_resume() act on the SINGLETON (that is the API the BSP
     * calls), so this half is checked through gov_init/gov_report/gov_level. */
    gov_init();
    gov_suspend();
    gov_report(BUDGET + 100u);
    ck("suspended: even a plausible overrun is ignored", gov_level() == 0u);
    gov_resume();
    gov_report(BUDGET);
    ck("resumed: the governor works again", gov_level() == GOV_MAX_LEVEL);
    ck("suspend/resume nests", (gov_suspend(), gov_suspend(), gov_resume(),
                                gov_state()->susp == 1u));
    gov_resume();

    /* ---- 7. SUSTAINED OVERRUN IS IMPOSSIBLE -----------------------------
     * Load model: level 0 costs 1.17x budget (the measured reference), level 1
     * costs 0.95x, the floor costs 0.55x (what `make wcet` must prove). Feed it
     * back through the governor and count deadline misses. */
    {
        const uint32_t cost[3] = { pct(1.17), pct(0.95), pct(0.55) };
        gov_reset(&g, &c);
        unsigned lvl = 0, misses = 0, consecutive = 0, worst_run = 0;
        for (int i = 0; i < 200000; i++) {
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

    /* ---- 8. recovery is slow, but it is REACHABLE ----------------------
     * The threshold has to sit above the mode's normal operating load or the
     * floor is permanent. 0.89 is the healthy steady state; it must recover. */
    gov_reset(&g, &c);
    gov_step(&g, BUDGET);                       /* -> floor */
    for (unsigned i = 0; i < c.hold_blocks + 4u * c.recover_blocks; i++)
        gov_step(&g, pct(0.89));
    ck("the measured healthy load DOES buy levels back", g.level < 2u);
    {
        unsigned guard = 0;
        while (g.level > 0u && guard++ < 400000u) gov_step(&g, pct(0.89));
        ck("...all the way to full quality", g.level == 0u);
    }

    /* ---- 9. the dead band between recover and drop does NOT recover ----- */
    gov_reset(&g, &c);
    gov_step(&g, BUDGET);
    for (int i = 0; i < 200000; i++) gov_step(&g, pct(0.95));
    ck("load in the hysteresis band never buys a level back", g.level == 2u);

    /* ---- 10. transitions defer recovery, but never a drop --------------- */
    {   /* The deferral is a property of gov_step, so it is tested on its own
         * config: the shipped rate limit and backoff would otherwise decide the
         * outcome and the case would prove nothing about deferral. Both halves
         * are checked — deferred does NOT recover, undeferred DOES — because
         * only the pair is non-vacuous. */
        gov_cfg_t d = c;
        d.hold_blocks = 0u; d.backoff_max = 0u;
        gov_reset(&g, &d);
        gov_step(&g, BUDGET);                          /* -> floor */
        for (unsigned i = 0; i < d.recover_blocks - 1u; i++) gov_step(&g, 20000u);
        gov_defer_recovery(&g);
        gov_step(&g, 20000u);
        ck("a transport transition in the block defers the level-up",
           g.level == 2u);
        gov_reset(&g, &d);
        gov_step(&g, BUDGET);
        for (unsigned i = 0; i < d.recover_blocks - 1u; i++) gov_step(&g, 20000u);
        ck("...and without the deferral that same block recovers",
           gov_step(&g, 20000u) == 1u);
    }

    gov_reset(&g, &c);
    gov_defer_recovery(&g);
    ck("a deferral never blocks a DROP (safety is not deferrable)",
       gov_step(&g, BUDGET) == 2u);

    /* ---- 11. NO FLUTTER -------------------------------------------------
     * The adversarial case, verbatim: a once-per-second load spike (30 blocks
     * hot, 2970 blocks quiet) — the shape of a loop wrap on an otherwise
     * healthy image — run for 20 s. The old design produced 41 level changes
     * and ~2 un-crossfaded interpolator swaps a second, indefinitely. Two
     * things kill it here: the spike has to reach the near-miss band at all
     * (0.90 does not), and if it does, the recovery window doubles per drop so
     * the oscillation converges instead of repeating. */
    {
        struct { const char *what; double hot; } sp[] = {
            { "0.90 spike (below the band)", 0.90 },
            { "0.98 spike (inside the band)", 0.98 },
            { "1.02 spike (over the deadline)", 1.02 },
        };
        for (unsigned k = 0; k < 3u; k++) {
            gov_reset(&g, &c);
            for (int i = 0; i < 60000; i++)          /* 20 s at ~3000 blocks/s */
                gov_step(&g, (i % 3000 < 30) ? pct(sp[k].hot) : pct(0.55));
            printf("    %-32s -> %u level changes in 20 s, final level %u\n",
                   sp[k].what, g.transitions, g.level);
            char nm[80];
            snprintf(nm, sizeof nm, "%s: no quality flutter", sp[k].what);
            ck(nm, g.transitions <= 4u);
        }
    }

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
