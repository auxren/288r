/* test_delay_line.c — host unit test for the fractional delay line.
 * Build+run:  cc -std=c11 -Wall -Wextra -I../src test_delay_line.c ../src/delay_line.c -o /tmp/t && /tmp/t
 */
#include "delay_line.h"
#include <stdio.h>
#include <math.h>

static int fails = 0;
static void check(const char *what, float got, float want, float tol)
{
    if (fabsf(got - want) > tol) {
        printf("  FAIL %-34s got %.6f want %.6f\n", what, got, want);
        fails++;
    } else {
        printf("  ok   %-34s %.6f\n", what, got);
    }
}

int main(void)
{
    float mem[64];
    delay_line_t d;
    dl_init(&d, mem, 64);
    dl_clear(&d);

    /* Write a known sequence: sample value == its write index (0,1,2,...). */
    for (int i = 0; i < 40; i++) dl_write(&d, (float)i);
    /* wpos now = 40; buf[k] = k for k<40. Reading `delay` back gives value 40-delay. */

    /* Integer delays: exact. */
    check("linear delay=1",  dl_read(&d, 1.0f, DL_INTERP_LINEAR), 39.0f, 1e-4f);
    check("linear delay=10", dl_read(&d, 10.0f, DL_INTERP_LINEAR), 30.0f, 1e-4f);

    /* Fractional delay on a linear ramp: linear interp is EXACT on a ramp. */
    check("linear delay=5.5",  dl_read(&d, 5.5f,  DL_INTERP_LINEAR), 34.5f, 1e-4f);
    check("linear delay=5.25", dl_read(&d, 5.25f, DL_INTERP_LINEAR), 34.75f, 1e-4f);

    /* Hermite also reproduces a linear ramp exactly (interpolates through points). */
    check("hermite delay=5.5",  dl_read(&d, 5.5f,  DL_INTERP_HERMITE), 34.5f, 1e-4f);
    check("hermite delay=10",   dl_read(&d, 10.0f, DL_INTERP_HERMITE), 30.0f, 1e-4f);

    /* Smoothness: sweeping the delay must change the output continuously
     * (no integer stair-step). Max step over a fine sweep should be small. */
    float prev = dl_read(&d, 2.0f, DL_INTERP_LINEAR), maxstep = 0.0f;
    for (float dl = 2.0f; dl <= 12.0f; dl += 0.01f) {
        float y = dl_read(&d, dl, DL_INTERP_LINEAR);
        float s = fabsf(y - prev); if (s > maxstep) maxstep = s;
        prev = y;
    }
    check("max step over fine sweep", maxstep, 0.01f, 1e-3f);  /* ~slope*step, not 1.0 */

    /* Wrap: delay that reaches across the buffer boundary stays finite/sane. */
    float w = dl_read(&d, 45.0f, DL_INTERP_LINEAR); /* wraps into zero-filled region */
    check("wrapped read finite", isfinite(w) ? 0.0f : 1.0f, 0.0f, 0.5f);

    /* Vintage quantizer: 12-bit step is ~1/2048; small signal snaps to a grid. */
    float q = dl_vintage_quantize(0.5f, 12, 0.0f);
    check("vintage 12-bit ~preserves 0.5", q, 0.5f, 1.0f/2048.0f);

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
