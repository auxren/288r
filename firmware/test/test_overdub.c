/* test_overdub.c — sound-on-sound (engine od_active/od_decay; owner feature).
 * While a loop plays with overdub engaged, every position the head visits
 * becomes old*decay + conditioned input, with ZOH across varispeed skips and
 * a soft ceiling. Idle = bit-exact passthrough.
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
static float buf[LEN];

int main(void)
{
    engine_t e;
    engine_init(&e, buf, LEN, 2000.0f, 0.4f, 1.6f, 0.02f);
    float chan[NUM_TAPS];

    /* record a marker level, capture a loop */
    for (int i = 0; i < 20000; i++) engine_process_multi(&e, 0.25f, 0.5f, chan);
    engine_recirc_window(&e, 8000);
    uint32_t ls = e.xport.loop_start;

    /* ---- idle: loop content untouched over several passes ----------------- */
    for (int i = 0; i < 20000; i++) engine_process_multi(&e, 0.9f, 0.5f, chan);
    ck("idle overdub: content untouched",
       fabsf(buf[(ls + 4000u) % LEN] - 0.25f) < 1e-6f);

    /* ---- one overdubbed pass: old*decay + input --------------------------- */
    e.od_active = 1;
    for (int i = 0; i < 8000; i++) engine_process_multi(&e, 0.10f, 0.5f, chan);
    e.od_active = 0;
    float expect = 0.25f * 0.95f + 0.10f;
    ck("one pass: old*decay + input", fabsf(buf[(ls + 4000u) % LEN] - expect) < 1e-3f);

    /* ---- release: content stable again ------------------------------------ */
    float held = buf[(ls + 4000u) % LEN];
    for (int i = 0; i < 16000; i++) engine_process_multi(&e, 0.9f, 0.5f, chan);
    ck("released: content stable", buf[(ls + 4000u) % LEN] == held);

    /* ---- varispeed rate 2: ZOH leaves no gaps ------------------------------ */
    e.varispeed = 1;
    e.lp_mult_ref = 2.0f * e.time.mult;      /* rate 2 at the current mult   */
    e.od_active = 1;
    for (int i = 0; i < 4200; i++) engine_process_multi(&e, 0.30f, 0.5f, chan);
    e.od_active = 0; e.varispeed = 0;
    int gaps = 0;
    for (uint32_t k = 0; k < 8000u; k++) {
        float v = buf[(ls + k) % LEN];
        /* every position must have been rewritten: old held value would
         * remain exactly `held`; a written one is held*0.95 + 0.30 or later */
        if (v == held) gaps++;
    }
    ck("varispeed overdub: no unwritten gaps", gaps == 0);

    /* ---- soft ceiling: sustained hot layering stays bounded --------------- */
    e.od_active = 1;
    for (int i = 0; i < 80000; i++) engine_process_multi(&e, 0.9f, 0.5f, chan);
    e.od_active = 0;
    float mx = 0.0f;
    for (uint32_t k = 0; k < 8000u; k++) {
        float a = fabsf(buf[(ls + k) % LEN]); if (a > mx) mx = a;
    }
    printf("      max |buffer| after 10 hot passes = %.3f\n", mx);
    ck("soft ceiling bounds accumulation (<2.05)", mx < 2.05f);

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
