/* test_config_dip.c — config DIP sw1 (x10 delay extend) + sw2 (bandwidth limit).
 *
 * sw2: the one-pole record-path low-pass (bwlimit) must pass lows, be ~-3 dB near
 * the 11025 Hz cutoff, roll off highs, and be bit-exact bypass when off.
 * sw1: engine_clamp_base must pass a base that fits and cap one that would run the
 * deepest tap (base*time_hi) past the buffer.
 *
 * Built by `make test` (links the engine .c files, all of src except main.c).
 */
#include "bwlimit.h"
#include "engine.h"
#include <stdio.h>
#include <math.h>

#define FS 96000.0f

static int fails = 0;
static void ck(const char *name, int cond) {
    printf("  %-40s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

/* |H(f)| of the bandwidth filter: RMS(out)/RMS(in) after the transient settles. */
static float mag(float cutoff, float f)
{
    bwlimit_t b; bw_init(&b, FS, cutoff);
    const int WARM = 6000, MEAS = 8000;
    float w = 2.0f * 3.14159265f * f / FS, ph = 0.0f;
    double si = 0.0, so = 0.0;
    for (int n = 0; n < WARM + MEAS; n++) {
        float x = sinf(ph); ph += w; if (ph > 6.2831853f) ph -= 6.2831853f;
        float y = bw_process(&b, x);
        if (n >= WARM) { si += (double)x * x; so += (double)y * y; }
    }
    return (float)sqrt(so / si);
}

int main(void)
{
    /* ---- sw2: bandwidth limit (11025 Hz) ---- */
    printf("bandwidth limit (fc=11025 Hz @ 96k):\n");
    float lo  = mag(11025.0f, 1000.0f);
    float mid = mag(11025.0f, 11025.0f);
    float hi  = mag(11025.0f, 35000.0f);
    printf("    |H(1k)|=%.3f  |H(11k)|=%.3f  |H(35k)|=%.3f\n", lo, mid, hi);
    ck("passes lows (|H(1k)| > 0.9)",          lo > 0.9f);
    ck("~-3dB near cutoff (0.5<|H(11k)|<0.75)", mid > 0.5f && mid < 0.75f);
    ck("rolls off highs (|H(35k)| < 0.4)",     hi < 0.4f);
    ck("monotonic lo > mid > hi",              lo > mid && mid > hi);

    /* bypass: cutoff off => exact passthrough at any frequency */
    printf("bandwidth bypass (off):\n");
    float bp = mag(0.0f, 35000.0f);
    printf("    |H(35k)| bypass = %.4f\n", bp);
    ck("bypass is ~unity (|H| > 0.99)", bp > 0.99f);
    {   /* bit-exact: bw_init(...,0) must yield y == x */
        bwlimit_t b; bw_init(&b, FS, 0.0f);
        int exact = 1;
        float ph = 0.0f;
        for (int n = 0; n < 500; n++) {
            float x = sinf(ph); ph += 0.2f;
            if (bw_process(&b, x) != x) { exact = 0; break; }
        }
        ck("bypass is bit-exact (y == x)", exact);
    }

    /* ---- sw1: x10 delay extend clamp ---- */
    printf("delay-extend base clamp (time_hi=1.6):\n");
    const uint32_t LEN = 2097152u;          /* 8 MB / sizeof(float) */
    const float TH = 1.6f;
    float fits    = engine_clamp_base(96000.0f * 10.0f, LEN, TH);   /* x10 FULL: 960k */
    float capped  = engine_clamp_base(2000000.0f, LEN, TH);         /* would overflow */
    float capmax  = ((float)LEN - 64.0f) / TH;
    printf("    x10 FULL -> %.0f (expect 960000)   overlong -> %.0f (cap %.0f)\n",
           fits, capped, capmax);
    ck("x10 FULL fits unchanged", fabsf(fits - 960000.0f) < 1.0f);
    ck("deepest tap fits buffer", fits * TH < (float)LEN);
    ck("overlong base is capped", fabsf(capped - capmax) < 1.0f);
    ck("capped deepest tap fits", capped * TH < (float)LEN);

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
