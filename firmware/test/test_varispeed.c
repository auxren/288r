/* test_varispeed.c — looper tape-motor playback (#9, see engine.c varispeed).
 *
 * A loop captured at multiplier M and played back at multiplier m must advance
 * its head at rate M/m: pitch and rate move together (288v-confirmed stock
 * behavior). Verified by frequency-measuring a recirculated sine at several
 * multiplier settings, plus: rate exactly 1.0 at the capture multiplier, no
 * repitch when varispeed is off, and clamping at the 0.25/4.0 rails.
 */
#include "engine.h"
#include <stdio.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int fails = 0;
static void ck(const char *name, int cond) {
    printf("  %-46s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

#define LEN 48000
static float buf[LEN];

/* count sign changes of tap-0's channel over n samples at a fixed control */
static float measure_freq(engine_t *e, float raw01, int settle, int n)
{
    float chan[NUM_TAPS];
    for (int i = 0; i < settle; i++) engine_process_multi(e, 0.0f, raw01, chan);
    int crossings = 0; float prev = 0.0f; int primed = 0;
    for (int i = 0; i < n; i++) {
        engine_process_multi(e, 0.0f, raw01, chan);
        float s = chan[0];
        if (primed && ((s >= 0.0f) != (prev >= 0.0f))) crossings++;
        prev = s; primed = 1;
    }
    return (float)crossings / (2.0f * (float)n);   /* cycles per sample */
}

int main(void)
{
    /* multiplier range 0.4..1.6 like the panel; the taper is LINEAR
     * (time_control.c map_taper): mult m sits at raw = (m - 0.4)/1.2. */
    const float RAW_1_0 = (1.0f - 0.4f) / 1.2f;
    const float RAW_0_5 = (0.5f - 0.4f) / 1.2f;

    engine_t e;
    engine_init(&e, buf, LEN, /*base*/ 2000.0f, 0.4f, 1.6f, /*slew*/ 0.02f);

    /* record a 100-sample-period sine at mult 1.0 (settle the slew first) */
    float chan[NUM_TAPS];
    for (int i = 0; i < 20000; i++) {
        float x = 0.5f * (float)sin(2.0 * M_PI * (double)i / 100.0);
        engine_process_multi(&e, x, RAW_1_0, chan);
    }
    engine_recirc_window(&e, 8000);
    ck("capture ref = settled multiplier (~1.0)",
       fabsf(e.lp_mult_ref - 1.0f) < 0.02f);

    /* varispeed OFF: loop plays at the recorded frequency at any multiplier */
    e.varispeed = 0;
    float f_off = measure_freq(&e, RAW_0_5, 4000, 8000);
    ck("varispeed off: no repitch (f = 1/100)", fabsf(f_off - 0.01f) < 0.0008f);

    /* varispeed ON at the capture multiplier: rate 1, unchanged frequency */
    e.varispeed = 1;
    float f_unity = measure_freq(&e, RAW_1_0, 4000, 8000);
    ck("rate 1.0 at capture multiplier", fabsf(f_unity - 0.01f) < 0.0008f);
    ck("lp_rate telemetry ~1.0", fabsf(e.lp_rate - 1.0f) < 0.02f);

    /* multiplier 0.5 -> rate 2.0: frequency doubles (tape motor sped up) */
    float f_double = measure_freq(&e, RAW_0_5, 6000, 8000);
    ck("mult 0.5 -> frequency x2", fabsf(f_double - 0.02f) < 0.0015f);
    ck("lp_rate telemetry ~2.0", fabsf(e.lp_rate - 2.0f) < 0.05f);

    /* multiplier 1.6 -> rate 1/1.6 = 0.625: pitched down */
    float f_down = measure_freq(&e, 1.0f, 6000, 8000);
    ck("mult 1.6 -> frequency x0.625", fabsf(f_down - 0.00625f) < 0.0008f);

    /* rate clamps: a capture ref of 4.0 against mult 0.4 wants x10 -> 4.0 */
    e.lp_mult_ref = 4.0f;
    measure_freq(&e, 0.0f, 6000, 2000);
    ck("rate clamped at 4.0", e.lp_rate <= 4.0f + 1e-6f && e.lp_rate > 3.9f);
    e.lp_mult_ref = 0.1f;
    measure_freq(&e, 1.0f, 6000, 2000);
    ck("rate clamped at 0.25", e.lp_rate >= 0.25f - 1e-6f && e.lp_rate < 0.26f);

    /* ---- tap freeze: knob is MOTOR-ONLY while a loop plays ----------------
     * (stock tape-head model; live respacing during varispeed double-applied
     * the knob and aliased — field: 'zippers then corrects'.) */
    {
        engine_t e2; static float b2[96000];
        engine_init(&e2, b2, 96000u, 2000.0f, 0.4f, 1.6f, 0.02f);
        float ch2[NUM_TAPS];
        for (int i = 0; i < 40000; i++) engine_process_multi(&e2, 0.2f, 0.5f, ch2);
        e2.varispeed = 1;
        engine_recirc_window(&e2, 8000u);
        for (int i = 0; i < 20000; i++) engine_process_multi(&e2, 0.0f, 0.5f, ch2);
        float d_before = taps_delay(&e2.taps, 7);
        for (int i = 0; i < 40000; i++) engine_process_multi(&e2, 0.0f, 0.9f, ch2);
        float d_after = taps_delay(&e2.taps, 7);
        printf("      loop: tap7 %.1f -> %.1f under a knob sweep\n", d_before, d_after);
        ck("loop playback: taps frozen under the knob", 
           fabsf(d_after - d_before) < 1.0f);
        ck("loop playback: rate follows the knob",
           e2.lp_rate < 0.85f);
        engine_write(&e2);
        for (int i = 0; i < 60000; i++) engine_process_multi(&e2, 0.2f, 0.9f, ch2);
        float d_live = taps_delay(&e2.taps, 7);
        printf("      live: tap7 -> %.1f after returning to write\n", d_live);
        ck("live delay: taps respace again", d_live > d_before * 1.2f);
    }

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
