/* test_declick.c — transport-transition clicks (field #27/#28/#29/#30/#31).
 *
 * Five separate field reports, one family: every transition into or out of
 * RECIRC clicked, "and the click propagates through the taps".
 *
 * Two distinct mechanisms, both covered here:
 *
 *  1. READ JUMP (heard immediately). Every engine_recirc_* entry teleports the
 *     head (`dl.wpos = start`) and window-maps the reads; engine_write() drops
 *     that mapping. Either way all 8 tap read positions jump to unrelated
 *     content in one sample — a step in every tap at once.
 *
 *  2. WRITE SEAM (recorded into the material, then heard on later passes —
 *     field #28: "clicks that are recorded into the next loop"). When writing
 *     resumes it lands mid-buffer on old content, so the buffer itself gets a
 *     step at the write position.
 *
 * The measure for (1) is the per-sample delta of the tap output against the
 * delta the test signal can legitimately produce; for (2) it is the delta of
 * the buffer contents across the resume point.
 */
#include "engine.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int fails = 0;
static void ck(const char *name, int cond) {
    printf("  %-52s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

#define FS      96000.0f
#define LEN     (1u << 18)
#define NTAPS   NUM_TAPS
static float buf[LEN];

/* A 220 Hz sine at 0.5 FS moves at most 2*pi*220/96000*0.5 = 0.0072 per sample.
 * Anything an order of magnitude past that is a discontinuity, not signal. */
/* The test signal must be INCOMMENSURATE with the loop windows below. A first
 * pass used 220 Hz with a 4800-sample window -- exactly 11.0 periods -- so every
 * head jump landed on matching phase and three of these tests passed while the
 * bug was fully present. Pick a frequency and windows that never align. */
#define SIG_HZ    137.0f
#define SIG_AMP   0.5f
static float max_step_of_signal(void) { return 2.0f * (float)M_PI * SIG_HZ / FS * SIG_AMP; }

typedef struct { float peak_step; float peak_out; } probe_t;

/* Run `n` samples of sine through the engine, tracking the largest one-sample
 * jump seen on ANY tap channel. */
static probe_t run(engine_t *e, int n, double *phase, int settle)
{
    static float prev[NTAPS];
    float chan[NTAPS];
    probe_t p = { 0.0f, 0.0f };
    for (int i = 0; i < n; i++) {
        float x = SIG_AMP * (float)sin(*phase);
        *phase += 2.0 * M_PI * SIG_HZ / FS;
        engine_process_multi(e, x, 0.5f, chan);
        if (!settle || i > 0) {
            for (int t = 0; t < NTAPS; t++) {
                float d = fabsf(chan[t] - prev[t]);
                if (d > p.peak_step) p.peak_step = d;
                if (fabsf(chan[t]) > p.peak_out) p.peak_out = fabsf(chan[t]);
            }
        }
        for (int t = 0; t < NTAPS; t++) prev[t] = chan[t];
    }
    return p;
}

static void setup(engine_t *e)
{
    memset(buf, 0, sizeof buf);
    engine_init(e, buf, LEN, 0.05f * FS, 0.4f, 1.6f, 1.0f / (0.010f * FS));
    float ph[NTAPS];
    for (int i = 0; i < NTAPS; i++) ph[i] = (float)(i + 1) * (PHASE_FULLSCALE / NTAPS);
    taps_set_phase(&e->taps, ph);
    engine_write(e);
}

int main(void)
{
    /* With the declick in place every transition sits within ~2x the signal's
     * own slope, so 3x is a meaningful regression guard rather than a rubber
     * stamp (pre-fix these measured 30x-200x over). */
    const float lim = max_step_of_signal() * 3.0f;
    printf("signal max step %.5f; discontinuity threshold %.5f\n",
           max_step_of_signal(), lim);

    /* ---------- 1. WRITE -> RECIRC (#27, #29, #30) ---------- */
    {
        engine_t e; double ph = 0.0;
        setup(&e);
        run(&e, 20000, &ph, 1);                       /* fill the buffer      */
        probe_t before = run(&e, 4000, &ph, 0);
        engine_recirc_window(&e, 5001u);
        probe_t at = run(&e, 4000, &ph, 0);
        printf("  write->recirc : steady %.5f  at transition %.5f\n",
               before.peak_step, at.peak_step);
        ck("write->recirc is continuous", at.peak_step < lim);
    }

    /* ---------- 2. RECIRC -> WRITE (#28, #31) ---------- */
    {
        engine_t e; double ph = 0.0;
        setup(&e);
        run(&e, 20000, &ph, 1);
        engine_recirc_window(&e, 5001u);
        run(&e, 8000, &ph, 1);
        probe_t before = run(&e, 4000, &ph, 0);
        engine_write(&e);
        probe_t at = run(&e, 4000, &ph, 0);
        printf("  recirc->write : steady %.5f  at transition %.5f\n",
               before.peak_step, at.peak_step);
        ck("recirc->write is continuous", at.peak_step < lim);
    }

    /* ---------- 3. recirc -> recirc (re-capture; "next sound") ---------- */
    {
        engine_t e; double ph = 0.0;
        setup(&e);
        run(&e, 20000, &ph, 1);
        engine_recirc_window(&e, 5001u);
        run(&e, 8000, &ph, 1);
        probe_t before = run(&e, 4000, &ph, 0);
        engine_recirc_window(&e, 3001u);   /* new take */
        probe_t at = run(&e, 4000, &ph, 0);
        printf("  recirc->recirc: steady %.5f  at transition %.5f\n",
               before.peak_step, at.peak_step);
        ck("re-capture is continuous", at.peak_step < lim);
    }

    /* ---------- 4. the WRITE SEAM left in the buffer (#28) ---------- */
    {
        engine_t e; double ph = 0.0;
        setup(&e);
        run(&e, 20000, &ph, 1);
        engine_recirc_window(&e, 5001u);
        run(&e, 8000, &ph, 1);
        uint32_t resume = e.dl.wpos;
        engine_write(&e);
        run(&e, 4000, &ph, 1);

        /* The buffer either side of the resume point should not step. */
        float worst = 0.0f;
        for (int k = 1; k <= 4; k++) {
            uint32_t a = (resume + LEN - k) % LEN, b = (resume + LEN - k + 1) % LEN;
            float d = fabsf(buf[b] - buf[a]);
            if (d > worst) worst = d;
        }
        printf("  write-resume seam in buffer: %.5f\n", worst);
        ck("write resume leaves no step in the buffer", worst < lim);
    }

    /* ---------- 5. the declick must not damage steady-state audio ---------- */
    {
        engine_t e; double ph = 0.0;
        setup(&e);
        run(&e, 20000, &ph, 1);
        probe_t p = run(&e, 20000, &ph, 0);
        ck("steady state unaffected (taps still sound)", p.peak_out > 0.05f);
        ck("steady state has no discontinuity", p.peak_step < lim);
    }

    printf(fails ? "\n%d FAILURES\n" : "\nall declick checks passed\n", fails);
    return fails ? 1 : 0;
}
