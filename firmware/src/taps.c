/* taps.c — see taps.h. Independently reconstructed from behavioral analysis. */
#include "taps.h"

void taps_init(taps_t *t, float base_delay, float slew)
{
    t->base_delay = base_delay;
    t->slew = slew;
    for (int i = 0; i < NUM_TAPS; i++) {
        /* faithful default preset: evenly spaced 20,40,..,160 */
        t->phase[i] = 20.0f * (float)(i + 1);
        t->cur[i]   = 0.0f;
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
        float target = taps_target(t, i, time_mult);
        t->cur[i] += (target - t->cur[i]) * t->slew;   /* one-pole slew (fractional) */
    }
}

float taps_delay(const taps_t *t, int i)
{
    return t->cur[i];
}
