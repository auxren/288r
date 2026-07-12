/* test_crossfade.c — the crossfade must turn a delay-time JUMP (which clicks) into
 * a smooth, click-free handoff.
 * cc -std=c11 -Wall -Wextra -I../src test_crossfade.c ../src/crossfade.c ../src/delay_line.c -o /tmp/t -lm && /tmp/t
 */
#include "crossfade.h"
#include <stdio.h>
#include <math.h>

static int fails = 0;
static void ok(const char *what, int cond) {
    printf(cond ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!cond) fails++;
}

static float buf[4096];

int main(void)
{
    delay_line_t dl;
    dl_init(&dl, buf, 4096);
    dl_clear(&dl);

    /* Build a buffer with two very different regions: older half = +1, recent
     * half = -1. So a large delay reads +1, a small delay reads -1. A raw jump
     * between them is a step of 2.0 (a loud click). */
    for (int k = 0; k < 1000; k++) dl_write(&dl,  1.0f);
    for (int k = 0; k < 1000; k++) dl_write(&dl, -1.0f);
    /* wpos = 2000: delay 1500 -> index 500 (+1 region); delay 200 -> index 1800 (-1). */
    const float D1 = 1500.0f, D2 = 200.0f;

    /* Baseline: interpolated reads land on the region values. */
    ok("read at D1 ~ +1", fabsf(dl_read(&dl, D1, DL_INTERP_LINEAR) - 1.0f) < 1e-3f);
    ok("read at D2 ~ -1", fabsf(dl_read(&dl, D2, DL_INTERP_LINEAR) + 1.0f) < 1e-3f);
    const float raw_jump_step = 2.0f;  /* what a naive delay change would do */

    /* Crossfade over 64 samples from D1 to D2. */
    xfade_t x;
    xfade_init(&x, /*fade_samples*/ 64.0f, /*initial_delay*/ D1);
    ok("settled read == D1 value", fabsf(xfade_read(&x, &dl, DL_INTERP_LINEAR) - 1.0f) < 1e-3f);

    xfade_trigger(&x, D2);
    float prev = 1.0f, maxstep = 0.0f, first = 0.0f, last = 0.0f;
    for (int k = 0; k < 80; k++) {
        float y = xfade_read(&x, &dl, DL_INTERP_LINEAR);
        if (k == 0) first = y;
        float s = fabsf(y - prev); if (s > maxstep) maxstep = s;
        prev = y; last = y;
    }
    ok("no click at trigger (first step tiny)", fabsf(first - 1.0f) < 0.1f);   /* not jumped to -1 */
    ok("max step << raw jump", maxstep < raw_jump_step / 20.0f);               /* ~2/64, not 2.0 */
    ok("fade completes at D2 value", fabsf(last + 1.0f) < 1e-3f);
    ok("fade finished (not fading)", x.fading == 0);

    /* After the fade, a plain read tracks the new delay with no extra cost. */
    ok("post-fade read == D2 value", fabsf(xfade_read(&x, &dl, DL_INTERP_LINEAR) + 1.0f) < 1e-3f);

    /* Auto threshold: a small change moves continuously (no snap); a big one snaps. */
    xfade_init(&x, 64.0f, 100.0f);
    ok("small change does not snap", xfade_set_delay(&x, 100.5f, /*snap*/ 10.0f) == 0);
    ok("big change snaps",           xfade_set_delay(&x, 500.0f, /*snap*/ 10.0f) == 1);

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
