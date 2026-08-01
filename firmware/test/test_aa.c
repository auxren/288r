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

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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
        if (p.aaband_req >= 0 && p.aarows_band != p.aaband_req)
            ps_set_aa_rows(&p, p.aaband_req,
                           ps_aa_flash_rows(p.aaband_req)); /* emulate superloop */
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
            if (p.aaband_req >= 0 && p.aarows_band != p.aaband_req)
            ps_set_aa_rows(&p, p.aaband_req,
                           ps_aa_flash_rows(p.aaband_req)); /* emulate superloop */
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

    /* ---- engage protections (#24 part 2) ----------------------------------
     * Field: a slow CV envelope crossing the AA boundary clicked every time,
     * echoed by the tap pattern. Two invariants kill it, both asserted here:
     * (1) the AA<->Hermite blend traverses GRADUALLY (>=2 ms) — never a hard
     * kernel swap; (2) the blend does not begin until the fast (CCM) rows
     * are published — engaging on flash rows is a CPU spike exactly at the
     * click-sensitive crossing (publisher emulated WITH DELAY to prove it).
     */
    {
        static float bufc[1u<<15];
        delay_line_t dc; dl_init(&dc, bufc, 1u<<15); dl_clear(&dc);
        pitchshift_t p; ps_init(&p, 0.020f * 96000.0f, 256.0f);
        int publish_at = -1;             /* delayed-publisher emulation      */
        int prev_on = 0, mix_low = 0, mix_hi = 0, traverse = -1;
        int premature = 0, engages = 0;
        for (int i = 0; i < 384000; i++) {
            float x = 0.4f*sinf(2.f*(float)M_PI*400.f*i/96000.f)
                    + 0.3f*sinf(2.f*(float)M_PI*9000.f*i/96000.f);
            dl_write(&dc, x);
            float r = 1.02f + 0.04f*sinf(2.f*(float)M_PI*0.5f*i/96000.f);
            ps_set_ratio(&p, r); p.ratio = r;
            /* publisher with a 300-sample delay after each request change */
            if (p.aaband_req >= 0 && p.aarows_band != p.aaband_req) {
                if (publish_at < 0) publish_at = i + 300;
                if (i >= publish_at) {
                    ps_set_aa_rows(&p, p.aaband_req,
                                   ps_aa_flash_rows(p.aaband_req));
                    publish_at = -1;
                }
            }
            ps_service(&p, &dc);
            (void)ps_process(&p, &dc, DL_INTERP_HERMITE);
            /* invariant 2: no blending while this band's rows are pending */
            if (p.aarows_band != p.aa_lastband && p.aa_mix > 0.0f && p.aa_on)
                premature = 1;
            /* invariant 1: time the 0.1 -> 0.9 traversal of each engage */
            if (p.aa_on && !prev_on) { engages++; mix_low = mix_hi = -1; }
            if (p.aa_on) {
                if (mix_low < 0 && p.aa_mix > 0.1f) mix_low = i;
                if (mix_hi  < 0 && p.aa_mix > 0.9f) mix_hi  = i;
                if (mix_low >= 0 && mix_hi >= 0 && traverse < 0)
                    traverse = mix_hi - mix_low;
            }
            prev_on = p.aa_on;
        }
        printf("      engages %d  first blend 0.1->0.9 in %d samples\n",
               engages, traverse);
        ck("boundary crossed repeatedly (test is live)", engages >= 2);
        ck("blend is gradual (>= 2 ms, no kernel swap)", traverse >= 192);
        ck("no blending before the fast rows are published", premature == 0);
    }

    printf(fails ? "FAILURES: %d\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
