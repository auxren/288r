/* test_pitch_taps.c — the pitch voice's per-tap delay ring (pitch_taps.c):
 * exact integer-delay recall, linear interpolation, wrap correctness across
 * many ring laps, and depth clamping. Built by `make test`.
 */
#include "pitch_taps.h"
#include <stdio.h>
#include <math.h>

static int fails = 0;
static void ck(const char *name, int cond) {
    printf("  %-52s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

static float ring[1024];

int main(void)
{
    printf("test_pitch_taps\n");
    ptaps_t p; pt_init(&p, ring, 1024); pt_clear(&p);

    /* write a known ramp: sample n has value n */
    for (int n = 0; n < 5000; n++) pt_write(&p, (float)n);
    /* most recent = 4999; delay d -> 4999-d */
    ck("integer delay recall (d=0)",   pt_read(&p, 0, 0.0f)   == 4999.0f);
    ck("integer delay recall (d=100)", pt_read(&p, 100, 0.0f) == 4899.0f);
    ck("integer delay recall (d=1000)",pt_read(&p, 1000, 0.0f)== 3999.0f);

    /* linear interp: halfway between 4899 and 4898 */
    ck("linear interp midpoint", fabsf(pt_read(&p, 100, 0.5f) - 4898.5f) < 1e-3f);

    /* clamp: beyond ring depth returns the oldest usable sample, no wrap-around
     * into fresh data */
    float deep = pt_read(&p, 4000, 0.0f);
    ck("depth clamp (d=4000 in a 1024 ring)", deep == pt_read(&p, 1022, 0.0f));
    ck("clamped read is the oldest lap, not future", deep == 4999.0f - 1022.0f);

    /* wrap across many laps: sequence continuity at an arbitrary point */
    int cont = 1;
    for (int d = 0; d < 1000; d++)
        if (pt_read(&p, (uint32_t)d, 0.0f) != (float)(4999 - d)) cont = 0;
    ck("continuity over 1000 delays after 5 laps", cont);

    printf(fails ? "FAILURES: %d\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
