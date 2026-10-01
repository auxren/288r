/* test_clockfollow.c — external clock follow + the clocked-mode entry gesture.
 *
 * The load-bearing assertion here is that x1 sits ON the printed "1" of the
 * panel legend. The MARF needs a measured raw-ADC constant per board revision
 * for that; we get it from cal_knob_panel_mult()'s legend mapping, so it should
 * hold by construction — and this test is what keeps it true if the zone
 * geometry is ever edited.
 */
#include "clockfollow.h"
#include "clock_mode.h"
#include <stdio.h>
#include <math.h>

static int fails = 0;
static void ck(const char *name, int cond) {
    printf("  %-56s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

#define FS CF_TRUE_FS_HZ   /* the TRUE rate, not SAMPLE_RATE_HZ */

int main(void)
{
    /* ---------- ratio zones live in panel-legend space ---------- */
    ck("x1 at the printed 1.0",            cf_ratio_from_legend(1.00f, 0) == 1);
    ck("x1 holds just below the mark",     cf_ratio_from_legend(0.98f, 0) == 1);
    ck("x1 holds just above the mark",     cf_ratio_from_legend(1.02f, 0) == 1);
    ck("full CCW divides by 8",            cf_ratio_from_legend(0.40f, 0) == -8);
    ck("full CW multiplies by 8",          cf_ratio_from_legend(1.60f, 0) == 8);
    ck("below noon divides",               cf_ratio_from_legend(0.70f, 0) <= -2);
    ck("above noon multiplies",            cf_ratio_from_legend(1.30f, 0) >= 2);

    /* every ratio must be reachable, and the ladder must be monotonic */
    {
        int seen[17] = {0}, monotonic = 1; int8_t prev = -9;
        for (int i = 0; i <= 1200; i++) {
            float m = CF_LEGEND_LO + (CF_LEGEND_HI - CF_LEGEND_LO) * (float)i / 1200.0f;
            int8_t r = cf_ratio_from_legend(m, 0);
            seen[r + 8] = 1;
            if (r < prev) monotonic = 0;      /* CCW->CW must never go backwards */
            prev = r;
        }
        int all = 1;
        for (int r = -8; r <= 8; r++) {
            if (r == 0 || r == -1) continue;  /* not part of the encoding */
            if (!seen[r + 8]) all = 0;
        }
        ck("every ratio /8..x8 is reachable", all);
        ck("ratio ladder is monotonic CCW->CW", monotonic);
    }

    /* ---------- hysteresis: a knob on a boundary must not chatter ---------- */
    {
        /* find a boundary by scanning for a transition */
        float b = 0.0f; int8_t before = cf_ratio_from_legend(CF_LEGEND_LO, 0);
        for (int i = 1; i <= 2000; i++) {
            float m = CF_LEGEND_LO + (CF_LEGEND_HI - CF_LEGEND_LO) * (float)i / 2000.0f;
            int8_t r = cf_ratio_from_legend(m, 0);
            if (r != before) { b = m; break; }
            before = r;
        }
        int8_t held = before;
        int chattered = 0;
        /* jitter across the boundary by less than the hysteresis width */
        for (int i = 0; i < 40; i++) {
            float m = b + ((i & 1) ? 1.0f : -1.0f) * (CF_HYST * 0.4f);
            int8_t r = cf_ratio_from_legend(m, held);
            if (r != held) chattered = 1;
            held = r;
        }
        ck("knob on a zone boundary does not chatter", !chattered);
    }

    /* ---------- the rate guard ---------- */
    {
        clockfollow_t a, b;
        cf_init(&a, CF_TRUE_FS_HZ);
        cf_init(&b, 96000.0f);          /* the mistake this guard exists for */
        ck("being handed SAMPLE_RATE_HZ does not double the windows",
           a.min_period == b.min_period && a.max_period == b.max_period
           && a.timeout == b.timeout);
        ck("qualification window is 20 ms at the true rate",
           a.min_period == (uint32_t)(0.020f * CF_TRUE_FS_HZ));
        ck("dropout is 2 s at the true rate",
           a.timeout == (uint32_t)(2.0f * CF_TRUE_FS_HZ));
    }

    /* ---------- period tracking ---------- */
    {
        clockfollow_t cf; cf_init(&cf, FS);
        const uint32_t P = 48000u;                    /* 500 ms = 120 BPM */
        ck("no lock before any pulse",   !cf.locked);
        ck("too-fast pulse is rejected", cf_pulse(&cf, 100u) == 0);
        ck("too-slow pulse is rejected", cf_pulse(&cf, (uint32_t)(3.0f*FS)) == 0);
        ck("still unlocked after junk",  !cf.locked);

        cf_pulse(&cf, P);
        ck("locks on a qualified pulse", cf.locked && cf.period == P);

        /* jitter should be smoothed, not followed */
        for (int i = 0; i < 8; i++) cf_pulse(&cf, P + ((i & 1) ? 300u : (uint32_t)-300));
        ck("jitter is smoothed toward the mean",
           cf.period > P - 200u && cf.period < P + 200u);

        /* a real tempo change should SNAP, not crawl */
        cf_pulse(&cf, 24000u);
        ck("a genuine tempo change snaps", cf.period == 24000u);

        /* a single spurious edge must not destroy the lock */
        uint32_t held = cf.period;
        cf_pulse(&cf, 5u);
        ck("one spurious edge does not disturb the period", cf.period == held && cf.locked);
    }

    /* ---------- dropout ---------- */
    {
        clockfollow_t cf; cf_init(&cf, FS);
        cf_pulse(&cf, 48000u);
        ck("locked", cf_tick(&cf, 1000u) == 1);
        ck("still locked before the timeout", cf_tick(&cf, (uint32_t)(1.5f*FS)) == 1);
        ck("drops out after the timeout",     cf_tick(&cf, (uint32_t)(1.0f*FS)) == 0);
        ck("period is forgotten, not stale",  cf.period == 0u);
    }

    /* ---------- cycle length from period x ratio ---------- */
    {
        ck("x1  keeps the period",      cf_cycle_samples(48000u,  1) == 48000u);
        ck("x4  lengthens the window",  cf_cycle_samples(48000u,  4) == 192000u);
        ck("/4  shortens the window",   cf_cycle_samples(48000u, -4) == 12000u);
        ck("unlocked yields zero",      cf_cycle_samples(0u,      4) == 0u);
    }

    /* ---------- the entry gesture ---------- */
    {
        clockmode_t cm; cm_init(&cm);
        ck("does not engage on a lone write edge",  !cm_update(&cm, 1, 0, 0));
        ck("does not engage on a lone recirc edge", !cm_update(&cm, 0, 1, 0));

        cm_init(&cm);
        int on = 0;
        for (unsigned i = 0; i < CM_ENTER_PAIRS; i++) on = cm_update(&cm, 1, 1, 0);
        ck("engages after a run of coincident pairs", on);
        ck("swallows the pulse JACKS once engaged", cm_swallows_pulse_jacks(&cm));

        /* quiet ticks between slow clock pulses must not break the run */
        cm_init(&cm);
        cm_update(&cm, 1, 1, 0);
        for (int i = 0; i < 50; i++) cm_update(&cm, 0, 0, 0);
        cm_update(&cm, 1, 1, 0);
        for (int i = 0; i < 50; i++) cm_update(&cm, 0, 0, 0);
        on = cm_update(&cm, 1, 1, 0);
        ck("a slow clock still engages across quiet ticks", on);

        /* a lone edge is ordinary playing and breaks the run */
        cm_init(&cm);
        cm_update(&cm, 1, 1, 0);
        cm_update(&cm, 1, 0, 0);          /* someone tapped write */
        cm_update(&cm, 1, 1, 0);
        on = cm_update(&cm, 1, 1, 0);
        ck("a lone transport press breaks the run", !on);

        /* The jacks must be withheld from the FIRST pair, before engagement:
         * otherwise patching a clock in fires write+recirc as transport and
         * captures a loop before clocked mode takes over (seen on hardware). */
        cm_init(&cm);
        cm_update(&cm, 1, 1, 0);
        ck("jacks withheld from the very first coincident pair",
           cm_swallows_pulse_jacks(&cm));
        ck("but not engaged yet after one pair", !cm.engaged);
        cm_init(&cm);
        cm_update(&cm, 1, 0, 0);
        ck("a lone transport press is NOT swallowed", !cm_swallows_pulse_jacks(&cm));

        /* ---- UNPLUGGING ONE JACK ---- the case the first design got wrong.
         * The clock keeps running into the surviving jack, so no pairs arrive.
         * Without unpaired-run detection the module sits engaged for the whole
         * dropout timeout swallowing that jack, then drops out and lets the
         * pulse train retrigger the transport several times a second. */
        cm_init(&cm);
        for (unsigned i = 0; i < CM_ENTER_PAIRS; i++) cm_update(&cm, 1, 1, 0);
        ck("engaged with both cables in", cm_swallows_pulse_jacks(&cm));
        {
            int still = 1;
            for (unsigned i = 0; i < CM_EXIT_SINGLES; i++)
                still = cm_update(&cm, 0, 1, 0);   /* write unplugged, recirc clocks on */
            ck("unplugging ONE jack exits within a few clocks", !still);
            ck("surviving jack is handed back to transport", !cm_swallows_pulse_jacks(&cm));
        }

        /* a single split pair (edges either side of a tick boundary) is jitter,
         * not an unplug, and must NOT drop the mode */
        cm_init(&cm);
        for (unsigned i = 0; i < CM_ENTER_PAIRS; i++) cm_update(&cm, 1, 1, 0);
        cm_update(&cm, 1, 0, 0);              /* pair split across the boundary */
        cm_update(&cm, 0, 1, 0);
        int held_on = cm_update(&cm, 1, 1, 0);
        ck("a split pair does not drop the mode", held_on);

        /* and the run must RESET on a good pair, so intermittent splits over a
         * long session never accumulate into a spurious exit */
        cm_init(&cm);
        for (unsigned i = 0; i < CM_ENTER_PAIRS; i++) cm_update(&cm, 1, 1, 0);
        for (int i = 0; i < 30; i++) {
            cm_update(&cm, 1, 0, 0);          /* one split ... */
            cm_update(&cm, 1, 1, 0);          /* ... then a good pair */
        }
        ck("occasional splits never accumulate into an exit", cm_swallows_pulse_jacks(&cm));

        /* exit is immediate when the clock stops */
        cm_init(&cm);
        for (unsigned i = 0; i < CM_ENTER_PAIRS; i++) cm_update(&cm, 1, 1, 0);
        ck("engaged before dropout", cm_swallows_pulse_jacks(&cm));
        on = cm_update(&cm, 0, 0, 1);
        ck("disengages the moment the clock is lost", !on);
        ck("jacks are handed back to transport", !cm_swallows_pulse_jacks(&cm));
    }

    /* ---------- edge PAIRING with skew (bench 2026-09-30) ----------
     * One clock split to two jacks arrives on two comparators: the rises land
     * in different 0.33 ms blocks. The ISR used to require both in the SAME
     * block, so most pulses were dropped; the follower saw 2x/4x intervals and
     * accepted them, and the lock dropped out to the 2 s timeout every ~3 s
     * (SWD trace: base 24000 <-> 41440 flipping). A pair is now "the other
     * jack rose within CM_PAIR_WINDOW_BLOCKS", stamped at the EARLIER edge. */
    {
        cm_pair_t pr; cm_pair_init(&pr);
        uint32_t st = 0; unsigned ev;
        ev = cm_pair_block(&pr, 0x3u, 100u, &st);
        ck("same-block rise on both jacks is a pair",       (ev & CM_EV_PAIR) && st == 100u);
        ev = cm_pair_block(&pr, 0x0u, 101u, &st);
        ck("...and is counted exactly once",                 ev == 0u);

        cm_pair_init(&pr);
        ev  = cm_pair_block(&pr, 0x1u, 200u, &st);          /* write first */
        ck("a lone first edge is not yet anything",         ev == 0u);
        ev  = cm_pair_block(&pr, 0x2u, 203u, &st);          /* recirc 1 ms later */
        ck("skewed rise within the window pairs",           (ev & CM_EV_PAIR) != 0u);
        ck("the pair is stamped at the EARLIER edge",       st == 200u);

        cm_pair_init(&pr);
        ev  = cm_pair_block(&pr, 0x2u, 300u, &st);          /* recirc first */
        ev |= cm_pair_block(&pr, 0x1u, 300u + CM_PAIR_WINDOW_BLOCKS, &st);
        ck("skew up to the full window still pairs",        (ev & CM_EV_PAIR) && st == 300u);

        cm_pair_init(&pr);
        ev = cm_pair_block(&pr, 0x1u, 400u, &st);
        for (uint32_t b = 401u; b <= 400u + CM_PAIR_WINDOW_BLOCKS; b++) ev |= cm_pair_block(&pr, 0u, b, &st);
        ck("inside the window a lone edge is still pending", ev == 0u);
        ev = cm_pair_block(&pr, 0u, 401u + CM_PAIR_WINDOW_BLOCKS, &st);
        ck("an edge whose partner never comes is a SINGLE",  (ev & CM_EV_SINGLE_W) != 0u);
        ev = cm_pair_block(&pr, 0x2u, 600u, &st);
        ev |= cm_pair_block(&pr, 0u, 601u + CM_PAIR_WINDOW_BLOCKS, &st);
        ck("a late partner is a new single, not a pair",     (ev & CM_EV_SINGLE_R) && !(ev & CM_EV_PAIR));

        /* a steady skewed clock: exactly one pair per period, stamps on the grid */
        cm_pair_init(&pr);
        unsigned pairs = 0, singles = 0; int grid_ok = 1;
        for (uint32_t b = 1000u; b < 1000u + 40u * 60u; b++) {
            unsigned r = 0;
            if ((b - 1000u) % 60u == 0u) r |= 0x1u;            /* write on the grid   */
            if ((b - 1000u) % 60u == 4u) r |= 0x2u;            /* recirc 4 blocks late */
            ev = cm_pair_block(&pr, r, b, &st);
            if (ev & CM_EV_PAIR) { pairs++; if ((st - 1000u) % 60u != 0u) grid_ok = 0; }
            if (ev & (CM_EV_SINGLE_W | CM_EV_SINGLE_R)) singles++;
        }
        ck("steady skewed clock: one pair per period",       pairs == 40u);
        ck("steady skewed clock: no singles",                singles == 0u);
        ck("steady skewed clock: stamps sit on the clock grid", grid_ok);
    }

    printf(fails ? "\n%d FAILURES\n" : "\nall clock-follow checks passed\n", fails);
    return fails ? 1 : 0;
}
