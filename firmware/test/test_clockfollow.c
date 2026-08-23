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

#define FS 96000.0f

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

        /* exit is immediate when the clock stops */
        cm_init(&cm);
        for (unsigned i = 0; i < CM_ENTER_PAIRS; i++) cm_update(&cm, 1, 1, 0);
        ck("engaged before dropout", cm_swallows_pulse_jacks(&cm));
        on = cm_update(&cm, 0, 0, 1);
        ck("disengages the moment the clock is lost", !on);
        ck("jacks are handed back to transport", !cm_swallows_pulse_jacks(&cm));
    }

    printf(fails ? "\n%d FAILURES\n" : "\nall clock-follow checks passed\n", fails);
    return fails ? 1 : 0;
}
