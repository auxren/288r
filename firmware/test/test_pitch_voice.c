/* test_pitch_voice.c — the playable pitch voice: 1.2 V/oct CV map, ratio slew, and
 * integrated frequency tracking through the real delay_line + pitch_shift + fast_math.
 * Built by `make test`.
 */
#include "pitch_voice.h"
#include "delay_line.h"
#include <stdio.h>
#include <math.h>

#define FS      48000.0f
#define BUFLEN  8192
#define TWO_PI  6.28318530717959

static float buf[BUFLEN];
static float outb[16384];

static int fails = 0;
static void ck(const char *name, int cond) {
    printf("  %-44s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

/* Stream a sine at f_in through the voice at a fixed CV; return output in out[N]. */
static void run(float *out, int N, float f_in, float volts, float W, float slew)
{
    delay_line_t d; dl_init(&d, buf, BUFLEN); dl_clear(&d);
    pitch_voice_t v; pv_init(&v, W, 64.0f, slew); pv_set_cv(&v, volts);
    float ph = 0.0f, w = (float)(TWO_PI) * f_in / FS;
    for (int n = 0; n < N; n++) {
        dl_write(&d, sinf(ph));
        ph += w; if (ph >= (float)TWO_PI) ph -= (float)TWO_PI;
        out[n] = pv_process(&v, &d, DL_INTERP_HERMITE);
    }
}

static double goertzel_pow(const float *x, int a, int b, double f) {
    double w = TWO_PI * f / FS, c = 2.0 * cos(w), s0, s1 = 0.0, s2 = 0.0;
    for (int n = a; n < b; n++) { s0 = x[n] + c * s1 - s2; s2 = s1; s1 = s0; }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}
static double dom_freq(const float *x, int a, int b, double f0) {
    double best = f0, bp = -1.0;
    for (double f = f0 * 0.6; f <= f0 * 1.4; f += 0.25) {
        double p = goertzel_pow(x, a, b, f); if (p > bp) { bp = p; best = f; }
    }
    return best;
}

int main(void)
{
    pitch_voice_t v;

    /* ---- 1.2 V/oct CV -> ratio map ---- */
    pv_init(&v, 2000.0f, 64.0f, 1.0f);
    pv_set_cv(&v, 0.0f);   ck("0 V -> ratio 1.0",   fabsf(v.target - 1.0f)   < 1e-3f);
    pv_set_cv(&v, 1.2f);   ck("+1.2 V -> ratio 2.0 (+1 oct)", fabsf(v.target - 2.0f) < 2e-3f);
    pv_set_cv(&v, -1.2f);  ck("-1.2 V -> ratio 0.5 (-1 oct)", fabsf(v.target - 0.5f) < 1e-3f);
    pv_set_cv(&v, 0.6f);   ck("+0.6 V -> ratio ~1.414 (+half oct)", fabsf(v.target - 1.41421f) < 2e-3f);
    /* NOT 1 V/oct: 1.0 V must give 2^(1/1.2)=1.7818, not 2.0 */
    pv_set_cv(&v, 1.0f);   ck("+1.0 V -> 1.782 (confirms 1.2 V/oct, not 1)", fabsf(v.target - 1.78180f) < 3e-3f);

    /* ---- integrated frequency tracking (ratio settled: slew=1.0) ---- */
    const int N = 16384, A = 4096;
    const float W = 0.060f * FS;
    printf("  integrated tracking (440 Hz in, W=60ms):\n");
    run(outb, N, 440.0f, 1.2f, W, 1.0f);           /* +1 oct -> 880 */
    double up = dom_freq(outb, A, N, 880.0);
    printf("    +1.2V: got %.1f Hz (expect 880)\n", up);
    ck("+1 oct tracks 880 Hz (<5%)", fabs(up - 880.0) / 880.0 < 0.05);
    run(outb, N, 440.0f, -1.2f, W, 1.0f);          /* -1 oct -> 220 */
    double dn = dom_freq(outb, A, N, 220.0);
    printf("    -1.2V: got %.1f Hz (expect 220)\n", dn);
    ck("-1 oct tracks 220 Hz (<5%)", fabs(dn - 220.0) / 220.0 < 0.05);

    /* ---- ratio slew: glide toward target, no overshoot ---- */
    pv_init(&v, 2000.0f, 64.0f, 0.01f);
    pv_set_ratio(&v, 2.0f);
    delay_line_t d; dl_init(&d, buf, BUFLEN); dl_clear(&d);
    for (int n = 0; n < 200; n++) dl_write(&d, 0.0f);   /* silence, just advance ratio */
    float prev = v.ratio; int monotonic = 1;
    for (int n = 0; n < 3000; n++) {
        pv_process(&v, &d, DL_INTERP_HERMITE);
        if (v.ratio < prev - 1e-6f || v.ratio > 2.0f + 1e-4f) monotonic = 0;  /* rising, no overshoot */
        prev = v.ratio;
    }
    ck("ratio glides up, no overshoot", monotonic);
    ck("ratio converges to target", fabsf(v.ratio - 2.0f) < 0.01f);

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
