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

static inline int64_t q32_from_float(float x)
{
    return (int64_t)(x * Q32_ONE);       /* exact scale, then truncate */
}

void taps_init(taps_t *t, float base_delay, float slew)
{
    t->base_delay = base_delay;
    t->slew = slew;
    for (int i = 0; i < NUM_TAPS; i++) {
        /* faithful default preset: evenly spaced 20,40,..,160 */
        t->phase[i] = 20.0f * (float)(i + 1);
        t->cur_q[i] = 0;
    }
}

void taps_set_phase(taps_t *t, const float phase[NUM_TAPS])
{
    for (int i = 0; i < NUM_TAPS; i++) t->phase[i] = phase[i];
}

void taps_set_base_delay(taps_t *t, float base_delay)
{
    t->base_delay = base_delay;   /* fixed-rate: just rescales the taps, no clock change */
}

float taps_target(const taps_t *t, int i, float time_mult)
{
    return t->base_delay * (t->phase[i] / PHASE_FULLSCALE) * time_mult;
}

void taps_update(taps_t *t, float time_mult)
{
    for (int i = 0; i < NUM_TAPS; i++) {
        int64_t target_q = q32_from_float(taps_target(t, i, time_mult));
        int64_t delta_q  = target_q - t->cur_q[i];

        /* one-pole slew in Q32.32. The delta is converted to float only for the
         * slew multiply — a small delta converts losslessly, so unlike the float
         * version this never stalls at the ULP of a large position. */
        float step = (float)delta_q * (1.0f / Q32_ONE) * t->slew;
        int64_t step_q = q32_from_float(step);
        if (step_q == 0) t->cur_q[i] = target_q;   /* snap the last <2^-32/slew */
        else             t->cur_q[i] += step_q;
    }
}

float taps_delay(const taps_t *t, int i)
{
    return (float)t->cur_q[i] * (1.0f / Q32_ONE);
}

void taps_delay_frac(const taps_t *t, int i, uint32_t *d_int, float *d_frac)
{
    int64_t q = t->cur_q[i];
    if (q < 0) q = 0;
    *d_int  = (uint32_t)((uint64_t)q >> 32);
    *d_frac = (float)(uint32_t)((uint64_t)q & 0xFFFFFFFFu) * (1.0f / Q32_ONE);
}
