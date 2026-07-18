/* test_am.c — grain-rate amplitude modulation of the pitch shifter.
 * Owner-reported artifact: audible AM on shifted material. Cause: a fixed
 * amplitude-complementary (sin^2) fade is flat only for COHERENT grains; on
 * uncorrelated grains it dips -3 dB at every fade midpoint (power adds, not
 * amplitude). The coherence-adaptive law blends toward power-complementary
 * weights as the measured splice NCC falls. This suite gates output-envelope
 * flatness on both material classes. Built by `make test`.
 */
#include "pitch_shift.h"
#include "delay_line.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

#define FS      48000.0f
#define BUFLEN  65536
#define BASE    64.0f

static float buf[BUFLEN];
static float outb[120000];

static int fails = 0;
static void ck(const char *name, int cond) {
    printf("  %-56s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

static unsigned rng = 0x2468ace1u;
static float frand(void) {           /* uniform [-1,1), deterministic */
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return ((float)(int32_t)rng) * (1.0f / 2147483648.0f);
}

/* run the shifter over N samples of the given source (0 = 330 Hz tone,
 * 1 = white noise), with the de-glitch service running as on hardware */
static void run(int N, float ratio, float W, int noise)
{
    delay_line_t d; dl_init(&d, buf, BUFLEN); dl_clear(&d);
    pitchshift_t p; ps_init(&p, W, BASE); ps_set_ratio(&p, ratio);
    float ph = 0.0f, w = 2.0f * 3.14159265f * 330.0f / FS;
    for (int n = 0; n < N; n++) {
        float s = noise ? 0.5f * frand() : sinf(ph);
        ph += w; if (ph > 6.2831853f) ph -= 6.2831853f;
        dl_write(&d, s);
        ps_service(&p, &d);
        outb[n] = ps_process(&p, &d, DL_INTERP_HERMITE);
    }
}

/* envelope ripple in dB: sliding-RMS (win samples) over the steady tail */
static double ripple_db(int N, int skip, int win)
{
    double mx = 0.0, mn = 1e30;
    for (int a = skip; a + win < N; a += win / 4) {
        double e = 0.0;
        for (int i = a; i < a + win; i++) e += (double)outb[i] * outb[i];
        e = sqrt(e / win);
        if (e > mx) mx = e;
        if (e < mn) mn = e;
    }
    return 20.0 * log10(mx / (mn + 1e-12));
}

int main(void)
{
    printf("test_am\n");
    const float W = 0.060f * FS;      /* 60 ms window, as on hardware */
    const int   N = 120000;           /* 2.5 s */

    /* coherent material: aligned splices -> amplitude-complementary is flat */
    run(N, 0.94f, W, 0);
    double r_tone = ripple_db(N, 20000, 480);
    ck("tone @ -1.07 st: envelope ripple < 1.5 dB", r_tone < 1.5);
    printf("      (tone ripple = %.2f dB)\n", r_tone);

    run(N, 0.76f, W, 0);
    double r_tone2 = ripple_db(N, 20000, 480);
    ck("tone @ -4.75 st: envelope ripple < 1.5 dB", r_tone2 < 1.5);
    printf("      (tone ripple = %.2f dB)\n", r_tone2);

    /* incoherent material: splices can't align white noise; the adaptive law
     * must fall back toward power-complementary or the envelope pumps -3 dB
     * at every fade. RMS window ~ one fade width. */
    run(N, 0.94f, W, 1);
    double r_noise = ripple_db(N, 20000, 1440);
    ck("noise @ -1.07 st: envelope ripple < 2.5 dB", r_noise < 2.5);
    printf("      (noise ripple = %.2f dB)\n", r_noise);

    printf(fails ? "FAILURES: %d\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
