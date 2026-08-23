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
    /* The splice is a CHUNKED background job (the inline burst starved the ISR
     * and recorded the tear at the seam, field 2026-07-29), and since the
     * 2026-08 budget work its per-frame quota is sized from the window so the
     * job finishes inside one loop pass rather than at a flat 8/sample. Run a
     * whole pass before asserting — that IS the deadline. */
    for (uint32_t i = 0; i < 4000u; i++) {
        engine_process_multi(&e, 0.0f, 0.5f, chan);
        if (!e.spl_active) break;
    }
    ck("chunked splice completes within one loop pass", !e.spl_active);
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

    /* ---- the quota is a QUALITY parameter, not a spare CPU budget ----------
     * The per-frame quota is derived from LOOP_SPLICE_FADE and DECLICK_FADE so
     * it tracks them, but the value it must land on for the shipped constants
     * was measured, not chosen: dropping it makes test_declick's capture cases
     * fail (sweep table in engine.c splice_arm). Pin it, so a change to either
     * constant fails HERE with an explanation rather than as a mystery click
     * in someone's loop three releases later. */
    {
        engine_t eq; static float bq[LEN]; float ch[NUM_TAPS];
        engine_init(&eq, bq, LEN, 2000.0f, 0.4f, 1.6f, 0.02f);
        for (int i = 0; i < 12000; i++) engine_process_multi(&eq, 0.1f, 0.5f, ch);
        engine_recirc_window(&eq, 8000u);      /* long window: exposure binds  */
        ck("splice quota holds the measured seam schedule (8/frame)",
           eq.spl_active && eq.spl_quota == 8u);
    }

    /* ---- CHUNKED JOB == ONE-SHOT SPLICE (content equivalence) --------------
     * The quota only changes WHEN the tail is rewritten, never WHAT it becomes.
     * Run the same capture two ways — the amortised engine job, and the
     * one-shot dl_loop_splice() on an identical buffer — and require the tail
     * to come out bit-identical. This is the gate that lets the quota be tuned
     * for CPU without anyone having to re-listen to the seam. */
    {
        static float b1[LEN], b2[LEN];
        engine_t ea, eb;
        float ch[NUM_TAPS];
        engine_init(&ea, b1, LEN, 2000.0f, 0.4f, 1.6f, 0.02f);
        engine_init(&eb, b2, LEN, 2000.0f, 0.4f, 1.6f, 0.02f);
        for (int i = 0; i < 12000; i++) {
            float x = 0.9f * (float)sin(2.0 * M_PI * i / 700.0);
            engine_process_multi(&ea, x, 0.5f, ch);
            engine_process_multi(&eb, x, 0.5f, ch);
        }
        uint32_t h = ea.dl.wpos, w = 4000u;
        uint32_t s = (h >= w) ? h - w : h + LEN - w;
        /* eb: the reference — splice it in one shot, exactly as the pre-2026-08
         * dl_loop_splice() path did, and do NOT run the engine afterwards. */
        dl_loop_splice(&eb.dl, s, h, LOOP_SPLICE_FADE);
        for (uint32_t i = 0; i < 4u; i++)
            b2[(h + i) % LEN] = b2[(s + i) % LEN];
        /* ea: capture through the engine and let the job finish. RECIRC does
         * not write, so nothing but the splice touches the tail. */
        engine_recirc_window(&ea, w);
        for (uint32_t i = 0; i < 8000u && ea.spl_active; i++)
            engine_process_multi(&ea, 0.0f, 0.5f, ch);
        int same = !ea.spl_active;
        for (uint32_t i = 0; i < LOOP_SPLICE_FADE + 4u; i++) {
            uint32_t k = (h + LEN - LOOP_SPLICE_FADE + i) % LEN;
            if (b1[k] != b2[k]) same = 0;
        }
        ck("chunked job == one-shot splice, bit for bit", same);
    }

    /* ---- DEADLINE: the shortest legal window at the fastest legal rate -----
     * The job must be finished before the read head WRAPS through the seam it
     * is repairing, or the wrap click it exists to kill is back for that pass.
     * Shortest window the arm accepts is 2*fade; the varispeed clamp caps head
     * travel at 4 samples per frame. Sweep from there upward and require the
     * job to complete inside one pass at rate 4. */
    {
        int ok = 1;
        uint32_t worst_frac_num = 0, worst_win = 0;
        for (uint32_t win = 2u * LOOP_SPLICE_FADE + 1u; win < 16000u; win += 617u) {
            static float b3[LEN];
            engine_t ec; float ch[NUM_TAPS];
            engine_init(&ec, b3, LEN, 2000.0f, 0.4f, 1.6f, 1.0f);
            for (uint32_t i = 0; i < LEN; i++)
                engine_process_multi(&ec, 0.1f, 0.5f, ch);
            ec.varispeed = 1;
            engine_recirc_window(&ec, win);
            /* mult at the low rail vs the capture reference -> rate clamps 4.0 */
            ec.lp_mult_ref = 1.6f;
            uint32_t frames = 0;
            while (ec.spl_active && frames < 100000u) {
                engine_process_multi(&ec, 0.0f, 0.0f, ch);   /* raw01 0 -> 0.4 */
                frames++;
            }
            /* head travel while the job ran, in samples, vs the window */
            uint32_t travel = frames * 4u;
            if (ec.spl_active || travel > win) ok = 0;
            if ((uint64_t)travel * 100u / win > worst_frac_num) {
                worst_frac_num = (uint32_t)((uint64_t)travel * 100u / win);
                worst_win = win;
            }
        }
        printf("      worst case: head travelled %u%% of the window (win %u)\n",
               worst_frac_num, worst_win);
        ck("splice beats the head to the seam at rate 4.0", ok);
    }

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
