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

    /* ---- time_control: range mapping (cubic taper placeholder) ---- */
    time_ctrl_t tc;
    tc_init(&tc, /*initial*/ 1.0f, /*slew*/ 1.0f, /*lo*/ 0.25f, /*hi*/ 20.0f);
    check("tc raw=0 -> lo", tc_update(&tc, 0.0f), 0.25f, 1e-3f);
    tc_init(&tc, 20.0f, 1.0f, 0.25f, 20.0f);
    check("tc raw=1 -> hi", tc_update(&tc, 1.0f), 20.0f, 1e-2f);
    tc_init(&tc, 1.0f, 1.0f, 0.25f, 20.0f);
    check("tc raw=0.5 -> cubic", tc_update(&tc, 0.5f), 0.25f + 19.75f*0.125f, 1e-2f);

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

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
