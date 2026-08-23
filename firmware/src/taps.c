/* taps.c — see taps.h. Independently reconstructed from behavioral analysis.
 *
 * Tap positions are held in Q32.32 fixed point (int64), NOT float: a float32
 * position has an ULP of 1/8 sample at a 2M-sample delay (one ~20 s SDRAM bank
 * @96 kHz), so a float one-pole slew stair-steps and eventually stalls — the
 * exact artifact this engine exists to remove. In Q32.32 the slew resolution
 * is 2^-32 samples at any delay. Note float->Q32.32 conversion is EXACT:
 * multiplying by 2^32 only shifts the float exponent. */
#include "taps.h"

#define Q32_ONE 4294967296.0f            /* 2^32 */

/* float -> Q32.32 using ONLY 32-bit FPU converts (two VCVTs). A direct
 * (int64_t)(x * 2^32) emits __aeabi_f2lz/f2ulz, which libgcc implements by
 * promoting to SOFT-DOUBLE — hundreds of cycles, and this runs per tap per
 * sample (it starved the CPU on hardware: audio ISR ate 100%, main never ran).
 * Split conversion: integer part + fraction scaled to Q1.31, both single VCVT.
 * Same 24-bit float precision as the direct cast. */
static inline int64_t q32_from_float(float x)
{
    int32_t hi = (int32_t)x;                          /* trunc toward zero  */
    float   fr = x - (float)hi;                       /* (-1,1)             */
    int32_t lo = (int32_t)(fr * 2147483648.0f);       /* Q1.31, fits int32  */
    return ((int64_t)hi << 32) + ((int64_t)lo << 1);  /* Q32.32             */
}

/* Q32.32 -> float SAMPLES without __aeabi_l2f (soft int64->float): high/low
 * words via two 32-bit VCVTs. Exact to float's 24-bit mantissa, branch-free
 * (negative q: arithmetic-shift high + unsigned low still sum correctly). */
static inline float q32_to_float(int64_t q)
{
    return (float)(int32_t)(q >> 32)
         + (float)(uint32_t)(uint64_t)q * (1.0f / Q32_ONE);
}

static void taps_retarget(taps_t *t, float time_mult);

void taps_init(taps_t *t, float base_delay, float slew)
{
    t->base_delay = base_delay;
    t->slew = slew;
    t->last_mult = -1.0e30f;
    t->targets_dirty = 1;
    t->blk_n = 0u;              /* forces the n-step constants to be built */
    t->blk_slew = 0.0f;
    t->blk_step = 0.0f;
    for (int i = 0; i < NUM_TAPS; i++) {
        /* faithful default preset: evenly spaced 20,40,..,160 */
        t->phase[i] = 20.0f * (float)(i + 1);
        t->cur_q[i] = 0;
        t->base_q[i] = 0;
        t->step_q[i] = 0;
    }
}

void taps_set_phase(taps_t *t, const float phase[NUM_TAPS])
{
    for (int i = 0; i < NUM_TAPS; i++) t->phase[i] = phase[i];
    t->targets_dirty = 1;
}

void taps_set_base_delay(taps_t *t, float base_delay)
{
    t->base_delay = base_delay;   /* fixed-rate: just rescales the taps, no clock change */
    t->targets_dirty = 1;
}

float taps_target(const taps_t *t, int i, float time_mult)
{
    return t->base_delay * (t->phase[i] / PHASE_FULLSCALE) * time_mult;
}

void taps_update(taps_t *t, float time_mult)
{
    /* targets only move when the CONTROL value moves (~kHz), not per audio
     * sample: recompute the 8 targets only on change. Profiled on hardware:
     * per-sample target math was 19% of the whole CPU. */
    taps_retarget(t, time_mult);
    for (int i = 0; i < NUM_TAPS; i++) {
        int64_t target_q = t->tgt_q[i];
        int64_t delta_q  = target_q - t->cur_q[i];
        if (delta_q == 0) continue;               /* lane settled: skip      */

        /* one-pole slew in Q32.32. The delta is converted to float only for the
         * slew multiply — a small delta converts losslessly, so unlike the float
         * version this never stalls at the ULP of a large position. */
        float step = q32_to_float(delta_q) * t->slew;
        int64_t step_q = q32_from_float(step);
        if (step_q == 0) t->cur_q[i] = target_q;   /* snap the last <2^-32/slew */
        else             t->cur_q[i] += step_q;
    }
}

/* Refresh the Q32.32 target cache if the control moved. Shared by the
 * per-sample and per-block updates so they can never disagree about targets. */
static void taps_retarget(taps_t *t, float time_mult)
{
    if (time_mult != t->last_mult || t->targets_dirty) {
        for (int i = 0; i < NUM_TAPS; i++)
            t->tgt_q[i] = q32_from_float(taps_target(t, i, time_mult));
        t->last_mult = time_mult;
        t->targets_dirty = 0;
    }
}

void taps_update_block(taps_t *t, float time_mult, unsigned n)
{
    if (n == 0u) return;
    taps_retarget(t, time_mult);

    /* n-step one-pole coefficient: 1-(1-slew)^n, divided by n to give the ramp
     * slope directly. Both the pow and the divide happen only when the block
     * size or the slew changes — i.e. essentially never — so the per-block cost
     * carries no division at all. */
    if (n != t->blk_n || t->slew != t->blk_slew) {
        float keep = 1.0f - t->slew;
        float p = 1.0f;
        for (unsigned k = 0; k < n; k++) p *= keep;   /* (1-slew)^n */
        t->blk_n = n;
        t->blk_slew = t->slew;
        t->blk_step = (1.0f - p) / (float)n;
    }

    const float kstep = t->blk_step;
    for (int i = 0; i < NUM_TAPS; i++) {
        int64_t cur = t->cur_q[i];
        int64_t delta_q = t->tgt_q[i] - cur;
        t->base_q[i] = cur;
        if (delta_q == 0) { t->step_q[i] = 0; continue; }  /* lane settled */

        /* Same float-only conversion path as the per-sample slew (a direct
         * int64<->float cast drags in soft-double; see q32_from_float). */
        int64_t step_q = q32_from_float(q32_to_float(delta_q) * kstep);
        if (step_q == 0) {
            /* below one ULP per sample: snap, exactly as taps_update() does,
             * so a lane cannot creep-stall short of its target. */
            t->cur_q[i] = t->tgt_q[i];
            t->base_q[i] = t->tgt_q[i];
            t->step_q[i] = 0;
        } else {
            /* end = base + n*step EXACTLY, so the ramp's last sample and the
             * next block's first sample are continuous by construction. */
            t->step_q[i] = step_q;
            t->cur_q[i] = cur + step_q * (int64_t)n;
        }
    }
}

void taps_delay_frac_at(const taps_t *t, int i, unsigned k,
                        uint32_t *d_int, float *d_frac)
{
    /* k+1: sample k sees the position AFTER its own slew step, matching
     * engine_process_multi()'s update-then-read order (see taps.h). */
    int64_t q = t->base_q[i] + t->step_q[i] * (int64_t)(k + 1u);
    if (q < 0) q = 0;
    *d_int  = (uint32_t)((uint64_t)q >> 32);
    *d_frac = (float)(uint32_t)((uint64_t)q & 0xFFFFFFFFu) * (1.0f / Q32_ONE);
}

float taps_delay(const taps_t *t, int i)
{
    return q32_to_float(t->cur_q[i]);
}

void taps_delay_frac(const taps_t *t, int i, uint32_t *d_int, float *d_frac)
{
    int64_t q = t->cur_q[i];
    if (q < 0) q = 0;
    *d_int  = (uint32_t)((uint64_t)q >> 32);
    *d_frac = (float)(uint32_t)((uint64_t)q & 0xFFFFFFFFu) * (1.0f / Q32_ONE);
}
