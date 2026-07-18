/* test_saturation.c — soft-clip waveshaper (see src/saturation.c). Verifies the
 * properties that make it a usable analog voice: clean bypass, odd symmetry,
 * boundedness, monotonicity (no fold-back), low-level unity, and that it actually
 * generates harmonics that grow with drive. Built by `make test`.
 */
#include "saturation.h"
#include <stdio.h>
#include <math.h>

#ifndef M_PI                     /* strict -std=c11 glibc doesn't define it */
#define M_PI 3.14159265358979323846
#endif

#define FS 48000.0f

static int fails = 0;
static void ck(const char *name, int cond) {
    printf("  %-46s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

static double gpow(const float *x, int n, double f) {
    double w = 2.0 * M_PI * f / FS, c = 2.0 * cos(w), s0, s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < n; i++) { s0 = x[i] + c * s1 - s2; s2 = s1; s1 = s0; }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

/* Total harmonic energy (2f..5f) relative to the fundamental. Uses a bin-aligned
 * tone (f0 = 85*FS/N, harmonics land on exact Goertzel bins) so a clean signal
 * reads ~0 THD with no spectral-leakage floor. */
#define THD_N 4096
static double thd(float drive, float mix) {
    saturation_t s; sat_init(&s, drive, mix);
    const double f0 = 85.0 * FS / THD_N;
    static float y[THD_N];
    for (int n = 0; n < THD_N; n++)
        y[n] = sat_process(&s, (float)sin(2.0 * M_PI * f0 * n / FS));
    double f = gpow(y, THD_N, f0);
    double h = gpow(y, THD_N, 2 * f0) + gpow(y, THD_N, 3 * f0)
             + gpow(y, THD_N, 4 * f0) + gpow(y, THD_N, 5 * f0);
    return sqrt(h / f);
}

int main(void)
{
    saturation_t s;

    /* clean bypass: mix=0 -> output == input exactly */
    sat_init(&s, 8.0f, 0.0f);
    int exact = 1;
    for (float x = -1.0f; x <= 1.0f; x += 0.01f) if (sat_process(&s, x) != x) { exact = 0; break; }
    ck("mix=0 is exact bypass", exact);

    /* odd symmetry: sat(-x) == -sat(x) */
    sat_init(&s, 4.0f, 0.8f);
    int odd = 1;
    for (float x = 0.0f; x <= 1.0f; x += 0.01f)
        if (fabsf(sat_process(&s, -x) + sat_process(&s, x)) > 1e-6f) { odd = 0; break; }
    ck("odd-symmetric (only odd harmonics)", odd);

    /* bounded: |y| <= 1 for |x| <= 1, any drive/mix */
    sat_init(&s, 32.0f, 1.0f);
    int bounded = 1;
    for (float x = -1.0f; x <= 1.0f; x += 0.005f) if (fabsf(sat_process(&s, x)) > 1.0f + 1e-6f) { bounded = 0; break; }
    ck("bounded to [-1,1]", bounded);

    /* monotonic: no fold-back (waveshaper must be a function, not a fold) */
    int mono = 1; float prev = sat_process(&s, -1.0f);
    for (float x = -1.0f + 0.002f; x <= 1.0f; x += 0.002f) {
        float cur = sat_process(&s, x);
        if (cur < prev - 1e-6f) { mono = 0; break; }
        prev = cur;
    }
    ck("monotonic (no fold-back)", mono);

    /* low-level unity: drive=1, mix=1, small x -> ~x (clean at low level) */
    sat_init(&s, 1.0f, 1.0f);
    ck("unity at low level (<2%)", fabsf(sat_process(&s, 0.01f) - 0.01f) / 0.01f < 0.02f);

    /* harmonic generation grows with drive; clean when bypassed */
    double t_clean = thd(8.0f, 0.0f);
    double t_soft  = thd(2.0f, 1.0f);
    double t_hard  = thd(10.0f, 1.0f);
    printf("    THD: bypass=%.4f  drive2=%.4f  drive10=%.4f\n", t_clean, t_soft, t_hard);
    ck("bypass is clean (THD ~ 0)", t_clean < 1e-3);
    ck("saturation adds harmonics", t_soft > 0.01);
    ck("more drive -> more harmonics", t_hard > t_soft);

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
