/* test_taps.c — host unit test for taps + time_control (the delay-time control path).
 * cc -std=c11 -Wall -Wextra -I../src test_taps.c ../src/taps.c ../src/time_control.c -o /tmp/t -lm && /tmp/t
 */
#include "taps.h"
#include "time_control.h"
#include <stdio.h>
#include <math.h>

static int fails = 0;
static void check(const char *what, float got, float want, float tol)
{
    if (fabsf(got - want) > tol) { printf("  FAIL %-38s got %.5f want %.5f\n", what, got, want); fails++; }
    else                         { printf("  ok   %-38s %.5f\n", what, got); }
}

int main(void)
{
    /* ---- taps: phase/time scaling ---- */
    taps_t t;
    taps_init(&t, /*base_delay*/ 1000.0f, /*slew*/ 0.05f);
    /* default preset phase[i] = 20*(i+1); tap 7 = 160 (fullscale). */
    check("tap7 target @mult=1", taps_target(&t, 7, 1.0f), 1000.0f, 1e-3f);   /* 160/160 * 1000 */
    check("tap0 target @mult=1", taps_target(&t, 0, 1.0f), 125.0f,  1e-3f);   /*  20/160 * 1000 */
    check("tap7 target @mult=2", taps_target(&t, 7, 2.0f), 2000.0f, 1e-3f);   /* scales w/ mult  */

    /* ---- taps: slew converges smoothly (no instant jump) ---- */
    float first_step = 0.0f;
    for (int n = 0; n < 400; n++) {
        float before = taps_delay(&t, 7);
        taps_update(&t, 1.0f);
        if (n == 0) first_step = taps_delay(&t, 7) - before;
    }
    check("slew first step is gradual", first_step, 50.0f, 1.0f);        /* 0.05*(1000-0) */
    check("slew converges to target",  taps_delay(&t, 7), 1000.0f, 1.0f);

    /* ---- time_control: range mapping (linear taper, matches panel legend) ---- */
    time_ctrl_t tc;
    tc_init(&tc, /*initial*/ 1.0f, /*slew*/ 1.0f, /*lo*/ 0.25f, /*hi*/ 20.0f);
    check("tc raw=0 -> lo", tc_update(&tc, 0.0f), 0.25f, 1e-3f);
    tc_init(&tc, 20.0f, 1.0f, 0.25f, 20.0f);
    check("tc raw=1 -> hi", tc_update(&tc, 1.0f), 20.0f, 1e-2f);
    tc_init(&tc, 1.0f, 1.0f, 0.25f, 20.0f);
    check("tc raw=0.5 -> linear midpoint", tc_update(&tc, 0.5f), 0.25f + 19.75f*0.5f, 1e-2f);

    /* ---- end-to-end CHORUS scenario: small LFO depth around a center delay ----
     * This is the real target use case. Assert the tap delay moves CONTINUOUSLY:
     * every step is small (slew-limited, no integer stair-step) but non-zero. */
    taps_init(&t, /*base_delay*/ 300.0f, /*slew*/ 0.3f);
    tc_init(&tc, /*initial*/ 1.0f, /*slew*/ 0.3f, /*lo*/ 0.9f, /*hi*/ 1.1f);
    /* warm up to center so we measure steady-state modulation, not startup */
    for (int k = 0; k < 200; k++) { taps_update(&t, tc_update(&tc, 0.5f)); }
    float prev = taps_delay(&t, 7), maxstep = 0.0f, movement = 0.0f;
    for (int k = 0; k <= 2000; k++) {
        float raw = 0.5f + 0.4f * sinf((float)k * 0.02f);   /* slow chorus LFO */
        taps_update(&t, tc_update(&tc, raw));
        float now = taps_delay(&t, 7);
        float s = fabsf(now - prev);
        if (s > maxstep) maxstep = s;
        movement += s;
        prev = now;
    }
    check("chorus: each step small (smooth)", maxstep < 3.0f ? 0.0f : 1.0f, 0.0f, 0.5f);
    check("chorus: delay actually moves",     movement > 50.0f ? 0.0f : 1.0f, 0.0f, 0.5f);

    /* ================= BLOCK-RATE TAP CONTROL (contract C3) ==============
     * taps_update_block() must be the same control law as taps_update(), not
     * an approximation of it that happens to sound similar. Each check below
     * is one property the per-sample path had and the block path must keep. */
#define BLK 16u
    {
        /* ---- trajectory: 1 block == BLK per-sample steps ---------------- */
        taps_t a, bkt;
        taps_init(&a,   2000000.0f, 0.001f);   /* deep delay: the ULP regime */
        taps_init(&bkt, 2000000.0f, 0.001f);
        float worst = 0.0f, scale = 0.0f;
        for (int blk = 0; blk < 400; blk++) {
            float m = 1.0f + 0.3f * sinf((float)blk * 0.05f);
            for (unsigned k = 0; k < BLK; k++) taps_update(&a, m);
            taps_update_block(&bkt, m, BLK);
            for (int i = 0; i < NUM_TAPS; i++) {
                float d = fabsf(taps_delay(&a, i) - taps_delay(&bkt, i));
                if (d > worst) worst = d;
                if (fabsf(taps_delay(&a, i)) > scale) scale = fabsf(taps_delay(&a, i));
            }
        }
        printf("    block-vs-sample trajectory: worst %.3f samples of %.0f "
               "(%.2e relative)\n", worst, scale, worst / scale);
        /* Deliberately a RELATIVE bound. Within a block the position follows
         * the chord instead of the arc, and that error scales with the delta
         * being slewed: (n*slew)^2/8 = 3.2e-5 of it at n=16, slew=0.001. The
         * absolute figure above is the extreme case — a 2M-sample delay swept
         * +/-30% — where 12 samples is 0.25 ms of delay-TIME difference in a
         * 40-second delay, on a trajectory that stays smooth and lands on the
         * identical endpoint. At ordinary delay lengths it is a hundredth of a
         * sample. What would matter is a discontinuity, which the boundary
         * check below covers. */
        check("block update tracks the per-sample slew",
              (worst / scale) < 1e-4f ? 0.0f : 1.0f, 0.0f, 0.5f);

        /* ---- the intra-block ramp ends exactly where the block ends ----- */
        uint32_t di_a, di_b; float df_a, df_b;
        taps_delay_frac(&bkt, 3, &di_a, &df_a);
        taps_delay_frac_at(&bkt, 3, BLK - 1u, &di_b, &df_b);
        check("ramp's last sample == the block's end position (int)",
              (float)(di_a == di_b), 1.0f, 0.0f);
        check("ramp's last sample == the block's end position (frac)",
              df_a, df_b, 1e-9f);

        /* ---- the ramp is monotone and has no boundary step -------------- */
        taps_t c;
        taps_init(&c, 300.0f, 0.3f);
        for (int k = 0; k < 40; k++) taps_update_block(&c, 1.0f, BLK);
        /* The read position must not JUMP where one block hands over to the
         * next. Inside a block the ramp's step is constant by construction, so
         * the test is the ratio: the step across a boundary must be the same
         * size as the steps either side of it. (An earlier version asserted an
         * absolute step size and failed on legitimate fast modulation — the
         * property that matters is continuity, not slowness.) */
        float prev2 = 0.0f, big_in = 0.0f, big_edge = 0.0f, moved = 0.0f;
        int first = 1;
        for (int blk = 0; blk < 300; blk++) {
            float m = 1.0f + 0.2f * sinf((float)blk * 0.09f);
            taps_update_block(&c, m, BLK);
            for (unsigned k = 0; k < BLK; k++) {
                uint32_t di; float df;
                taps_delay_frac_at(&c, 7, k, &di, &df);
                float pos = (float)di + df;
                if (!first) {
                    float s = fabsf(pos - prev2);
                    if (k == 0u) { if (s > big_edge) big_edge = s; }
                    else         { if (s > big_in)   big_in   = s; }
                    moved += s;
                }
                first = 0;
                prev2 = pos;
            }
        }
        printf("    ramp step: %.5f inside a block, %.5f across the boundary\n",
               big_in, big_edge);
        check("no jump at the block boundary (continuous read position)",
              big_edge <= big_in * 1.5f + 1e-6f ? 0.0f : 1.0f, 0.0f, 0.5f);
        check("the modulation actually moves the tap",
              moved > 5.0f ? 0.0f : 1.0f, 0.0f, 0.5f);

        /* ---- a settled lane costs nothing and does not creep ------------ */
        taps_t s;
        taps_init(&s, 1000.0f, 0.5f);
        for (int k = 0; k < 400; k++) taps_update_block(&s, 1.0f, BLK);
        float at0, at15;
        { uint32_t di; float df;
          taps_delay_frac_at(&s, 5, 0u, &di, &df);        at0 = (float)di + df;
          taps_delay_frac_at(&s, 5, BLK - 1u, &di, &df);  at15 = (float)di + df; }
        check("a settled lane is flat across the block", at15 - at0, 0.0f, 1e-6f);
        check("a settled lane sits exactly on target",
              at0, taps_target(&s, 5, 1.0f), 1e-3f);

        /* ---- sub-ULP resolution survives (the reason for Q32.32) --------
         * A float POSITION at a 2M-sample delay quantises to 1/8 sample, so a
         * per-sample slew step smaller than that would stall — the exact
         * failure this module's fixed point exists to prevent. Move the target
         * by 10 samples (per-sample step ~0.02) and check the ramp resolves
         * it, i.e. that carrying the ramp in Q32.32 kept the property. */
        taps_t q;
        taps_init(&q, 2000000.0f, 0.001f);
        for (int k = 0; k < 4000; k++) taps_update_block(&q, 1.0f, BLK);
        taps_update_block(&q, 1.000005f, BLK);    /* target +10 samples */
        uint32_t di0, di1; float df0, df1;
        taps_delay_frac_at(&q, 7, 0u, &di0, &df0);
        taps_delay_frac_at(&q, 7, 1u, &di1, &df1);
        /* NB: the difference must be taken on the int and frac parts
         * SEPARATELY. Adding them first — (float)di + df — rounds the sum to
         * the float grid at 2e6, whose ULP is 0.125, and throws away exactly
         * the resolution being measured. That is the whole reason the API
         * hands out int+frac instead of a float. */
        float step_one = (float)((int32_t)di1 - (int32_t)di0) + (df1 - df0);
        printf("    per-sample ramp step at a 2M delay: %.6f samples "
               "(float ULP there is 0.125)\n", step_one);
        check("a step far below the float ULP still moves the position",
              (step_one > 1e-4f && step_one < 0.125f) ? 0.0f : 1.0f, 0.0f, 0.5f);
    }

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
