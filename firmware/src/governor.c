/* governor.c — see governor.h. */
#include "governor.h"
#include "bsp/board.h"   /* macro-only header: safe in the host build too */

void gov_reset(governor_t *g, const gov_cfg_t *cfg)
{
    g->cfg = *cfg;
    if (g->cfg.max_level > 15u) g->cfg.max_level = 15u;
    if (g->cfg.drop_streak < 1u) g->cfg.drop_streak = 1u;
    if (g->cfg.backoff_max > 8u) g->cfg.backoff_max = 8u;   /* keep the shift sane */
    g->level = 0u;
    g->defer = 0u;
    g->susp = 0u;
    g->transitions = 0u;
    g->quiet = 0u;
    g->near = 0u;
    /* "ready to change": the rate limit exists to space changes APART, not to
     * make the first one wait — a governor that cannot act for the first two
     * seconds after boot is a governor that is absent for the first two
     * seconds after boot. */
    g->since = g->cfg.hold_blocks;
    g->peak = 0u;
    g->over = 0u;
    g->artefacts = 0u;
    g->backoff = 0u;
}

/* One place that moves the level, so the rate limit and the counter reset
 * cannot be forgotten at one of the call sites. Returns 1 if it moved. */
static int gov_change(governor_t *g, int up)
{
    if (g->since < g->cfg.hold_blocks) return 0;   /* rule 4: rate limit */
    if (up) { if (g->level == 0u) return 0; g->level--;
              if (g->backoff) g->backoff--; }            /* rule 5 */
    else    { if (g->level >= g->cfg.max_level) return 0; g->level++;
              if (g->backoff < g->cfg.backoff_max) g->backoff++; }
    g->transitions++;
    g->since = 0u;
    g->quiet = 0u;
    g->near  = 0u;
    return 1;
}

unsigned gov_step(governor_t *g, uint32_t cycles)
{
    if (g->susp) return g->level;                  /* rule 3: bracketed stall */

    if (cycles >= g->cfg.absurd_cycles) {
        /* rule 3: a block that took several times the entire budget did not
         * spend it on DSP — it was stalled (flash erase, debugger halt, a bus
         * hog). Discard it whole: it is not a measurement of anything the
         * governor can act on, and acting on it costs the player their audio
         * quality until the next power cycle. */
        g->artefacts++;
        return g->level;
    }

    if (cycles > g->peak) g->peak = cycles;
    if (g->since < 0xFFFF0000u) g->since++;

    if (cycles >= g->cfg.panic_cycles) {
        /* This block missed the deadline. Nothing gradual and nothing
         * rate-limited: go to the level whose cost is statically proven, and do
         * it before the next block. A torn output buffer is worse than any
         * quality step, and the crossfade in the engine still covers the swap. */
        g->over++;
        g->quiet = 0u;
        g->near  = 0u;
        if (g->level != g->cfg.max_level) {
            g->level = g->cfg.max_level;
            g->transitions++;
            g->since = 0u;
            if (g->backoff < g->cfg.backoff_max) g->backoff++;
        }
    } else if (cycles >= g->cfg.drop_cycles) {
        /* Near-miss band. One block in here is normal (a loop wrap, a splice
         * arm, an AA republish); a RUN of them is a load that is not going
         * away. Only the run costs a level. */
        g->quiet = 0u;
        if (++g->near >= g->cfg.drop_streak) (void)gov_change(g, /*up*/ 0);
    } else if (cycles < g->cfg.recover_cycles) {
        /* Only a LONG quiet run buys a level back. */
        g->near = 0u;
        /* rule 5: the quiet run needed doubles with every drop taken */
        if (++g->quiet >= (g->cfg.recover_blocks << g->backoff)) {
            g->quiet = 0u;
            if (!g->defer) (void)gov_change(g, /*up*/ 1);
        }
    } else {
        /* between recover and drop: hold station, and do NOT credit the quiet
         * run — recovery must mean "comfortably cheap", not "not yet over". */
        g->quiet = 0u;
        g->near  = 0u;
    }

    g->defer = 0u;      /* the deferral covers exactly one block */
    return g->level;
}

void gov_defer_recovery(governor_t *g) { g->defer = 1u; }

/* ---- the single instance behind the frozen cross-stream API -------------- */
static governor_t g_gov;
/* The engine reads this every block; keep the read a single load and make the
 * ISR's write visible immediately (both run in the same ISR today, but the
 * level is also inspected from the superloop for telemetry). */
static volatile uint8_t g_gov_level;
static volatile uint8_t g_gov_susp;      /* nesting count for gov_suspend()   */

void gov_init(void)
{
    const gov_cfg_t cfg = {
        .drop_cycles    = GOV_DROP_CYCLES,
        .drop_streak    = GOV_DROP_STREAK,
        .panic_cycles   = GOV_PANIC_CYCLES,
        .absurd_cycles  = GOV_ABSURD_CYCLES,
        .recover_cycles = GOV_RECOVER_CYCLES,
        .recover_blocks = GOV_RECOVER_BLOCKS,
        .hold_blocks    = GOV_HOLD_BLOCKS,
        .backoff_max    = GOV_BACKOFF_MAX,
        .max_level      = (uint8_t)GOV_MAX_LEVEL,
    };
    gov_reset(&g_gov, &cfg);
    g_gov_level = 0u;
    g_gov_susp = 0u;
}

unsigned gov_level(void)
{
#if GOV_ENABLE
    return g_gov_level;
#else
    return 0u;
#endif
}

void gov_report(uint32_t cycles)
{
    g_gov_level = (uint8_t)gov_step(&g_gov, cycles);
}

void gov_note_transition(void) { gov_defer_recovery(&g_gov); }

const governor_t *gov_state(void) { return &g_gov; }

void gov_suspend(void)
{
    if (g_gov_susp < 255u) g_gov_susp++;
    g_gov.susp = 1u;
}

void gov_resume(void)
{
    if (g_gov_susp) g_gov_susp--;
    if (g_gov_susp == 0u) {
        g_gov.susp  = 0u;
        /* The stalled blocks told us nothing; start both runs from scratch so a
         * half-finished quiet run cannot hand a level back on the strength of
         * blocks that were never measured. */
        g_gov.quiet = 0u;
        g_gov.near  = 0u;
    }
}
