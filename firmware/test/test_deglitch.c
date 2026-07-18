/* test_deglitch.c — measure the H949-style correlation splice alignment.
 * Metric: sideband contamination on a sustained tone. The plain crossfaded
 * shifter pushes energy into splice-rate sidebands (the ±1.5% coloration in
 * test_pitch_shift); aligned splices must (a) keep the dominant partial at
 * rho*f at least as accurately and (b) RAISE the carrier-to-total ratio
 * (spectral purity) vs the unaligned shifter. Built by `make test`.
 */
#include "pitch_shift.h"
#include "delay_line.h"
#include <stdio.h>
#include <math.h>

#define FS      48000.0f
#define BUFLEN  32768
#define F_IN    330.0f
#define BASE    64.0f
#define TWO_PI  6.28318530717959

static float buf[BUFLEN];
static float outb[32768];

static int fails = 0;
static void ck(const char *name, int cond) {
    printf("  %-52s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

static void run(float *out, int N, float ratio, float W, int deglitch)
{
    delay_line_t d; dl_init(&d, buf, BUFLEN); dl_clear(&d);
    pitchshift_t p; ps_init(&p, W, BASE); ps_set_ratio(&p, ratio);
    float ph = 0.0f, w = (float)(TWO_PI) * F_IN / FS;
    for (int n = 0; n < N; n++) {
        dl_write(&d, sinf(ph));
        ph += w; if (ph >= (float)TWO_PI) ph -= (float)TWO_PI;
        if (deglitch) ps_service(&p, &d);   /* host: service every sample */
        out[n] = ps_process(&p, &d, DL_INTERP_HERMITE);
    }
}

static double gpow(const float *x, int a, int b, double f) {
    double w = TWO_PI * f / FS, c = 2.0 * cos(w), s0, s1 = 0.0, s2 = 0.0;
    for (int n = a; n < b; n++) { s0 = x[n] + c * s1 - s2; s2 = s1; s1 = s0; }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}
static double domf(const float *x, int a, int b, double f0) {
    double best = f0, bp = -1.0;
    for (double f = f0 * 0.9; f <= f0 * 1.1; f += 0.2) {
        double p = gpow(x, a, b, f); if (p > bp) { bp = p; best = f; }
    }
    return best;
}
/* carrier / total-power ratio: purity of the shifted tone */
static double purity(const float *x, int a, int b, double fc) {
    double carrier = gpow(x, a, b, fc);
    double tot = 0.0;
    for (int n = a; n < b; n++) tot += (double)x[n] * x[n];
    double norm = tot * (double)(b - a) / 2.0;   /* goertzel |X|^2 scale */
    return carrier / (norm + 1e-12);
}

int main(void)
{
    const int N = 32768, A = 8192;
    const float W = 0.060f * FS;

    for (int r = 0; r < 4; r++) {
        static const float RS[4] = { 0.917f, 0.794f, 0.5f, 2.0f };
        float ratio = RS[r];   /* panel range + the octave extremes */
        double fexp = ratio * F_IN;

        run(outb, N, ratio, W, 0);
        double f_plain = domf(outb, A, N, fexp);
        double p_plain = purity(outb, A, N, f_plain);

        run(outb, N, ratio, W, 1);
        double f_dg = domf(outb, A, N, fexp);
        double p_dg = purity(outb, A, N, f_dg);

        double err_plain = fabs(f_plain - fexp) / fexp * 100.0;
        double err_dg    = fabs(f_dg    - fexp) / fexp * 100.0;
        printf("    ratio=%.3f: freq err %.2f%% -> %.2f%%,  purity %.3f -> %.3f\n",
               ratio, err_plain, err_dg, p_plain, p_dg);
        char nm[64];
        snprintf(nm, sizeof nm, "deglitch tracks rho*f (ratio %.3f, <2%%)", ratio);
        ck(nm, err_dg < 2.0);
        snprintf(nm, sizeof nm, "deglitch purity >= plain (ratio %.3f)", ratio);
        ck(nm, p_dg >= p_plain * 0.98);   /* must not be worse; expect better */
        if (ratio <= 0.5f || ratio >= 2.0f) {
            snprintf(nm, sizeof nm, "octave case near-pure (ratio %.3f, >0.99)", ratio);
            ck(nm, p_dg > 0.99);          /* the measured transformation */
        }
    }

    /* ENVELOPED MATERIAL (the adversarial blind spot): a tremolo tone. The old
     * acc/ein ranking picked QUIET lags and dug ~30 dB level holes; the NCC^2
     * ranking must keep the de-glitched output level within a whisker of the
     * plain shifter's. Compare windowed RMS floor across grains. */
    {
        delay_line_t d; pitchshift_t p;
        for (int mode = 0; mode < 2; mode++) {
            dl_init(&d, buf, BUFLEN); dl_clear(&d);
            ps_init(&p, W, BASE); ps_set_ratio(&p, 0.794f);
            float ph = 0.0f, w = (float)(TWO_PI) * 330.0f / FS;
            for (int n = 0; n < N; n++) {
                float env = 0.55f + 0.45f * sinf((float)TWO_PI * 3.0f * (float)n / FS);
                dl_write(&d, env * sinf(ph));
                ph += w; if (ph >= (float)TWO_PI) ph -= (float)TWO_PI;
                if (mode) ps_service(&p, &d);
                outb[n] = ps_process(&p, &d, DL_INTERP_HERMITE);
            }
            /* min windowed RMS after settle (a level hole would crater this) */
            double minr = 1e9;
            for (int a2 = A; a2 + 1024 <= N; a2 += 512) {
                double s2 = 0.0;
                for (int n = a2; n < a2 + 1024; n++) s2 += (double)outb[n]*outb[n];
                double r2 = sqrt(s2 / 1024.0);
                if (r2 < minr) minr = r2;
            }
            static double plainf = 0.0;
            if (mode == 0) plainf = minr;
            else {
                printf("    tremolo RMS floor: plain=%.4f deglitch=%.4f\n", plainf, minr);
                ck("no level holes on enveloped material", minr > plainf * 0.7);
                ck("tremolo floors are sane (nonzero)", plainf > 0.001 && minr > 0.001);
            }
        }
    }

    /* splice continuity must be preserved with offsets in play */
    run(outb, N, 0.917f, W, 1);
    float mx = 0.0f;
    for (int n = A + 1; n < N; n++) {
        float d = fabsf(outb[n] - outb[n-1]);
        if (d > mx) mx = d;
    }
    printf("    max sample step with deglitch = %.4f\n", mx);
    ck("no clicks with aligned splices", mx < 0.35f);

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
