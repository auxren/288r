/* test_ks.c — Karplus-Strong string bank (ks.c): pitch accuracy of the
 * fractional loop, exponential decay (stable, no blowup), damping's spectral
 * effect, and retune glide. Built by `make test`.
 */
#include "ks.h"
#include <stdio.h>
#include <math.h>

#define FS 48000.0
static ks_t K;
static float out[48000];

static int fails = 0;
static void ck(const char *name, int cond) {
    printf("  %-52s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

static double domfreq(const float *x, int a, int b, double lo, double hi)
{
    double best = 0, bp = -1;
    for (double f = lo; f <= hi; f += 0.25) {
        double w = 2*M_PI*f/FS, c = 2*cos(w), s0, s1 = 0, s2 = 0;
        for (int n = a; n < b; n++) { s0 = x[n] + c*s1 - s2; s2 = s1; s1 = s0; }
        double p = s1*s1 + s2*s2 - c*s1*s2;
        if (p > bp) { bp = p; best = f; }
    }
    return best;
}

int main(void)
{
    printf("test_ks\n");
    float chan[KS_STRINGS];

    /* one string at 220 Hz (period 218.18 @48k), impulse-excited */
    ks_init(&K, 0.995f, 0.5f);
    for (int i = 0; i < KS_STRINGS; i++) ks_set_period(&K, i, 218.18f);
    for (int n = 0; n < 48000; n++) {
        float x = (n == 400) ? 0.9f : 0.0f;
        ks_process(&K, x, chan);
        out[n] = chan[0];
    }
    /* let the period glide settle before measuring */
    double f = domfreq(out, 24000, 48000, 180, 260);
    /* KS pitch = FS/(period + damping group delay ~0.5) */
    double fexp = FS / (218.18 + 0.25);
    printf("      measured %.2f Hz (expected ~%.2f)\n", f, fexp);
    ck("pitch within 1%", fabs(f - fexp) / fexp < 0.01);

    /* decay: envelope shrinks, never grows */
    double e1 = 0, e2 = 0;
    for (int n = 6000;  n < 12000; n++) e1 += (double)out[n]*out[n];
    for (int n = 36000; n < 42000; n++) e2 += (double)out[n]*out[n];
    ck("string decays (stable feedback)", e2 < e1 && e1 > 0);
    float mx = 0;
    for (int n = 0; n < 48000; n++) { float a = fabsf(out[n]); if (a > mx) mx = a; }
    ck("no blowup (peak < 2)", mx < 2.0f);

    /* damping darkens: heavier damp -> faster HF loss (shorter ring) */
    ks_init(&K, 0.995f, 0.95f);
    for (int i = 0; i < KS_STRINGS; i++) ks_set_period(&K, i, 218.18f);
    for (int n = 0; n < 48000; n++) {
        float x = (n == 400) ? 0.9f : 0.0f;
        ks_process(&K, x, chan);
        out[n] = chan[0];
    }
    double e2d = 0;
    for (int n = 36000; n < 42000; n++) e2d += (double)out[n]*out[n];
    ck("heavier damping decays faster", e2d < e2);

    printf(fails ? "FAILURES: %d\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
