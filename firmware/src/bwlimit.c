/* bwlimit.c — see bwlimit.h. One-pole RC low-pass, no libm. */
#include "bwlimit.h"

#define BW_TWO_PI 6.28318530717959f

void bw_init(bwlimit_t *b, float fs, float cutoff_hz)
{
    b->state = 0.0f;
    if (cutoff_hz <= 0.0f || cutoff_hz >= 0.5f * fs) {
        b->coeff = 1.0f;                        /* bypass: y = x exactly */
    } else {
        float w = BW_TWO_PI * cutoff_hz / fs;   /* normalized cutoff, rad/sample */
        b->coeff = w / (1.0f + w);              /* discrete RC one-pole; no expf */
    }
}

float bw_process(bwlimit_t *b, float x)
{
    if (b->coeff >= 1.0f) return x;   /* bypass: exact identity (no rounding drift) */
    b->state += b->coeff * (x - b->state);
    return b->state;
}
