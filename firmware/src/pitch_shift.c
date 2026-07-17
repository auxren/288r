/* pitch_shift.c — see pitch_shift.h. Standard delay-line pitch shift; clean-room. */
#include "pitch_shift.h"
#include <math.h>

#define PS_PI      3.14159265358979f
#define PS_UNITY_EPS 1.0e-4f          /* |1-ratio| below this -> treat as bypass  */

void ps_init(pitchshift_t *p, float window, float base)
{
    p->ratio  = 1.0f;
    p->window = window;
    p->base   = base;
    p->phase  = 0.0f;
}

void ps_set_ratio(pitchshift_t *p, float ratio)
{
    /* keep the per-sample delay ramp well under 1 sample so interpolation stays
       valid: |1-ratio| <= W is the hard limit; clamp conservatively.           */
    if (ratio < 0.25f) ratio = 0.25f;   /* -2 oct */
    if (ratio > 4.0f)  ratio = 4.0f;    /* +2 oct */
    p->ratio = ratio;
}

void ps_reset(pitchshift_t *p) { p->phase = 0.0f; }

float ps_process(pitchshift_t *p, const delay_line_t *d, dl_interp_t interp)
{
    const float W = p->window;

    /* Near unity the ramp is frozen and two static taps would comb-filter;
       collapse to a single centered tap so unity is a clean delayed bypass.    */
    if (fabsf(1.0f - p->ratio) < PS_UNITY_EPS)
        return dl_read(d, p->base + 0.5f * W, interp);

    const float fracA = p->phase;
    const float fracB = (fracA >= 0.5f) ? fracA - 0.5f : fracA + 0.5f;

    const float sA = sinf(PS_PI * fracA);
    const float wA = sA * sA;           /* sin^2(pi*fracA)          */
    const float wB = 1.0f - wA;         /* = sin^2(pi*fracB) exactly */

    const float out = wA * dl_read(d, p->base + fracA * W, interp)
                    + wB * dl_read(d, p->base + fracB * W, interp);

    /* advance: delay changes by (1 - ratio) samples per output sample          */
    float ph = p->phase + (1.0f - p->ratio) / W;
    while (ph >= 1.0f) ph -= 1.0f;
    while (ph <  0.0f) ph += 1.0f;
    p->phase = ph;

    return out;
}
