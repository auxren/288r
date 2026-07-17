/* test_audio_buffer.c — int16/int32 SDRAM storage layer (see src/audio_buffer.c).
 * Verifies the capacity win, round-trip fidelity per format, that the fractional
 * (Hermite) read matches the float delay_line kernel through int32 storage, the
 * vintage crush, clamping, and circular wrap. Built by `make test`.
 */
#include "audio_buffer.h"
#include "delay_line.h"
#include <stdio.h>
#include <math.h>

#define BYTES 16384u

static unsigned char store8[BYTES];      /* raw storage for the buffer under test */
static float        fbuf[BYTES / 4u];    /* float delay_line for the kernel compare */

static int fails = 0;
static void ck(const char *name, int cond) {
    printf("  %-46s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

int main(void)
{
    audio_buffer_t ab;

    /* ---- 1. capacity: same bytes, 2x samples for I16 vs I32 ---- */
    ab_init(&ab, store8, BYTES, AB_FMT_I16);
    uint32_t n16 = ab_capacity_samples(&ab);
    ab_init(&ab, store8, BYTES, AB_FMT_I32);
    uint32_t n32 = ab_capacity_samples(&ab);
    printf("  capacity: I16=%u samples, I32=%u samples\n", n16, n32);
    ck("I16 = bytes/2", n16 == BYTES / 2u);
    ck("I32 = bytes/4", n32 == BYTES / 4u);
    ck("I16 gives 2x the delay time", n16 == 2u * n32);

    /* ---- 2. round-trip fidelity at integer delays (f=0 -> exact fetch) ---- */
    const int N = 2000;
    for (int pass = 0; pass < 2; pass++) {
        ab_format_t fmt = pass ? AB_FMT_I32 : AB_FMT_I16;
        ab_init(&ab, store8, BYTES, fmt);
        ab_clear(&ab);
        float in[2048];
        for (int n = 0; n < N; n++) { in[n] = 0.9f * sinf(0.017f * n); ab_write(&ab, in[n]); }
        float emax = 0.0f;
        for (int d = 1; d < 500; d++) {
            float got = ab_read(&ab, (float)d, DL_INTERP_LINEAR);   /* integer delay */
            float e = fabsf(got - in[N - d]);
            if (e > emax) emax = e;
        }
        printf("    %s round-trip max err = %.2e\n", pass ? "I32" : "I16", emax);
        if (pass == 0) ck("I16 round-trip within 16-bit step", emax < 2e-5f);
        else           ck("I32 round-trip near-lossless (<1e-6)", emax < 1e-6f);
    }

    /* ---- 3. fractional Hermite read matches the float delay_line through I32 ---- */
    {
        ab_init(&ab, store8, BYTES, AB_FMT_I32);
        ab_clear(&ab);
        delay_line_t dl; dl_init(&dl, fbuf, BYTES / 4u); dl_clear(&dl);
        for (int n = 0; n < N; n++) { float x = 0.8f * sinf(0.05f * n); ab_write(&ab, x); dl_write(&dl, x); }
        float dmax = 0.0f;
        for (float d = 50.0f; d < 300.0f; d += 0.37f) {
            float a = ab_read(&ab, d, DL_INTERP_HERMITE);
            float b = dl_read(&dl, d, DL_INTERP_HERMITE);
            float e = fabsf(a - b);
            if (e > dmax) dmax = e;
        }
        printf("    I32 vs float delay_line (Hermite) max diff = %.2e\n", dmax);
        ck("I32 Hermite == float kernel (<1e-5)", dmax < 1e-5f);
    }

    /* ---- 4. noise floor: I16 quantization >> I32 ---- */
    {
        double s16 = 0.0, s32 = 0.0; int cnt = 0;
        for (int pass = 0; pass < 2; pass++) {
            ab_init(&ab, store8, BYTES, pass ? AB_FMT_I32 : AB_FMT_I16);
            ab_clear(&ab);
            float in[2048];
            for (int n = 0; n < N; n++) { in[n] = 0.7f * sinf(0.03f * n); ab_write(&ab, in[n]); }
            double s = 0.0; cnt = 0;
            for (int d = 1; d < 500; d++) {
                float e = ab_read(&ab, (float)d, DL_INTERP_LINEAR) - in[N - d];
                s += (double)e * e; cnt++;
            }
            if (pass) s32 = sqrt(s / cnt); else s16 = sqrt(s / cnt);
        }
        printf("    RMS noise: I16=%.2e  I32=%.2e\n", s16, s32);
        ck("I16 noise floor present (~16-bit)", s16 > 1e-6f && s16 < 1e-4f);
        ck("I32 noise floor >=50x lower", s32 < s16 / 50.0f);
    }

    /* ---- 5. vintage crush (8-bit) quantizes readback ---- */
    {
        ab_init(&ab, store8, BYTES, AB_FMT_I16);
        ab_set_vintage(&ab, 8);
        ab_clear(&ab);
        float in[2048];
        for (int n = 0; n < N; n++) { in[n] = 0.9f * sinf(0.02f * n); ab_write(&ab, in[n]); }
        float emax = 0.0f;
        for (int d = 1; d < 500; d++) {
            float e = fabsf(ab_read(&ab, (float)d, DL_INTERP_LINEAR) - in[N - d]);
            if (e > emax) emax = e;
        }
        printf("    8-bit vintage crush max err = %.4f (step ~0.0078)\n", emax);
        ck("8-bit crush error ~<= half a step", emax > 0.001f && emax < 0.006f);
    }

    /* ---- 6. clamp on overflow ---- */
    {
        ab_init(&ab, store8, BYTES, AB_FMT_I32);
        ab_clear(&ab);
        ab_write(&ab, 2.5f); ab_write(&ab, -3.0f);
        float hi = ab_read(&ab, 2.0f, DL_INTERP_LINEAR);   /* the +2.5 sample */
        float lo = ab_read(&ab, 1.0f, DL_INTERP_LINEAR);   /* the -3.0 sample */
        printf("    clamp: +2.5 -> %.4f, -3.0 -> %.4f\n", hi, lo);
        ck("positive overflow clamps to ~+1.0", fabsf(hi - 1.0f) < 1e-3f);
        ck("negative overflow clamps to ~-1.0", fabsf(lo + 1.0f) < 1e-3f);
    }

    /* ---- 7. circular wrap: write more than len, recent samples still correct ---- */
    {
        ab_init(&ab, store8, BYTES, AB_FMT_I16);
        uint32_t len = ab_capacity_samples(&ab);
        ab_clear(&ab);
        for (uint32_t n = 0; n < len + 137u; n++) ab_write(&ab, 0.5f * sinf(0.01f * (float)n));
        uint32_t last = len + 137u - 1u;
        float got = ab_read(&ab, 1.0f, DL_INTERP_LINEAR);
        float exp = 0.5f * sinf(0.01f * (float)last);
        printf("    wrap: last written read back err = %.2e\n", fabsf(got - exp));
        ck("read wraps past buffer end", fabsf(got - exp) < 2e-5f);
    }

    /* ---- 8. loop read/advance sanity ---- */
    {
        ab_init(&ab, store8, BYTES, AB_FMT_I32);
        ab_clear(&ab);
        for (int n = 0; n < 400; n++) ab_write(&ab, (float)(n % 50) / 50.0f);
        float a = ab_read_loop(&ab, 10.0f, 100, 300, DL_INTERP_LINEAR);
        uint32_t before = ab.wpos;
        for (int i = 0; i < 5; i++) ab_advance_loop(&ab, 100, 300);
        ck("loop read returns a finite sample", a >= -1.5f && a <= 1.5f);
        ck("advance_loop moves the head", ab.wpos == before + 5u);
    }

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
