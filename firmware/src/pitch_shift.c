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
    p->off[0] = 0.0f;
    p->off[1] = 0.0f;
    p->pend_off = 0.0f;
    p->pend_tap = -1;
    p->next_tap = 0;
}

void ps_set_ratio(pitchshift_t *p, float ratio)
{
    /* keep the per-sample delay ramp well under 1 sample so interpolation stays
       valid: |1-ratio| <= W is the hard limit; clamp conservatively.           */
    if (ratio < 0.25f) ratio = 0.25f;   /* -2 oct */
    if (ratio > 4.0f)  ratio = 4.0f;    /* +2 oct */
    p->ratio = ratio;
}

void ps_reset(pitchshift_t *p)
{
    p->phase = 0.0f;
    p->off[0] = p->off[1] = 0.0f;
    p->pend_tap = -1;
    p->next_tap = 0;
}

/* ---- de-glitch: correlation-aligned splices (H949-style) -------------------
 * When tap T is about to wrap, its new read position (delay base+off) should be
 * WAVEFORM-ALIGNED with the other tap (delay ~base+W/2+offOther) so the
 * crossfade sums coherent material instead of combing. We search off in
 * [0, PS_DEGLITCH_MAXLAG) for the offset maximizing normalized correlation
 * between the two delay positions over PS_DEGLITCH_N samples. Runs in the main
 * loop; ~N*MAXLAG MACs (~250k) per wrap (wraps are ~0.5 s apart). */
#define PS_DEGLITCH_N       384
#define PS_DEGLITCH_MAXLAG  1200      /* one period down to ~80 Hz @96k */
#define PS_DEGLITCH_LOOKA   0.06f     /* start searching within 6% of the wrap */

void ps_service(pitchshift_t *p, const delay_line_t *d)
{
    if (p->pend_tap >= 0) return;                 /* already prepared        */
    float step = (1.0f - p->ratio) / p->window;
    if (step == 0.0f) return;                     /* unity: no wraps         */

    /* distance (in phase) to the next wrap of next_tap */
    float ph = p->phase;
    float dist;
    if (p->next_tap == 0)                          /* A wraps at 1 (or 0)    */
        dist = (step > 0.0f) ? (1.0f - ph) : ph;
    else                                           /* B wraps at 0.5         */
        dist = (step > 0.0f) ? ((ph < 0.5f) ? 0.5f - ph : 1.5f - ph)
                             : ((ph >= 0.5f) ? ph - 0.5f : ph + 0.5f);
    float astep = (step > 0.0f) ? step : -step;
    if (dist > PS_DEGLITCH_LOOKA) return;          /* not imminent yet       */
    if (dist < 8.0f * astep) return;               /* too late — skip safely */

    /* correlate: the incoming tap enters at frac 0 for DOWN-shifts (step>0,
     * delay base) but at frac 1 for UP-shifts (step<0, delay base+W) — the
     * phase runs backward for ratio>1. The outgoing tap sits mid-window. */
    const float dIn  = (step > 0.0f) ? p->base : p->base + p->window;
    const float dOut = p->base + 0.5f * p->window + p->off[p->next_tap ^ 1];
    float best = -1.0e30f; float bestoff = 0.0f;
    for (int lag = 0; lag < PS_DEGLITCH_MAXLAG; lag += 4) {   /* coarse 4-sample grid */
        float acc = 0.0f, ein = 0.0f;
        for (int k = 0; k < PS_DEGLITCH_N; k += 2) {          /* decimate x2 */
            float a = dl_read(d, dIn + (float)lag + (float)k, DL_INTERP_LINEAR);
            float b = dl_read(d, dOut + (float)k, DL_INTERP_LINEAR);
            acc += a * b;
            ein += a * a;
        }
        float score = (ein > 1.0e-9f) ? acc / (ein + 1.0e-9f) : -1.0e30f;
        if (score > best) { best = score; bestoff = (float)lag; }
    }
    p->pend_off = bestoff;
    __asm volatile ("" ::: "memory");              /* order: off BEFORE tap  */
    p->pend_tap = p->next_tap;                     /* publish AFTER off      */
}

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

    const float out = wA * dl_read(d, p->base + fracA * W + p->off[0], interp)
                    + wB * dl_read(d, p->base + fracB * W + p->off[1], interp);

    /* advance: delay changes by (1 - ratio) samples per output sample          */
    float ph = p->phase + (1.0f - p->ratio) / W;
    /* tap wraps: consume the de-glitch offset the service prepared (0 if none).
       A wraps when phase crosses 1 (or 0 going down); B when phase crosses 0.5. */
    if (ph >= 1.0f || ph < 0.0f) {
        p->off[0] = (p->pend_tap == 0) ? p->pend_off : 0.0f;
        p->pend_tap = -1; p->next_tap = 1;
    } else {
        int sideNew = (ph >= 0.5f), sideOld = (p->phase >= 0.5f);
        if (sideNew != sideOld) {                    /* B's frac wrapped */
            p->off[1] = (p->pend_tap == 1) ? p->pend_off : 0.0f;
            p->pend_tap = -1; p->next_tap = 0;
        }
    }
    while (ph >= 1.0f) ph -= 1.0f;
    while (ph <  0.0f) ph += 1.0f;
    p->phase = ph;

    return out;
}
