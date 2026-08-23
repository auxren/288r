/* governor.c — see governor.h. */
#include "governor.h"
#include "bsp/board.h"   /* macro-only header: safe in the host build too */

void gov_reset(governor_t *g, const gov_cfg_t *cfg)
{
    g->cfg = *cfg;
    if (g->cfg.max_level > 15u) g->cfg.max_level = 15u;
    g->level = 0u;
    g->defer = 0u;
    g->transitions = 0u;
    g->quiet = 0u;
    g->peak = 0u;
    g->over = 0u;
}

unsigned gov_step(governor_t *g, uint32_t cycles)
{
    if (cycles > g->peak) g->peak = cycles;

    if (cycles >= g->cfg.panic_cycles) {
        /* This block missed the deadline. Nothing gradual: go to the level
         * whose cost is statically proven, and do it before the next block. */
        g->over++;
        g->quiet = 0u;
        if (g->level != g->cfg.max_level) {
            g->level = g->cfg.max_level;
            g->transitions++;
        }
    } else if (cycles >= g->cfg.drop_cycles) {
        g->quiet = 0u;
        if (g->level < g->cfg.max_level) {
            g->level++;
            g->transitions++;
        }
    } else if (cycles < g->cfg.recover_cycles) {
        /* Only a LONG quiet run buys a level back — see the header on why
         * fast recovery flutters at the loop rate. */
        if (++g->quiet >= g->cfg.recover_blocks) {
            g->quiet = 0u;
            if (g->level > 0u && !g->defer) {
                g->level--;
                g->transitions++;
            }
        }
    } else {
        /* between recover and drop: hold station, and do NOT credit the quiet
         * run — recovery must mean "comfortably cheap", not "not yet over". */
        g->quiet = 0u;
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

void gov_init(void)
{
    const gov_cfg_t cfg = {
        .drop_cycles    = GOV_DROP_CYCLES,
        .panic_cycles   = GOV_PANIC_CYCLES,
        .recover_cycles = GOV_RECOVER_CYCLES,
        .recover_blocks = GOV_RECOVER_BLOCKS,
        .max_level      = (uint8_t)GOV_MAX_LEVEL,
    };
    gov_reset(&g_gov, &cfg);
    g_gov_level = 0u;
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
