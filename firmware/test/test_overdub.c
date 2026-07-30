/* test_overdub.c — sound-on-sound (engine od_active/od_decay; owner feature).
 * While a loop plays with overdub engaged, every position the head visits
 * becomes old*decay + conditioned input, resampled onto the varispeed clock
 * (box-decimate under rate<1, linear interp across multi-writes) and held
 * near 0.75 by a slew-gain write limiter. Idle = bit-exact passthrough.
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

    /* ---- exact accumulation, measured MID-HOLD with the ramp pre-settled -- */
    e.od_gain = 1.0f;          /* skip the engage ramp for deterministic math */
    e.od_lp1 = e.od_lp2 = 0.10f;  /* pre-charge the squeal-guard LP (DC-unity) */
    e.od_active = 1;
    for (int i = 0; i < 8000; i++) engine_process_multi(&e, 0.10f, 0.5f, chan);
    float v1p = buf[(ls + 4000u) % LEN];
    ck("one pass mid-hold: old*decay + input",
       fabsf(v1p - (0.25f * 0.95f + 0.10f)) < 1e-3f);
    e.od_active = 0;
    for (int i = 0; i < 16000; i++) engine_process_multi(&e, 0.10f, 0.5f, chan);

    /* ---- release: after the release ramp, content stable ------------------ */
    for (int i = 0; i < 16000; i++) engine_process_multi(&e, 0.0f, 0.5f, chan);
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

    /* ---- sustained hot layering: bounded AND shape-preserving ------------- */
    e.od_active = 1;
    for (int i = 0; i < 80000; i++)
        engine_process_multi(&e, 0.9f * (float)sin(2.0 * M_PI * i / 173.0),
                             0.5f, chan);
    e.od_active = 0;
    float mx = 0.0f;
    for (uint32_t k = 0; k < 8000u; k++) {
        float a = fabsf(buf[(ls + k) % LEN]); if (a > mx) mx = a;
    }
    printf("      max |buffer| after 10 hot passes = %.3f\n", mx);
    ck("write limiter keeps content near DAC-linear (<0.85)", mx < 0.85f);
    {   /* shape preserved: no flat-top runs (the old knee squared the wave) */
        int runs = 0, i2 = 0;
        while (i2 < 7999) {
            float ai = fabsf(buf[(ls + (uint32_t)i2) % LEN]);
            if (ai > 0.5f) {
                int j2 = i2;
                while (j2 + 1 < 8000 &&
                       fabsf(buf[(ls + (uint32_t)(j2+1)) % LEN]
                             - buf[(ls + (uint32_t)i2) % LEN]) < 1e-4f) j2++;
                if (j2 - i2 >= 3) runs++;
                i2 = j2 + 1;
            } else i2++;
        }
        printf("      flat-top runs after hot stacking = %d\n", runs);
        ck("no flat-topping (shape-preserving limiter)", runs == 0);
    }

    /* ---- release re-splice (via looper): seam continuous over new layers -- */
    /* simulate what looper does on release: re-splice and check the guards
     * mirror the (now-layered) head content and the tail meets the lead-in */
    {
        uint32_t le2 = e.xport.loop_end;
        dl_loop_splice(&e.dl, ls, le2, LOOP_SPLICE_FADE);
        int ok = 1;
        for (uint32_t i = 0; i < 4u; i++)
            if (buf[(le2 + i) % LEN] != buf[(ls + i) % LEN]) ok = 0;
        ck("post-overdub re-splice restores guards", ok);
    }

    /* ---- limiter linearity: hot stacking must not WRITE distortion --------
     * (field: 'second overdub = distortion and grit' — the old fast-attack
     * follower modulated the gain at 2x the signal frequency, i.e. harmonic
     * distortion baked into the loop.) Stack a correlated sine 30 passes so
     * the limiter is continuously engaged, then Goertzel-THD the content. */
    {
        engine_t e2; static float b2[LEN];
        engine_init(&e2, b2, LEN, 2000.0f, 0.4f, 1.6f, 0.02f);
        const int W = 8000, CYC = 40;               /* 40 cycles in the loop */
        for (int i = 0; i < 20000; i++)
            engine_process_multi(&e2, 0.5f * (float)sin(2.0*M_PI*CYC*i/W),
                                 0.5f, chan);
        engine_recirc_window(&e2, (uint32_t)W);
        uint32_t s2 = e2.xport.loop_start;
        e2.od_active = 1; e2.od_gain = 1.0f;
        for (int i = 0; i < 30 * W; i++)
            engine_process_multi(&e2, 0.5f * (float)sin(2.0*M_PI*CYC*i/W),
                                 0.5f, chan);
        e2.od_active = 0;
        double mag[5] = {0};
        for (int h = 1; h <= 4; h++) {              /* Goertzel at h*f0 */
            double cr = 0, ci = 0;
            for (int i = 0; i < W; i++) {
                float v = b2[(s2 + (uint32_t)i) % LEN];
                cr += v * cos(2.0*M_PI*h*CYC*i/W);
                ci += v * sin(2.0*M_PI*h*CYC*i/W);
            }
            mag[h] = sqrt(cr*cr + ci*ci);
        }
        double thd = sqrt(mag[2]*mag[2] + mag[3]*mag[3] + mag[4]*mag[4])
                   / (mag[1] > 1e-9 ? mag[1] : 1e-9);
        float mxa = 0.0f;
        for (int i = 0; i < W; i++) {
            float av2 = fabsf(b2[(s2 + (uint32_t)i) % LEN]);
            if (av2 > mxa) mxa = av2;
        }
        ck("no above-FS content ever written (<= 1.0)", mxa <= 1.0f);
        printf("      hot-stack content THD (h2..h4 / h1) = %.4f\n", thd);
        ck("limiter writes no distortion (THD < 2%)", thd < 0.02);
        ck("limited content is hot (fundamental present)",
           mag[1] / (double)W * 2.0 > 0.5);
    }

    /* ---- fractional-rate overdub: no duplicate writes (ZOH signature) -----
     * (field: multiplier moved mid-overdub = zipper.) Layer a sine at rate
     * 1.5 into a silent loop: interp across the double-writes means no two
     * consecutive content samples are bit-identical; ZOH duplicated ~1/3. */
    {
        engine_t e3; static float b3[LEN];
        engine_init(&e3, b3, LEN, 2000.0f, 0.4f, 1.6f, 0.02f);
        for (int i = 0; i < 20000; i++) engine_process_multi(&e3, 0.0f, 0.5f, chan);
        engine_recirc_window(&e3, 8000u);
        uint32_t s3 = e3.xport.loop_start;
        /* capture ref = settled mult (1.0); rate 1.5 = raw for mult 1/1.5
         * (linear taper: raw = (m - 0.4) / 1.2) */
        float raw15 = (1.0f/1.5f - 0.4f) / 1.2f;
        for (int i = 0; i < 60000; i++) engine_process_multi(&e3, 0.0f, raw15, chan);
        e3.od_active = 1; e3.od_gain = 1.0f;
        for (int i = 0; i < 30000; i++)
            engine_process_multi(&e3, 0.7f * (float)sin(2.0*M_PI*i/173.0),
                                 raw15, chan);
        e3.od_active = 0;
        int dup = 0, n = 0;
        for (int i = 0; i < 7999; i++) {
            float a2 = b3[(s3 + (uint32_t)i)  % LEN];
            float b4 = b3[(s3 + (uint32_t)i+1u) % LEN];
            if (fabsf(a2) > 0.05f) { n++; if (a2 == b4) dup++; }
        }
        printf("      rate-1.5 layer: %d duplicate pairs of %d\n", dup, n);
        ck("no ZOH duplicates at fractional rate", n > 1000 && dup == 0);
    }

    /* ---- squeal guard: ultrasonic input is attenuated, audible band isn't -
     * (field: an 18.9 kHz tone at 13%% of loop energy after od sessions —
     * a codec-round-trip feedback mode. The od input LP must break it.) */
    {
        engine_t e4; static float b4[LEN];
        float g19 = 0.0f, g1k = 0.0f;
        for (int pass = 0; pass < 2; pass++) {
            float frq = pass ? 1000.0f : 19000.0f;
            engine_init(&e4, b4, LEN, 2000.0f, 0.4f, 1.6f, 0.02f);
            for (int i = 0; i < 20000; i++) engine_process_multi(&e4, 0.0f, 0.5f, chan);
            engine_recirc_window(&e4, 8000u);
            uint32_t s4 = e4.xport.loop_start;
            e4.od_active = 1; e4.od_gain = 1.0f;
            for (int i = 0; i < 8000; i++)      /* exactly ONE pass: no
                                                   accumulation confound */
                engine_process_multi(&e4,
                    0.5f * (float)sin(2.0*M_PI*frq*i/96000.0), 0.5f, chan);
            e4.od_active = 0;
            float mx = 0.0f;
            for (int i = 0; i < 8000; i++) {
                float av4 = fabsf(b4[(s4 + (uint32_t)i) % LEN]);
                if (av4 > mx) mx = av4;
            }
            if (pass) g1k = mx; else g19 = mx;
        }
        printf("      od write level: 1 kHz %.3f   19 kHz %.3f\n", g1k, g19);
        ck("squeal guard: 19 kHz attenuated >8 dB vs 1 kHz", g19 < g1k * 0.4f);
        ck("squeal guard: audible band preserved (>0.4)", g1k > 0.4f);
    }

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
