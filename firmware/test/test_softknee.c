/* test_softknee.c — output soft-knee limiter (patched-feedback quality).
 * Transparent below the knee, monotonic, asymptotic to FS, C1-ish continuity,
 * and a simulated unity+ feedback loop must self-limit smoothly (no wrap, no
 * hard-clip flat-tops). Built by `make test`. */
#include "audio_io.h"
#include <stdio.h>
#include <math.h>

static int fails = 0;
static void ck(const char *name, int cond) {
    printf("  %-50s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}
static float roundtrip(float x) {   /* through the DAC word and back */
    int32_t w = audio_f_to_out(x);
    int32_t s = (w << 8) >> 8;
    return (float)s / 8388607.0f;
}

int main(void)
{
    /* transparent below the knee (bit-exact modulo 24-bit quantization) */
    int transparent = 1;
    for (float x = -0.74f; x <= 0.74f; x += 0.01f)
        if (fabsf(roundtrip(x) - x) > 2.0f / 8388607.0f) transparent = 0;
    ck("transparent below 0.75 FS", transparent);

    /* monotonic + bounded above the knee */
    int mono = 1; float prev = roundtrip(0.70f);
    for (float x = 0.71f; x <= 3.0f; x += 0.01f) {
        float y = roundtrip(x);
        if (y < prev - 1e-6f) mono = 0;
        prev = y;
    }
    ck("monotonic through the knee", mono);
    ck("bounded at FS (x=3 -> <1.0)", roundtrip(3.0f) <= 1.0f && roundtrip(3.0f) > 0.95f);
    ck("C1-ish at the knee (no step)", fabsf(roundtrip(0.751f) - roundtrip(0.749f)) < 0.005f);

    /* simulated patched feedback at gain 1.15: level must settle smoothly under
       the asymptote, never wrap, never flat-top for long runs */
    float x = 0.1f; float peak = 0.0f; int flat = 0, prevflat = 0;
    for (int n = 0; n < 20000; n++) {
        float y = roundtrip(x * 1.15f);        /* loop gain > 1 */
        if (y > peak) peak = y;
        if (y > 0.999f) { flat++; if (prevflat) {} prevflat = 1; } else prevflat = 0;
        x = y;
    }
    printf("    feedback settle peak = %.4f (flat-top samples: %d)\n", peak, flat);
    ck("feedback settles below FS", peak < 1.0f);
    ck("no sustained flat-topping", flat < 100);

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
