/* test_bass.c — period-adaptive splice search (bass reach below ~125 Hz).
 * The fixed 768-sample correlation span cannot see a full period below
 * ~62 Hz at the 48 kHz host rate, so bass splices fell back to offset 0 and
 * thumped at the grain rate. The background period estimator + widened
 * search must (a) detect the period of a low tone, (b) raise purity vs the
 * fallback behavior, and (c) leave mid-frequency material untouched.
 * Built by `make test`.
 */
#include "pitch_shift.h"
#include "delay_line.h"
#include <stdio.h>
#include <math.h>

#define FS      48000.0f
#define BUFLEN  65536
#define BASE    64.0f
#define TWO_PI  6.28318530717959

static float buf[BUFLEN];
static float outb[BUFLEN];

static int fails = 0;
static void ck(const char *name, int cond) {
    printf("  %-56s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

static double gpow(const float *x, int a, int b, double f) {
    double w = TWO_PI * f / FS, c = 2.0 * cos(w), s0, s1 = 0.0, s2 = 0.0;
    for (int n = a; n < b; n++) { s0 = x[n] + c * s1 - s2; s2 = s1; s1 = s0; }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}
static double purity(const float *x, int a, int b, double fc) {
    double carrier = gpow(x, a, b, fc);
    double tot = 0.0;
    for (int n = a; n < b; n++) tot += (double)x[n] * x[n];
    return carrier / (tot * (double)(b - a) / 2.0 + 1e-12);
}

/* run a tone through the shifter; per_enable=0 suppresses the estimator by
 * zeroing confidence each service call (simulates the pre-extension engine) */
static pitchshift_t g_p;
static void run(float f_in, int N, float ratio, int per_enable)
{
    delay_line_t d; dl_init(&d, buf, BUFLEN); dl_clear(&d);
    ps_init(&g_p, 0.060f * FS, BASE); ps_set_ratio(&g_p, ratio);
    float ph = 0.0f, w = (float)TWO_PI * f_in / FS;
    for (int n = 0; n < N; n++) {
        dl_write(&d, sinf(ph));
        ph += w; if (ph >= (float)TWO_PI) ph -= (float)TWO_PI;
        ps_service(&g_p, &d);
        if (!per_enable) { g_p.per_conf = 0.0f; }
        outb[n] = ps_process(&g_p, &d, DL_INTERP_HERMITE);
    }
}

int main(void)
{
    printf("test_bass\n");
    const int N = BUFLEN, A = 16384;

    /* (a) the estimator finds a 40 Hz period (1200 samples @48k) */
    run(30.0f, N, 0.794f, 1);
    printf("      estimated period=%.0f (true 1600), conf=%.2f\n",
           (double)g_p.period, (double)g_p.per_conf);
    ck("period estimate within 8 samples of truth",
       fabsf(g_p.period - 1600.0f) <= 8.0f);
    ck("confidence high on a pure bass tone", g_p.per_conf > 0.8f);

    /* (b) deep-bass purity: 30 Hz (period 1600 > the standard 1200 MAXLAG,
     * so the old engine CANNOT align) shifted to ~23.8 Hz */
    double fexp = 0.794 * 30.0;
    run(30.0f, N, 0.794f, 0);
    double p_off = purity(outb, A, N, fexp);
    run(30.0f, N, 0.794f, 1);
    double p_on = purity(outb, A, N, fexp);
    printf("      30 Hz purity: fallback=%.3f adaptive=%.3f\n", p_off, p_on);
    ck("adaptive search improves bass purity", p_on > p_off + 0.02);
    ck("adaptive bass purity is decent (>0.85)", p_on > 0.85);

    /* (c) mid-frequency regression: 330 Hz must be unaffected */
    run(330.0f, N, 0.794f, 1);
    double p_mid = purity(outb, A, N, 0.794 * 330.0);
    printf("      330 Hz purity with estimator on = %.3f\n", p_mid);
    ck("mid-frequency purity unchanged (>0.98)", p_mid > 0.98);

    printf(fails ? "FAILURES: %d\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
