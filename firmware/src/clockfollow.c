/* clockfollow.c — see clockfollow.h. Pure: no hardware, no allocation, no libm. */
#include "clockfollow.h"

/* ---- ratio zones, in panel-legend space --------------------------------- */
/* Fifteen ratios across the legend: /8../2 below noon, x1 centred ON the
 * printed 1.0, x2..x8 above. Anchoring to the legend rather than to raw ADC
 * counts is what makes this hardware-independent (see the header). */

static int ratio_valid(int8_t r)
{
    return (r >= CF_RATIO_MIN && r <= -2) || r == 1 || (r >= 2 && r <= CF_RATIO_MAX);
}

/* Zone bounds in legend units for a given ratio. */
static void zone_bounds(int8_t r, float *lo, float *hi)
{
    const float below = (1.0f - CF_X1_HALF) - CF_LEGEND_LO;   /* span for /8../2 */
    const float above = CF_LEGEND_HI - (1.0f + CF_X1_HALF);   /* span for x2..x8 */
    const float wb = below / 7.0f, wa = above / 7.0f;

    if (r == 1) { *lo = 1.0f - CF_X1_HALF; *hi = 1.0f + CF_X1_HALF; return; }
    if (r <= -2) {
        /* -8 sits at the CCW end, -2 just below the unity band */
        int i = (-r) - 2;                       /* -2 -> 0 ... -8 -> 6 */
        *hi = (1.0f - CF_X1_HALF) - (float)i * wb;
        *lo = *hi - wb;
        return;
    }
    {   int i = r - 2;                          /* +2 -> 0 ... +8 -> 6 */
        *lo = (1.0f + CF_X1_HALF) + (float)i * wa;
        *hi = *lo + wa;
    }
}

static int8_t ratio_raw(float m)
{
    if (m <= CF_LEGEND_LO) return CF_RATIO_MIN;
    if (m >= CF_LEGEND_HI) return CF_RATIO_MAX;
    if (m >= 1.0f - CF_X1_HALF && m <= 1.0f + CF_X1_HALF) return 1;

    const float below = (1.0f - CF_X1_HALF) - CF_LEGEND_LO;
    const float above = CF_LEGEND_HI - (1.0f + CF_X1_HALF);

    if (m < 1.0f) {
        float d = ((1.0f - CF_X1_HALF) - m) / (below / 7.0f);
        int i = (int)d; if (i > 6) i = 6; if (i < 0) i = 0;
        return (int8_t)(-(i + 2));
    }
    {
        float d = (m - (1.0f + CF_X1_HALF)) / (above / 7.0f);
        int i = (int)d; if (i > 6) i = 6; if (i < 0) i = 0;
        return (int8_t)(i + 2);
    }
}

int8_t cf_ratio_from_legend(float legend_mult, int8_t current)
{
    if (legend_mult < CF_LEGEND_LO) legend_mult = CF_LEGEND_LO;
    if (legend_mult > CF_LEGEND_HI) legend_mult = CF_LEGEND_HI;

    /* Stay in the current zone until the knob clearly leaves it. Without this a
     * knob parked on a boundary chatters between two ratios, which on a delay
     * is an audible jump in window length rather than a cosmetic wobble. */
    if (ratio_valid(current)) {
        float lo, hi;
        zone_bounds(current, &lo, &hi);
        if (legend_mult >= lo - CF_HYST && legend_mult <= hi + CF_HYST) return current;
    }
    return ratio_raw(legend_mult);
}

/* ---- period tracking ---------------------------------------------------- */

uint32_t cf_update_period(uint32_t period, uint32_t delta)
{
    if (period == 0u) return delta;
    {
        uint32_t diff = (delta > period) ? (delta - period) : (period - delta);
        /* Small deviation = jitter in the incoming clock: average it away.
         * Large deviation = the player changed tempo: SNAP, because averaging
         * a real tempo change just means lagging behind it for several bars. */
        if (diff <= period / 8u) return (period * 3u + delta) / 4u;
    }
    return delta;
}

void cf_init(clockfollow_t *cf, float fs)
{
    if (fs < 1000.0f) fs = 96000.0f;
    cf->period     = 0u;
    cf->since      = 0u;
    cf->locked     = 0u;
    cf->ratio      = 1;
    cf->min_period = (uint32_t)(CF_MIN_PERIOD_SEC * fs);
    cf->max_period = (uint32_t)(CF_MAX_PERIOD_SEC * fs);
    cf->timeout    = (uint32_t)(CF_TIMEOUT_SEC    * fs);
}

int cf_pulse(clockfollow_t *cf, uint32_t dt)
{
    if (dt < cf->min_period || dt > cf->max_period) {
        /* Not a plausible clock interval. Reject WITHOUT disturbing the period
         * already held: a single spurious edge (a patch cable being moved, a
         * bouncing contact) must not throw away a good lock. */
        cf->since = 0u;
        return 0;
    }
    cf->period = cf_update_period(cf->period, dt);
    cf->since  = 0u;
    cf->locked = 1u;
    return 1;
}

int cf_tick(clockfollow_t *cf, uint32_t elapsed)
{
    if (!cf->locked) return 0;
    if (cf->since > 0xFFFFFFFFu - elapsed) cf->since = 0xFFFFFFFFu;
    else                                   cf->since += elapsed;
    if (cf->since >= cf->timeout) {
        cf->locked = 0u;
        cf->period = 0u;      /* forget it: a stale period is worse than none */
    }
    return cf->locked ? 1 : 0;
}

uint32_t cf_cycle_samples(uint32_t period, int8_t ratio)
{
    if (period == 0u) return 0u;
    if (ratio >= 2)  return period * (uint32_t)ratio;
    if (ratio <= -2) return period / (uint32_t)(-ratio);
    return period;
}
