/* test_precision.c — the SDRAM-size float-precision regression.
 *
 * A float32 read position has a 24-bit mantissa: at a head position near 2M
 * samples (one ~20 s SDRAM bank @96 kHz) its ULP is 0.25 in [2^21,2^22) and
 * 0.125 in [2^20,2^21) — so dl_read()'s `(float)wpos - delay` quantizes the
 * interpolation FRACTION to coarse steps whenever the head is deep in the
 * buffer, for ANY tap length (even a 10-sample chorus tap). This suite:
 *
 *   1. demonstrates the artifact on the float API at wpos ~2.5M (documented,
 *      expected-coarse — if float math ever becomes exact here, great, but the
 *      test tells us);
 *   2. proves dl_read_frac / ab_read_frac stay exact at the same position;
 *   3. proves frac == float API on a small buffer (where float IS exact),
 *      linear + Hermite, straight + loop reads;
 *   4. proves the Q32.32 taps slew converges at multi-million-sample delays
 *      where the old float one-pole stalled below its ULP.
 *
 * cc -std=c11 -Wall -Wextra -I../src test_precision.c ../src/delay_line.c \
 *    ../src/audio_buffer.c ../src/taps.c -lm -o /tmp/t && /tmp/t
 */
#include "delay_line.h"
#include "audio_buffer.h"
#include "taps.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static int fails = 0;
static void check(const char *what, int ok)
{
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}

#define BIG_LEN  3000000u          /* > 2^21: float index ULP = 0.25 up here */
#define BIG_POS  2500001u          /* park the head deep in the buffer      */

int main(void)
{
    /* ---------- 1+2: float API quantizes the fraction at 2.5M; frac API exact.
     * Buffer holds 0,1,0,1,... so a linear read at fraction f between an even
     * and odd index must return exactly f (or 1-f) — any fraction error shows
     * up 1:1 in the output. */
    float *big = malloc(BIG_LEN * sizeof(float));
    if (!big) { printf("  SKIP (malloc)\n"); return 0; }
    for (uint32_t i = 0; i < BIG_LEN; i++) big[i] = (float)(i & 1u);

    delay_line_t d;
    dl_init(&d, big, BIG_LEN);
    d.wpos = BIG_POS;

    /* tap 10.3 samples back: straddles indices wpos-11 (even) and wpos-10 (odd),
     * index-space fraction 0.7 -> exact linear value 0.7 */
    const float want = 0.7f;
    float got_float = dl_read(&d, 10.3f, DL_INTERP_LINEAR);
    float got_frac  = dl_read_frac(&d, 10, 0.3f, DL_INTERP_LINEAR);

    printf("  head=%u  float API=%.6f  frac API=%.6f  exact=%.6f\n",
           BIG_POS, (double)got_float, (double)got_frac, (double)want);
    check("float API quantizes fraction at 2.5M (documents the artifact)",
          fabsf(got_float - want) > 0.02f);
    check("frac API exact at 2.5M (linear)", fabsf(got_frac - want) < 1e-5f);

    /* Hermite on the same alternating pattern is symmetric around 0.5 at f=0.7;
     * just require it lands within the overshoot-free midband, exactly equal to
     * a small-buffer reference computed below. */
    float big_herm = dl_read_frac(&d, 10, 0.3f, DL_INTERP_HERMITE);

    /* ---------- 3: frac == float API where float is exact (small buffer) */
    {
        enum { N = 64 };
        float sb[N];
        delay_line_t s;
        dl_init(&s, sb, N);
        for (int i = 0; i < 100; i++) dl_write(&s, sinf(0.37f * (float)i));

        int okl = 1, okh = 1;
        for (int k = 2; k < 40; k++) {
            float f = (float)(k % 10) * 0.1f;
            float a = dl_read(&s, (float)k + f, DL_INTERP_LINEAR);
            float b = dl_read_frac(&s, (uint32_t)k, f, DL_INTERP_LINEAR);
            if (fabsf(a - b) > 1e-5f) okl = 0;
            a = dl_read(&s, (float)k + f, DL_INTERP_HERMITE);
            b = dl_read_frac(&s, (uint32_t)k, f, DL_INTERP_HERMITE);
            if (fabsf(a - b) > 1e-5f) okh = 0;
        }
        check("frac == float API, small buffer, linear",  okl);
        check("frac == float API, small buffer, Hermite", okh);

        /* loop-window variant against the float loop read */
        int okloop = 1;
        for (int k = 2; k < 20; k++) {
            float f = (float)(k % 4) * 0.25f;
            float a = dl_read_loop(&s, (float)k + f, 8, 56, DL_INTERP_LINEAR);
            float b = dl_read_loop_frac(&s, (uint32_t)k, f, 8, 56, DL_INTERP_LINEAR);
            if (fabsf(a - b) > 1e-5f) okloop = 0;
        }
        check("loop frac == loop float API, small buffer", okloop);

        /* Hermite reference for the big-buffer alternating pattern: same local
         * sample neighbourhood (1,0,1,0 around f=0.3 in delay space) */
        float ref[8] = {0,1,0,1,0,1,0,1};
        delay_line_t r;
        dl_init(&r, ref, 8);
        r.wpos = 5;                                    /* index parity matches */
        float small_herm = dl_read_frac(&r, 2, 0.3f, DL_INTERP_HERMITE);
        check("frac API Hermite identical at 2.5M vs small buffer",
              fabsf(big_herm - small_herm) < 1e-5f);
    }

    /* ---------- 2b: audio_buffer (I32) frac read exact at 2.5M */
    {
        int32_t *ibuf = malloc(BIG_LEN * sizeof(int32_t));
        if (ibuf) {
            audio_buffer_t ab;
            ab_init(&ab, ibuf, BIG_LEN * 4u, AB_FMT_I32);
            for (uint32_t i = BIG_POS - 16; i <= BIG_POS + 4; i++)
                ((int32_t *)ab.buf)[i % BIG_LEN] = (i & 1u) ? 1073741824 : 0;
            ab.wpos = BIG_POS;
            float g = ab_read_frac(&ab, 10, 0.3f, DL_INTERP_LINEAR);
            check("ab frac API exact at 2.5M (I32, linear)", fabsf(g - want) < 1e-5f);
            free(ibuf);
        }
    }
    free(big);

    /* ---------- 4: Q32.32 slew converges where a float one-pole stalls.
     * Target moves 2,000,000.0 -> 2,000,000.5. Old float cur: increment
     * 0.0005*... < ULP(2M)=0.25 rounds to nothing -> stalls forever at .0.
     * Q32.32 must walk the half sample and settle. */
    {
        taps_t t;
        taps_init(&t, 2000000.0f, 1.0f);       /* slew=1: land on target now  */
        taps_update(&t, 1.0f);                 /* tap7 = base (phase 160/160) */
        t.slew = 0.001f;                       /* then a slow musical slew    */
        taps_set_base_delay(&t, 2000000.5f);   /* +0.5 samples (representable)*/
        for (int n = 0; n < 20000; n++) taps_update(&t, 1.0f);

        uint32_t di; float df;
        taps_delay_frac(&t, 7, &di, &df);
        printf("  slew @2M: d_int=%u  d_frac=%.6f (want 2000000 + 0.5)\n",
               di, (double)df);
        check("Q32.32 slew converges at 2M-sample delay",
              di == 2000000u && fabsf(df - 0.5f) < 0.01f);

        /* and the trajectory actually moved through sub-ULP steps: after ONE
         * update from the start, the position must have advanced by less than
         * a float-ULP-at-2M (0.25) but more than zero. */
        taps_init(&t, 2000000.0f, 1.0f);
        taps_update(&t, 1.0f);
        t.slew = 0.001f;
        taps_set_base_delay(&t, 2000000.5f);
        int64_t before = t.cur_q[7];
        taps_update(&t, 1.0f);
        int64_t step = t.cur_q[7] - before;
        double step_samples = (double)step / 4294967296.0;
        printf("  first slew step = %.9f samples (float ULP here = 0.25)\n",
               step_samples);
        check("slew resolves sub-ULP steps (smooth, not stair-stepped)",
              step_samples > 0.0 && step_samples < 0.25);
    }

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
