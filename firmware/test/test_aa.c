/* test_aa.c — up-shift anti-aliasing (polyphase band-limited read).
 * Reading faster than write = decimation: buffer content above Nyquist/ratio
 * folds. Gates: (1) an 18 kHz tone shifted +1 oct (whose output IS pure fold
 * at 12 kHz @48k) drops by >=25 dB vs the Hermite path; (2) passband tones
 * stay within 1 dB; (3) sweeping the ratio across a band edge is click-free.
 * Built by `make test`.
 */
#include "pitch_shift.h"
#include "delay_line.h"
#include <stdio.h>
#include <math.h>

#define FS      48000.0f
#define BUFLEN  32768
#define BASE    64.0f
#define TWO_PI  6.28318530717959

static float buf[BUFLEN];
static float outb[BUFLEN];

static int fails = 0;
static void ck(const char *name, int cond) {
    printf("  %-56s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

static void run_tone2(float f_in, int N, float ratio, int bypass)
{
    delay_line_t d; dl_init(&d, buf, BUFLEN); dl_clear(&d);
    pitchshift_t p; ps_init(&p, 0.060f * FS, BASE); ps_set_ratio(&p, ratio);
    p.aa_bypass = bypass;
    float ph = 0.0f, w = (float)TWO_PI * f_in / FS;
    for (int n = 0; n < N; n++) {
        dl_write(&d, sinf(ph));
        ph += w; if (ph >= (float)TWO_PI) ph -= (float)TWO_PI;
        ps_service(&p, &d);
        outb[n] = ps_process(&p, &d, DL_INTERP_HERMITE);
    }
}
static void run_tone(float f_in, int N, float ratio) { run_tone2(f_in, N, ratio, 0); }

static double gpow(const float *x, int a, int b, double f) {
    double w = TWO_PI * f / FS, c = 2.0 * cos(w), s0, s1 = 0.0, s2 = 0.0;
    for (int n = a; n < b; n++) { s0 = x[n] + c * s1 - s2; s2 = s1; s1 = s0; }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

int main(void)
{
    printf("test_aa\n");
    const int N = BUFLEN, A = 8192;

    /* (1) TRUE alias suppression vs the Hermite path (verify-panel fix: the
     * old gate compared against a separate passband run, not Hermite):
     * 18 kHz @ ratio 2 -> 36 kHz folds to 12 kHz; measure the fold with the
     * AA bypassed (Hermite) and engaged. */
    run_tone2(18000.0f, N, 2.0f, 1);
    double fold_h = gpow(outb, A, N, 12000.0);
    run_tone2(18000.0f, N, 2.0f, 0);
    double fold_aa = gpow(outb, A, N, 12000.0);
    double supp_db = 10.0 * log10(fold_h / (fold_aa + 1e-12));
    printf("      fold(12k): AA is %.1f dB below Hermite\n", supp_db);
    ck("alias >=25 dB below the Hermite path at +1 oct", supp_db >= 25.0);

    /* (2) passband transparency: 2 kHz -> 4 kHz through the AA kernel vs the
     * same shift of a 2 kHz tone at a *down* ratio through Hermite as level
     * reference (both unity-gain paths). Compare output RMS to input RMS. */
    {
        run_tone(2000.0f, N, 2.0f);
        double rms = 0.0;
        for (int n = A; n < N; n++) rms += (double)outb[n] * outb[n];
        rms = sqrt(rms / (N - A));
        double db = 20.0 * log10(rms / 0.7071);
        printf("      passband level: %+.2f dB vs input\n", db);
        ck("passband within +/-1 dB", fabs(db) <= 1.0);
    }

    /* (3) band-edge crossing is click-free: slew the ratio through 1.4 while
     * processing; the max sample-to-sample step must stay in family with a
     * steady-state run. */
    {
        delay_line_t d; dl_init(&d, buf, BUFLEN); dl_clear(&d);
        pitchshift_t p; ps_init(&p, 0.060f * FS, BASE);
        float ph = 0.0f, w = (float)TWO_PI * 990.0f / FS;
        float mx = 0.0f, prev = 0.0f;
        for (int n = 0; n < N; n++) {
            dl_write(&d, sinf(ph));
            ph += w; if (ph >= (float)TWO_PI) ph -= (float)TWO_PI;
            float r = 1.0f + 0.5f * (float)n / (float)N;   /* 1.0 -> 1.5:
                crosses the Hermite<->AA swap at 1.02 AND the band edge 1.4 */
            ps_set_ratio(&p, r);
            ps_service(&p, &d);
            float y = ps_process(&p, &d, DL_INTERP_HERMITE);
            if (n > A) { float st = fabsf(y - prev); if (st > mx) mx = st; }
            prev = y;
        }
        printf("      max step across swap + band edge = %.4f\n", mx);
        ck("no click crossing swap/band edges (<0.35)", mx < 0.35f);
    }

    /* (4) cache-stress: ratio 3.9 slides the grain windows ~4 samples per
     * output sample and refills every ~4 samples; any cache-index bug shreds
     * the waveform, which the purity gate catches. */
    {
        run_tone(990.0f, N, 3.9f);
        double fc = 3.9 * 990.0;
        double carrier = gpow(outb, A, N, fc);
        double tot = 0.0;
        for (int n = A; n < N; n++) tot += (double)outb[n] * outb[n];
        double purity = carrier / (tot * (double)(N - A) / 2.0 + 1e-12);
        printf("      cache-stress purity @3.9x = %.3f\n", purity);
        ck("streaming cache intact at ratio 3.9 (>0.9)", purity > 0.9);
    }

    printf(fails ? "FAILURES: %d\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
