/* test_splice.c — loop-seam crossfade (dl_loop_splice; bench + field #9: every
 * wrap clicked because the window tail and head are unrelated content). The
 * splice rewrites the tail to glide into the content that leads into the loop
 * start, making the wrap continuous.
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

#define LEN 16384u
static float buf[LEN];

int main(void)
{
    /* ---- dl-level: seam becomes continuous ---- */
    delay_line_t d; dl_init(&d, buf, LEN);
    /* content: a ramp, so every sample is unique and blending is checkable */
    for (uint32_t i = 0; i < LEN; i++) buf[i] = (float)i;
    uint32_t start = 4000, end = 8000, fade = 100;
    dl_loop_splice(&d, start, end, fade);
    ck("tail end matches pre-start lead (continuous wrap)",
       fabsf(buf[end - 1] - (float)(start - 1)) < 1.0f);
    ck("fade start still ~original content",
       fabsf(buf[end - fade] - ((float)(end - fade) * 0.99f
             + (float)(start - fade) * 0.01f)) < 45.0f);
    {   /* monotonic blend: each tail sample between original and lead value */
        int ok = 1;
        for (uint32_t i = 0; i < fade; i++) {
            float v = buf[end - fade + i];
            float orig = (float)(end - fade + i), lead = (float)(start - fade + i);
            if (v > orig + 0.5f || v < lead - 0.5f) ok = 0;
        }
        ck("blend stays between original and lead", ok);
    }

    /* ---- degenerate windows are skipped, not corrupted ---- */
    for (uint32_t i = 0; i < LEN; i++) buf[i] = (float)i;
    dl_loop_splice(&d, 100, 250, 100);           /* window 150 <= 2*fade */
    ck("tiny window untouched", buf[249] == 249.0f);

    /* ---- engine-level: recirc_window applies the splice ---- */
    engine_t e;
    engine_init(&e, buf, LEN, 2000.0f, 0.4f, 1.6f, 0.02f);
    float chan[NUM_TAPS];
    /* sine whose period doesn't divide the window -> raw seam discontinuity */
    for (int i = 0; i < 12000; i++)
        engine_process_multi(&e, 0.9f * (float)sin(2.0 * M_PI * i / 700.0),
                             0.5f, chan);
    uint32_t head = e.dl.wpos;
    engine_recirc_window(&e, 4000);
    uint32_t ls = e.xport.loop_start;
    ck("engine splice: window tail meets the head content",
       fabsf(buf[(head + LEN - 1u) % LEN]
             - buf[(ls + LEN - 1u) % LEN]) < 0.02f);
    {   /* guard samples: stencil reads past the seam must see head content */
        int ok = 1;
        for (uint32_t i = 0; i < 4u; i++)
            if (buf[(head + i) % LEN] != buf[(ls + i) % LEN]) ok = 0;
        ck("guard samples mirror the head past the seam", ok);
    }

    /* ---- end-to-end: varispeed playback across the wrap is click-free ---- */
    {
        /* fractional rate (mult != capture) so reads walk through the seam
         * zone; measure the largest sample-to-sample output step across
         * several wraps and compare against the sine's own max slope */
        e.varispeed = 1;
        float prev = 0.0f, mx = 0.0f; int primed = 0;
        for (int i = 0; i < 30000; i++) {
            engine_process_multi(&e, 0.0f, 0.30f, chan);   /* rate ~ 1.45 */
            if (primed) {
                float st = fabsf(chan[0] - prev);
                if (st > mx) mx = st;
            }
            prev = chan[0]; primed = 1;
        }
        /* sine amp 0.9, period 700, rate<=1.6: max slope ~ 0.9*2pi/700*1.6 */
        ck("varispeed wrap: no step above analog slope", mx < 0.025f);
    }

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
