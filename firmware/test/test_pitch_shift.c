/* test_pitch_shift.c — crossfaded-tap pitch shifter (see src/pitch_shift.c and
 * firmware/PITCH_SHIFT.md). Host reference on the float delay_line, no hardware.
 *
 * Tolerances are deliberately honest and match the note's "Measured behavior":
 * the RATE MATH is exact (single ramping tap tracks rho*f tightly), but the
 * two-tap crossfade adds a few-percent, W-dependent coloration on sustained tones
 * (the documented vintage artifact, NOT a bug). We test the properties that must
 * hold: rate tracking (loose), single-tap rate (tight), click-free wraps (strict),
 * unity bypass (bit-exact), and detune level stability.
 *
 * cc -std=c11 -Wall -Wextra -I../src test_pitch_shift.c ../src/pitch_shift.c \
 *    ../src/delay_line.c -o /tmp/t -lm && /tmp/t
 */
#include "pitch_shift.h"
#include "delay_line.h"
#include <stdio.h>
#include <math.h>

#define FS      48000.0f
#define BUFLEN  8192
#define F_IN    440.0f
#define BASE    64.0f
#define TWO_PI  6.28318530717959

static float buf[BUFLEN];
static float outb[16384];
static float outb2[16384];

/* Stream a sine at f_in into the line and read it back through the pitch shifter.
 * With the write head advancing 1/sample and the read delay ramping by (1-rho),
 * the read traverses the recording at rate rho -> output pitch = rho * f_in. */
static void run_ps(float *out, int N, float f_in, float ratio, float W)
{
    delay_line_t d; dl_init(&d, buf, BUFLEN); dl_clear(&d);
    pitchshift_t p; ps_init(&p, W, BASE); ps_set_ratio(&p, ratio);
    float ph = 0.0f, w = (float)(TWO_PI) * f_in / FS;
    for (int n = 0; n < N; n++) {
        dl_write(&d, sinf(ph));
        ph += w; if (ph >= (float)TWO_PI) ph -= (float)TWO_PI;
        out[n] = ps_process(&p, &d, DL_INTERP_HERMITE);
    }
}

/* Pure rate math, isolated: ramp the read delay MONOTONICALLY across most of one
 * window (no wrap, no crossfade), so the read traverses the recording at exactly
 * rate rho -> a clean sine at rho*f with no splice sidebands. Pre-fills history so
 * the deepest tap reads valid audio from sample 0. Returns #output samples. */
static int run_rate(float *out, float f_in, float ratio, float W)
{
    delay_line_t d; dl_init(&d, buf, BUFLEN); dl_clear(&d);
    float ph = 0.0f, w = (float)(TWO_PI) * f_in / FS;
    int prefill = (int)(BASE + W) + 256;
    for (int i = 0; i < prefill; i++) {
        dl_write(&d, sinf(ph)); ph += w; if (ph >= (float)TWO_PI) ph -= (float)TWO_PI;
    }
    float step = (1.0f - ratio) / W;               /* + for down-shift, - for up */
    float frac = (step > 0.0f) ? 0.05f : 0.95f;    /* start at the entering edge */
    int n = 0;
    while (frac >= 0.05f && frac <= 0.95f && n < 16384) {
        dl_write(&d, sinf(ph)); ph += w; if (ph >= (float)TWO_PI) ph -= (float)TWO_PI;
        out[n++] = dl_read(&d, BASE + frac * W, DL_INTERP_HERMITE);
        frac += step;
    }
    return n;
}

/* Single ramping tap WITH the wrap (no crossfade): the clicky reference for the
 * click-free test — same phase advance as ps_process, so it wraps and splices. */
static void run_singletap(float *out, int N, float f_in, float ratio, float W)
{
    delay_line_t d; dl_init(&d, buf, BUFLEN); dl_clear(&d);
    float ph = 0.0f, w = (float)(TWO_PI) * f_in / FS, frac = 0.0f;
    for (int n = 0; n < N; n++) {
        dl_write(&d, sinf(ph));
        ph += w; if (ph >= (float)TWO_PI) ph -= (float)TWO_PI;
        out[n] = dl_read(&d, BASE + frac * W, DL_INTERP_HERMITE);
        frac += (1.0f - ratio) / W;
        while (frac >= 1.0f) frac -= 1.0f;
        while (frac <  0.0f) frac += 1.0f;
    }
}

/* Goertzel power of x[a..b) at freq. */
static double goertzel_pow(const float *x, int a, int b, double freq)
{
    double w = TWO_PI * freq / FS, c = 2.0 * cos(w), s0, s1 = 0.0, s2 = 0.0;
    for (int n = a; n < b; n++) { s0 = x[n] + c * s1 - s2; s2 = s1; s1 = s0; }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

/* Dominant partial of x[a..b) via a Goertzel peak search around f0 (Hz). */
static double dom_freq(const float *x, int a, int b, double f0)
{
    double best = f0, bp = -1.0;
    for (double f = f0 * 0.60; f <= f0 * 1.40; f += 0.25) {
        double p = goertzel_pow(x, a, b, f);
        if (p > bp) { bp = p; best = f; }
    }
    return best;
}

/* max |x[n]-x[n-1]| over [a,b). */
static float max_step(const float *x, int a, int b)
{
    float m = 0.0f;
    for (int n = a + 1; n < b; n++) { float d = fabsf(x[n] - x[n-1]); if (d > m) m = d; }
    return m;
}

static float rms(const float *x, int a, int b)
{
    double s = 0.0; for (int n = a; n < b; n++) s += (double)x[n] * x[n];
    return (float)sqrt(s / (b - a));
}

int main(void)
{
    int fails = 0;
    const int N = 16384, A = 4096;            /* analyze [A,N): skip transient */
    const float W = 0.060f * FS;              /* 60 ms window (>= 50 ms, cleaner) */

    /* ---- 1. Frequency tracking (loose, ~few %): crossfaded output ---- */
    printf("  frequency tracking (W=60ms, tol 4%%):\n");
    const float ratios[] = { 0.5f, 0.7937f, 1.5f, 2.0f };  /* incl. -4 semitones */
    for (int i = 0; i < 4; i++) {
        float r = ratios[i], expect = r * F_IN;
        run_ps(outb, N, F_IN, r, W);
        double got = dom_freq(outb, A, N, expect);
        float err = (float)(got - expect) / expect;
        printf("    rho=%.4f  expect=%.1f  got=%.1f  err=%+.2f%%\n", r, expect, got, 100.0f*err);
        if (fabsf(err) > 0.04f) { printf("    FAIL rho=%.3f off by %.2f%%\n", r, 100.0f*err); fails++; }
    }

    /* ---- 2. Single-tap rate check (tight, 1.5%): monotonic ramp, no wrap/crossfade
     *        -> pure rate math, no splice coloration. Should track rho*f tightly. ---- */
    printf("  single-tap rate (monotonic, no crossfade, tol 1.5%%):\n");
    const float rate_ratios[] = { 0.5f, 0.7937f, 1.5f, 2.0f };
    for (int i = 0; i < 4; i++) {
        float r = rate_ratios[i], expect = r * F_IN;
        int m = run_rate(outb, F_IN, r, W);
        double got = dom_freq(outb, 128, m - 128, expect);
        float err = (float)(got - expect) / expect;
        printf("    rho=%.4f  expect=%.1f  got=%.1f  err=%+.2f%%  (%d samp)\n",
               r, expect, got, 100.0f*err, m);
        if (fabsf(err) > 0.015f) { printf("    FAIL single-tap rate off by %.2f%%\n", 100.0f*err); fails++; }
    }

    /* ---- 3. Click-free wraps (strict): crossfade smooth, single-tap clicks ---- */
    printf("  click-free wraps:\n");
    for (int i = 0; i < 2; i++) {
        float r = (i == 0) ? 2.0f : 0.5f;               /* up- and down-shift */
        run_ps(outb, N, F_IN, r, W);                    /* crossfaded */
        run_singletap(outb2, N, F_IN, r, W);            /* no crossfade */
        float sm = max_step(outb, A, N), cl = max_step(outb2, A, N);
        printf("    rho=%.1f  crossfade max-step=%.4f   single-tap max-step=%.4f\n", r, sm, cl);
        if (!(sm < 0.35f)) { printf("    FAIL crossfade has clicks (%.4f)\n", sm); fails++; }
        if (!(cl > sm))    { printf("    FAIL single-tap not rougher (test not meaningful)\n"); fails++; }
    }

    /* ---- 4. Unity bypass (bit-exact): rho=1.0 == dl_read(base + 0.5W) ---- */
    {
        printf("  unity bypass (bit-exact):\n");
        delay_line_t d; dl_init(&d, buf, BUFLEN); dl_clear(&d);
        pitchshift_t p; ps_init(&p, W, BASE); ps_set_ratio(&p, 1.0f);
        float ph = 0.0f, w = (float)(TWO_PI) * F_IN / FS; int bad = 0;
        for (int n = 0; n < 4000; n++) {
            dl_write(&d, sinf(ph));
            ph += w; if (ph >= (float)TWO_PI) ph -= (float)TWO_PI;
            float got = ps_process(&p, &d, DL_INTERP_HERMITE);
            float ref = dl_read(&d, BASE + 0.5f * W, DL_INTERP_HERMITE);
            if (got != ref) bad++;
        }
        printf("    mismatches over 4000 samples: %d\n", bad);
        if (bad) { printf("    FAIL unity path not exact dl_read(base+0.5W)\n"); fails++; }
    }

    /* ---- 5. Detune stability: rho=1.003, level constant through wraps ---- */
    {
        printf("  detune stability (rho=1.003):\n");
        run_ps(outb, N, F_IN, 1.003f, W);
        float e = rms(outb, A, A + 4000), l = rms(outb, N - 4000, N);
        float ratio = l / (e + 1e-9f);
        printf("    rms early=%.4f late=%.4f  ratio=%.3f\n", e, l, ratio);
        if (!(ratio > 0.8f && ratio < 1.25f)) { printf("    FAIL level drifts (ratio %.3f)\n", ratio); fails++; }
    }

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
