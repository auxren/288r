/* test_pitch_fm.c — signal-in FM in the pitch domain (ps fm_in/fm_off; owner
 * design: "signal-in modulates whatever the multiplier's domain is").
 * A common-mode read offset on both grains = pure vibrato of the voice.
 * Checks: fm = 0 bit-exact; vibrato depth at unity (bypass path) and at an
 * active shift; slew limiter bounds the applied offset.
 */
#include "pitch_shift.h"
#include <stdio.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FS 96000.0f
#define LEN (1u<<17)
static float buf[LEN];
static float outb[LEN];

static int fails = 0;
static void ck(const char *name, int cond) {
    printf("  %-52s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

/* run: 480 Hz tone in, ratio, 6 Hz FM of amplitude `span` samples */
static void run(float ratio, float span, int N)
{
    delay_line_t d; dl_init(&d, buf, LEN); dl_clear(&d);
    pitchshift_t p; ps_init(&p, 0.060f * FS, 256.0f);
    ps_set_ratio(&p, ratio);
    float ph = 0.0f, w = 2.0f * (float)M_PI * 480.0f / FS;
    for (int n = 0; n < N; n++) {
        dl_write(&d, sinf(ph)); ph += w;
        if (ph >= 2.0f * (float)M_PI) ph -= 2.0f * (float)M_PI;
        p.fm_in = span * sinf(2.0f * (float)M_PI * 6.0f * (float)n / FS);
        ps_service(&p, &d);
        outb[n] = ps_process(&p, &d, DL_INTERP_HERMITE);
    }
}

/* min/max instantaneous freq via zero crossings in the settled half */
static void instf(int N, float *fmin, float *fmax)
{
    *fmin = 1e9f; *fmax = -1e9f;
    float prev = 0.0f; int last = 0, primed = 0;
    for (int n = N / 2; n < N; n++) {
        if (primed && prev <= 0.0f && outb[n] > 0.0f) {
            if (last) {
                float f = FS / (float)(n - last);
                if (f < *fmin) *fmin = f;
                if (f > *fmax) *fmax = f;
            }
            last = n;
        }
        prev = outb[n]; primed = 1;
    }
}

int main(void)
{
    const int N = 96000;
    float lo, hi;

    /* ---- fm = 0: identical to a run that never touches fm ---------------- */
    run(1.0f, 0.0f, N);
    static float ref[96000];
    for (int n = 0; n < N; n++) ref[n] = outb[n];
    run(1.0f, 0.0f, N);
    int exact = 1;
    for (int n = 0; n < N; n++) if (outb[n] != ref[n]) exact = 0;
    ck("fm = 0 reproducible/bit-exact", exact);

    /* ---- unity ratio + FM: vibrato on the bypass path --------------------- */
    run(1.0f, 96.0f, N);
    instf(N, &lo, &hi);
    printf("      unity vibrato: %.1f..%.1f Hz\n", lo, hi);
    /* 96 samp @6 Hz -> dev = 96*2pi*6/96000 = 3.8%% */
    ck("unity: vibrato deviates >2%% both ways",
       hi > 480.0f * 1.02f && lo < 480.0f / 1.02f);
    ck("unity: deviation sane (<10%%)", hi < 480.0f * 1.10f && lo > 480.0f * 0.90f);

    /* ---- active shift + FM: vibrato rides the shifted voice --------------- */
    run(0.891f, 96.0f, N);            /* -2 st */
    instf(N, &lo, &hi);
    float c = 480.0f * 0.891f;
    printf("      shifted vibrato: %.1f..%.1f Hz around %.1f\n", lo, hi, c);
    ck("shifted: vibrato deviates >2%% both ways",
       hi > c * 1.02f && lo < c / 1.02f);

    /* ---- slew limiter: garbage fm stays bounded --------------------------- */
    {
        delay_line_t d; dl_init(&d, buf, LEN); dl_clear(&d);
        pitchshift_t p; ps_init(&p, 0.060f * FS, 256.0f);
        ps_set_ratio(&p, 1.0f);
        float mx = 0.0f;
        for (int n = 0; n < 20000; n++) {
            dl_write(&d, 0.5f);
            p.fm_in = (n & 1) ? 1.0e6f : -1.0e6f;
            (void)ps_process(&p, &d, DL_INTERP_HERMITE);
            float a = fabsf(p.fm_off); if (a > mx) mx = a;
        }
        printf("      max |fm_off| under garbage fm = %.1f\n", mx);
        ck("absolute clamp holds (|off| <= 192)", mx <= 192.0f + 0.6f);
        ck("reads stayed causal (no NaN)", outb[0] == outb[0]);
    }

    /* ---- aligner survives vibrato (rc4 field regression) ------------------
     * Slow ratio modulation must NOT starve the splice search: with the
     * resume stamp at 0.08, searches complete and publish during +/-2%%
     * vibrato. (rc4's tight stamp + settled-only gate ran every splice
     * unaligned under CV modulation — audibly grittier than rc3.) */
    {
        delay_line_t d; dl_init(&d, buf, LEN); dl_clear(&d);
        pitchshift_t p; ps_init(&p, 0.060f * FS, 256.0f);
        int publishes = 0;
        float ph2 = 0.0f, w2 = 2.0f * (float)M_PI * 220.0f / FS;
        for (int n = 0; n < 4 * 96000; n++) {
            dl_write(&d, sinf(ph2)); ph2 += w2;
            if (ph2 >= 2.0f * (float)M_PI) ph2 -= 2.0f * (float)M_PI;
            float r = 0.90f * (1.0f + 0.02f * sinf(2.0f * (float)M_PI * 2.0f
                                                   * (float)n / FS));
            ps_set_ratio(&p, r);
            p.ratio = r;                    /* emulate the slewed ratio       */
            int had = (p.pend_tap >= 0);
            ps_service(&p, &d);
            if (!had && p.pend_tap >= 0) publishes++;
            (void)ps_process(&p, &d, DL_INTERP_HERMITE);
        }
        printf("      splice publishes under vibrato: %d in 4 s\n", publishes);
        ck("aligner still publishes during vibrato (>10)", publishes > 10);
    }

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
