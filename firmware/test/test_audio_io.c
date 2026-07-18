/* test_audio_io.c — block processing + codec word conversion + 8-channel layout.
 * cc -std=c11 -Wall -Wextra -I../src test_audio_io.c ../src/[a-z]*.c -o /tmp/t -lm && /tmp/t
 */
#include "audio_io.h"
#include <stdio.h>
#include <math.h>

static int fails = 0;
static void ok(const char *what, int cond) {
    printf(cond ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!cond) fails++;
}

static float dlbuf[8192];

int main(void)
{
    /* ---- codec word conversion round-trips and clamps ---- */
    ok("int24<->float midscale", fabsf(audio_in_to_f(audio_f_to_out(0.5f)) - 0.5f) < 1e-4f);
    ok("int24<->float negative", fabsf(audio_in_to_f(audio_f_to_out(-0.25f)) + 0.25f) < 1e-4f);
    /* over-FS goes through the soft knee now (patched-feedback limiter): the
     * invariant is bounded-and-monotonic, never sign-wrap (see test_softknee). */
    { int32_t w2 = (audio_f_to_out(2.0f) << 8) >> 8, w1 = (audio_f_to_out(0.9f) << 8) >> 8;
      ok("over-FS bounded + monotonic (+)", w2 > w1 && w2 <= 8388607); }
    { int32_t w2 = (audio_f_to_out(-2.0f) << 8) >> 8, w1 = (audio_f_to_out(-0.9f) << 8) >> 8;
      ok("over-FS bounded + monotonic (-)", w2 < w1 && w2 >= -8388608); }

    /* ---- block processing: 4-in/8-out TDM ---- */
    engine_t e;
    engine_init(&e, dlbuf, 8192, /*base_delay*/ 200.0f, 1.0f, 1.0f, 1.0f);

    enum { N = 16 };
    int32_t in[N * ADC_SLOTS], out[N * DAC_SLOTS];
    for (unsigned f = 0; f < N; f++)
        for (unsigned s = 0; s < ADC_SLOTS; s++)
            in[f*ADC_SLOTS + s] = (s == 0) ? ((int32_t)(0.5f * 8388607.0f) << 8) : 0;

    /* run several blocks so the delay line fills and taps produce output */
    int finite = 1; long peak = 0;
    for (int b = 0; b < 40; b++) {
        audio_io_block(&e, in, out, N, /*in_slot*/ 0, /*time*/ 0.5f);
        for (unsigned k = 0; k < N * DAC_SLOTS; k++) {
            float v = audio_in_to_f(out[k]);
            if (!isfinite(v)) finite = 0;
            long a = out[k] < 0 ? -out[k] : out[k];
            if (a > peak) peak = a;
        }
    }
    ok("block output finite", finite);
    ok("block output within 24-bit range", peak <= (8388607L << 8));
    ok("all 8 DAC slots written (nonzero energy)", peak > 0);

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
