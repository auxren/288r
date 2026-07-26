/* test_time_fm.c — signal-in delay-time FM (engine.time_fm; owner feature
 * 2026-07-25). The FM term scales tap distances per sample AFTER the control
 * slews. Checks: off = bit-exact passthrough of the unmodulated engine;
 * on = the delayed tone is actually frequency-modulated (inst-freq swing
 * tracks the modulator); bounded output; the +/-0.25 safety clamp holds.
 */
#include "engine.h"
#include <stdio.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int fails = 0;
static void ck(const char *name, int cond) {
    printf("  %-52s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

#define LEN 48000u
static float bufA[LEN], bufB[LEN];

int main(void)
{
    /* two identical engines, one with FM applied */
    engine_t A, B;
    engine_init(&A, bufA, LEN, 2000.0f, 0.4f, 1.6f, 0.02f);
    engine_init(&B, bufB, LEN, 2000.0f, 0.4f, 1.6f, 0.02f);
    float chA[NUM_TAPS], chB[NUM_TAPS];

    /* ---- FM = 0: bit-exact identity with the plain engine ---------------- */
    int exact = 1;
    for (int i = 0; i < 20000; i++) {
        float x = 0.5f * (float)sin(2.0 * M_PI * i / 200.0);
        A.time_fm = 0.0f;
        float a = engine_process_multi(&A, x, 0.5f, chA);
        float b = engine_process_multi(&B, x, 0.5f, chB);
        if (a != b || chA[0] != chB[0]) exact = 0;
    }
    ck("fm = 0 is bit-exact vs plain engine", exact);

    /* ---- FM on: the delayed tone is frequency-modulated ------------------ */
    /* 5 Hz modulator, +/-0.08 span; measure the inst-freq swing of tap 0 */
    float prevph = 0.0f; int primed = 0;
    float fmax = -1e9f, fmin = 1e9f;
    /* simple zero-crossing period tracker over the modulated output */
    int last_zc = 0; int nzc = 0;
    for (int i = 0; i < 96000; i++) {
        float x = 0.5f * (float)sin(2.0 * M_PI * i / 200.0);
        A.time_fm = 0.08f * (float)sin(2.0 * M_PI * 5.0 * i / 96000.0);
        engine_process_multi(&A, x, 0.5f, chA);
        float s = chA[7];   /* deepest tap: ~8x the deviation of tap 0 */
        if (i > 48000) {                       /* settled region */
            if (primed && prevph <= 0.0f && s > 0.0f) {
                if (last_zc) {
                    float period = (float)(i - last_zc);
                    float f = 96000.0f / period;
                    if (f > fmax) fmax = f;
                    if (f < fmin) fmin = f;
                    nzc++;
                }
                last_zc = i;
            }
            prevph = s; primed = 1;
        }
    }
    /* carrier 480 Hz; delay-FM at 5 Hz/0.08 span sweeps the read position:
     * inst freq must deviate by well over 1% both ways, and stay sane */
    printf("      inst freq range %.1f..%.1f Hz over %d cycles\n", fmin, fmax, nzc);
    /* tap 7 = 2000 samples: expected ~+/-5%% deviation at m=0.08, 5 Hz */
    ck("FM deviates the delayed tone (>2%% both ways)",
       fmax > 480.0f * 1.02f && fmin < 480.0f / 1.02f);
    ck("deviation sane (<20%%)", fmax < 480.0f * 1.2f && fmin > 480.0f * 0.8f);

    /* ---- safety clamp: absurd fm values stay bounded ---------------------- */
    float mx = 0.0f;
    for (int i = 0; i < 20000; i++) {
        float x = 0.5f * (float)sin(2.0 * M_PI * i / 200.0);
        A.time_fm = (i & 1) ? 5.0f : -5.0f;    /* garbage input */
        engine_process_multi(&A, x, 0.5f, chA);
        float a = fabsf(chA[0]); if (a > mx) mx = a;
    }
    printf("      max |out| under garbage fm = %.3f\n", mx);
    ck("clamped: output bounded under garbage fm", mx < 1.5f);

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
