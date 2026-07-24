/* test_pitch_loop.c — pitch voice over a recirculating loop (#19).
 *
 * The shifter's reads must window-map in RECIRC: the write head snaps back at
 * the loop boundary, and un-mapped reads fetch garbage on every wrap (bench:
 * ~100 full-scale overrange events/s with silent input). With the window set,
 * grains cross the seam continuously (the capture splice + guard samples make
 * the content itself seamless).
 */
#include "pitch_shift.h"
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

/* run the shifter over N samples of loop playback; return max |out| and max
 * sample-to-sample |step| */
static void run_loop(pitchshift_t *ps, delay_line_t *d,
                     uint32_t ls, uint32_t le, int n,
                     float *mx_out, float *mx_step)
{
    float prev = 0.0f; int primed = 0;
    *mx_out = 0.0f; *mx_step = 0.0f;
    for (int i = 0; i < n; i++) {
        dl_advance_loop(d, ls, le);
        float y = ps_process(ps, d, DL_INTERP_HERMITE);
        float ay = fabsf(y);
        if (ay > *mx_out) *mx_out = ay;
        if (primed) {
            float st = fabsf(y - prev);
            if (st > *mx_step) *mx_step = st;
        }
        prev = y; primed = 1;
    }
}

int main(void)
{
    delay_line_t d; dl_init(&d, buf, LEN);
    dl_clear(&d);
    /* record a 0.5-amplitude sine (period 173 — doesn't divide the window) */
    for (int i = 0; i < 30000; i++)
        dl_write(&d, 0.5f * (float)sin(2.0 * M_PI * i / 173.0));

    uint32_t le = d.wpos;                       /* loop = last 8000 samples  */
    uint32_t ls = le - 8000u;
    dl_loop_splice(&d, ls, le, 480);            /* capture-time seam splice  */
    d.wpos = ls;

    pitchshift_t ps; ps_init(&ps, 1024.0f, 1200.0f);
    ps_set_ratio(&ps, 1.26f);                   /* an active up-shift        */

    /* --- unfixed path (no window set): must produce garbage at wraps ------ */
    float mo, msu;
    run_loop(&ps, &d, ls, le, 40000, &mo, &msu);
    ck("validity: un-mapped loop reads DO glitch", msu > 0.10f);

    /* --- fixed path: window set, several wraps, bounded and click-free ----- */
    d.wpos = ls;
    ps_init(&ps, 1024.0f, 1200.0f);
    ps_set_ratio(&ps, 1.26f);
    ps_set_loop_window(&ps, ls, le, LEN);
    float mo2, ms2;
    run_loop(&ps, &d, ls, le, 40000, &mo2, &ms2);
    ck("mapped: output bounded (~input level)", mo2 < 0.75f);
    /* content max slope ~ 0.5*2pi/173*1.26 = 0.023; crossfades stay near it */
    ck("mapped: no wrap clicks (max step ~ slope)", ms2 < 0.06f);
    ck("mapped: clearly better than unmapped", ms2 < 0.5f * msu);

    /* --- AA bypass while looping: up-shift band must not engage ----------- */
    ck("AA disabled in loop (band request idle)", ps.aaband_req == -1);

    /* --- WRAPPED window (end < start): the majority of long captures ------- */
    {
        /* rebuild content so the window straddles the buffer wrap: sine into
         * the last 4k and first 4k of the buffer, continuous across 0 */
        dl_init(&d, buf, LEN); dl_clear(&d);
        d.wpos = LEN - 6000u;
        for (int i = 0; i < 12000; i++)
            dl_write(&d, 0.5f * (float)sin(2.0 * M_PI * i / 173.0));
        uint32_t wle = d.wpos;                 /* = 6000                    */
        uint32_t wls = LEN - 2000u;            /* window wraps through 0    */
        dl_loop_splice(&d, wls, wle, 480);
        d.wpos = wls;
        ps_init(&ps, 1024.0f, 1200.0f);
        ps_set_ratio(&ps, 1.26f);
        ps_set_loop_window(&ps, wls, wle, LEN);
        ck("wrapped window accepted (span = 8000)", ps.lp_span == 8000u);
        float mo3, ms3;
        run_loop(&ps, &d, wls, wle, 40000, &mo3, &ms3);
        ck("wrapped: bounded output", mo3 < 0.75f);
        ck("wrapped: no wrap clicks", ms3 < 0.06f);
    }

    /* --- window release returns to the live path -------------------------- */
    ps_set_loop_window(&ps, 0u, 0u, LEN);
    ck("release: span cleared", ps.lp_span == 0u);

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
