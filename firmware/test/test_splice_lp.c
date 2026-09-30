/* test_splice_lp.c — anti-aliased (box low-passed) SRAM-resident splice search.
 *
 * The coarse splice search decimates its correlation grid by up to 8 for bass
 * windows. A plain STRIDE (take every 8th sample) is decimation without a
 * low-pass: a strong partial well above the decimated Nyquist aliases in the
 * LAG domain into a slow cosine of comparable size to the fundamental's own
 * correlation peak, and the coarse search locks to the wrong lag (the fine
 * ±3 pass cannot recover a whole-period error). Averaging each run of kstep
 * samples instead (a box low-pass with its first null at fs/kstep, ~-46 dB on
 * the partial used here) is the paper's recipe: Haghparast, Penttinen,
 * Välimäki, "Real-Time Pitch-Shifting ... Using Normalized Filtered
 * Correlation TSM", DAFx 2007 — correlate a normalized, low-passed copy whose
 * cutoff sits just above the highest fundamental.
 *
 * Also asserts the read-head proximity floor from the same paper,
 * dmin = (alpha_max - 1)*L_xfade + N/2: no grain read may come within the AA
 * half-kernel of the write head, at any ratio, with FM at full negative swing
 * and a wrapped loop window (p->min_dist telemetry).
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
/* purity of the fundamental after a 2-pole ~300 Hz low-pass (the inharmonic
 * partial is deliberately still in the output; we judge the bass only) */
static double purity_lp(const float *x, int a, int b, double fc) {
    static float y[BUFLEN];
    float l1 = 0.0f, l2 = 0.0f, k = (float)(TWO_PI * 300.0 / FS);
    for (int n = 0; n < b; n++) {
        l1 += (x[n] - l1) * k; l2 += (l1 - l2) * k; y[n] = l2;
    }
    double carrier = gpow(y, a, b, fc), tot = 0.0;
    for (int n = a; n < b; n++) tot += (double)y[n] * y[n];
    return carrier / (tot * (double)(b - a) / 2.0 + 1e-12);
}

static pitchshift_t g_p;
static void run(float f0, float f1, float a1, int N, float ratio, int box)
{
    delay_line_t d; dl_init(&d, buf, BUFLEN); dl_clear(&d);
    ps_init(&g_p, 0.060f * FS, BASE); ps_set_ratio(&g_p, ratio);
    g_p.srch_box = box;
    float ph0 = 0.0f, w0 = (float)TWO_PI * f0 / FS;
    float ph1 = 0.3f, w1 = (float)TWO_PI * f1 / FS;
    for (int n = 0; n < N; n++) {
        dl_write(&d, sinf(ph0) + a1 * sinf(ph1));
        ph0 += w0; if (ph0 >= (float)TWO_PI) ph0 -= (float)TWO_PI;
        ph1 += w1; if (ph1 >= (float)TWO_PI) ph1 -= (float)TWO_PI;
        ps_service(&g_p, &d);
        outb[n] = ps_process(&g_p, &d, DL_INTERP_HERMITE);
    }
}

int main(void)
{
    printf("test_splice_lp\n");
    const int N = BUFLEN, A = 16384;
    const float F0 = 35.0f;          /* period 1371 @48k -> kstep 8 search */
    const float F1 = 6047.0f;        /* inharmonic partial above fs/16      */
    const double fexp = 0.794 * F0;

    /* (a) bass + bright inharmonic partial: box search aligns the bass */
    run(F0, F1, 1.2f, N, 0.794f, 1);
    double p_box = purity_lp(outb, A, N, fexp);
    printf("      period=%.0f conf=%.2f\n", (double)g_p.period, (double)g_p.per_conf);
    run(F0, F1, 1.2f, N, 0.794f, 0);
    double p_raw = purity_lp(outb, A, N, fexp);
    printf("      35 Hz + 6047 Hz purity: raw-stride=%.3f box=%.3f\n", p_raw, p_box);
    ck("box search: bass purity under a bright partial > 0.9", p_box > 0.90);
    ck("box search beats the raw stride search", p_box > p_raw + 0.10);

    /* (b) pure bass: box must not regress the plain case */
    run(F0, F1, 0.0f, N, 0.794f, 0);
    double q_raw = purity_lp(outb, A, N, fexp);
    run(F0, F1, 0.0f, N, 0.794f, 1);
    double q_box = purity_lp(outb, A, N, fexp);
    printf("      35 Hz alone purity: raw-stride=%.3f box=%.3f\n", q_raw, q_box);
    ck("pure bass purity not regressed", q_box > q_raw - 0.02 && q_box > 0.85);

    /* (c) mid-frequency regression (default search, kstep 2) */
    run(330.0f, F1, 0.0f, N, 0.794f, 1);
    double p_mid = purity_lp(outb, A, N, 0.794 * 330.0);
    printf("      330 Hz purity = %.3f\n", p_mid);
    ck("mid-frequency purity unchanged (>0.98)", p_mid > 0.98);

    /* (d) dmin floor: firmware base 256, FM swing -96, AA half-kernel 8 */
    {
        delay_line_t d; dl_init(&d, buf, BUFLEN); dl_clear(&d);
        ps_init(&g_p, 0.060f * FS, 256.0f);
        for (int n = 0; n < 20000; n++) dl_write(&d, sinf((float)n * 0.01f));
        /* wrapped loop window: end < start */
        ps_set_loop_window(&g_p, 30000u, 5000u, BUFLEN);
        float mind = 1.0e9f;
        for (int s = 0; s < 8; s++) {
            float ratio = 0.25f + 3.75f * (float)s / 7.0f;
            ps_set_ratio(&g_p, ratio);
            for (int n = 0; n < 12000; n++) {
                g_p.fm_in = -96.0f;
                dl_write(&d, sinf((float)n * 0.01f));
                ps_service(&g_p, &d);
                (void)ps_process(&g_p, &d, DL_INTERP_HERMITE);
                if (g_p.min_dist < mind) mind = g_p.min_dist;
            }
        }
        printf("      min grain read distance = %.1f (floor 152)\n", (double)mind);
        ck("no grain read inside base - FM - AA half-kernel", mind >= 256.0f - 96.0f - 8.0f);
    }

    printf(fails ? "FAILURES: %d\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
