/* mixer.c — see mixer.h. */
#include "mixer.h"

void mixer_init(mixer_t *m)
{
    for (int i = 0; i < NUM_TAPS; i++) {
        m->gain[i]  = 1.0f;
        m->phase[i] = 1.0f;
    }
    m->master = 1.0f / (float)NUM_TAPS;   /* unity-ish headroom default */
}

void mixer_set_tap(mixer_t *m, int i, float gain, float phase)
{
    if (i < 0 || i >= NUM_TAPS) return;
    m->gain[i]  = gain;
    m->phase[i] = (phase < 0.0f) ? -1.0f : 1.0f;
}

void mixer_channels(const mixer_t *m, const float taps[NUM_TAPS], float out[NUM_TAPS])
{
    for (int i = 0; i < NUM_TAPS; i++)
        out[i] = taps[i] * m->gain[i] * m->phase[i];
}

float mixer_sum(const mixer_t *m, const float taps[NUM_TAPS], float auto_correction)
{
    float acc = 0.0f;
    for (int i = 0; i < NUM_TAPS; i++)
        acc += taps[i] * m->gain[i] * m->phase[i];
    return (acc + auto_correction) * m->master;
}

float mixer_input(float signal, float gain)
{
    return signal * gain;
}
