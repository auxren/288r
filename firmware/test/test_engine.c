/* test_engine.c — end-to-end host test of the integrated 288r engine.
 * cc -std=c11 -Wall -Wextra -I../src test_engine.c ../src/[a-z]*.c -o /tmp/t -lm && /tmp/t
 */
#include "engine.h"
#include <stdio.h>
#include <math.h>

static int fails = 0;
static void ok(const char *what, int cond) {
    printf(cond ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!cond) fails++;
}

static float buf[4096];

int main(void)
{
    engine_t e;

    /* ---- 1. stability under a full TIME sweep with signal (chorus use) ---- */
    engine_init(&e, buf, 4096, /*base_delay*/ 300.0f, /*lo*/ 0.5f, /*hi*/ 4.0f, /*slew*/ 0.2f);
    int finite = 1; float peak = 0.0f;
    for (int k = 0; k < 20000; k++) {
        float in  = 0.5f * sinf((float)k * 0.05f);
        float raw = 0.5f + 0.5f * sinf((float)k * 0.001f);   /* sweep TIME */
        float out = engine_process(&e, in, raw);
        if (!isfinite(out)) finite = 0;
        float a = fabsf(out); if (a > peak) peak = a;
    }
    ok("time-swept output stays finite", finite);
    ok("time-swept output bounded (<4)", peak < 4.0f);

    /* ---- 2. an impulse comes out delayed ---- */
    engine_init(&e, buf, 4096, 300.0f, 1.0f, 1.0f, 1.0f);   /* instant slew, mult=1 */
    (void)engine_process(&e, 1.0f, 0.5f);                    /* impulse in */
    float energy = 0.0f;
    for (int k = 0; k < 400; k++) energy += fabsf(engine_process(&e, 0.0f, 0.5f));
    ok("impulse produces delayed output", energy > 0.01f);

    /* ---- 3. RECIRC loops recorded content (periodic, non-decaying) ---- */
    engine_init(&e, buf, 4096, 20.0f, 1.0f, 1.0f, 1.0f);
    const int P = 300;
    for (int k = 0; k < P; k++) engine_process(&e, sinf((float)k * 0.3f), 0.5f); /* record */
    engine_recirc(&e);                                        /* loop = [0, head=P] */
    float first[P], second[P];
    for (int k = 0; k < P; k++) first[k]  = engine_process(&e, 0.0f, 0.5f);
    for (int k = 0; k < P; k++) second[k] = engine_process(&e, 0.0f, 0.5f);
    float diff = 0.0f, ener = 0.0f;
    for (int k = 0; k < P; k++) { diff += fabsf(first[k] - second[k]); ener += fabsf(first[k]); }
    ok("recirc output is periodic (loops)", diff / (ener + 1e-6f) < 0.05f);
    ok("recirc content non-zero (held)",    ener > 0.1f);

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
