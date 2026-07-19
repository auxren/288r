/* ks.c — see ks.h. */
#include "ks.h"

void ks_init(ks_t *k, float fb, float damp)
{
    for (int i = 0; i < KS_STRINGS; i++) {
        for (int n = 0; n < KS_RING_LEN; n++) k->ring[i][n] = 0.0f;
        k->period[i] = 480.0f;       /* 200 Hz default                       */
        k->target[i] = 480.0f;
        k->w[i] = 0u;
    }
    k->fb = fb;
    k->damp = damp;
}

void ks_set_period(ks_t *k, int i, float period)
{
    if (i < 0 || i >= KS_STRINGS) return;
    if (period < KS_MIN_PERIOD) period = KS_MIN_PERIOD;
    if (period > (float)(KS_RING_LEN - 3)) period = (float)(KS_RING_LEN - 3);
    k->target[i] = period;
}

void ks_process(ks_t *k, float x, float chan[KS_STRINGS])
{
    for (int i = 0; i < KS_STRINGS; i++) {
        /* glide the period (de-zippered retuning, ~5 ms) */
        k->period[i] += (k->target[i] - k->period[i]) * 0.002f;

        const float p = k->period[i];
        const uint32_t pi = (uint32_t)p;
        const float    pf = p - (float)pi;
        const uint32_t w  = k->w[i];
        /* fractional read at delay p (linear), plus the one-older sample for
         * the classic KS damping average */
        uint32_t a0 = (w + KS_RING_LEN - pi)     % KS_RING_LEN;
        uint32_t a1 = (a0 + KS_RING_LEN - 1u)    % KS_RING_LEN;
        uint32_t a2 = (a1 + KS_RING_LEN - 1u)    % KS_RING_LEN;
        float s0 = k->ring[i][a0] + pf * (k->ring[i][a1] - k->ring[i][a0]);
        float s1 = k->ring[i][a1] + pf * (k->ring[i][a2] - k->ring[i][a1]);
        /* damped feedback + excitation injection */
        float y = k->fb * ((1.0f - k->damp) * s0 + k->damp * 0.5f * (s0 + s1));
        k->ring[i][w] = y + x;
        k->w[i] = (w + 1u) % KS_RING_LEN;
        chan[i] = y;
    }
}
