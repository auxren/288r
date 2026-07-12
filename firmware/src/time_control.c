/* time_control.c — see time_control.h. Self-contained (no libm): the exact panel
 * taper is a bench-calibration item, so we use a simple cubic taper as a placeholder
 * and slew it. Swap map_taper() for the measured curve once known. */
#include "time_control.h"

void tc_init(time_ctrl_t *tc, float initial, float slew, float lo, float hi)
{
    tc->lo = lo;
    tc->hi = hi;
    tc->slew = slew;
    tc->mult = initial;
}

void tc_set_range(time_ctrl_t *tc, float lo, float hi)
{
    tc->lo = lo;
    tc->hi = hi;
}

/* Placeholder taper: raw 0..1 -> lo..hi with a cubic (pot-like) weighting.
 * Endpoints exact; replace with the measured TIME control curve on hardware. */
static inline float map_taper(float raw, float lo, float hi)
{
    return lo + (hi - lo) * raw * raw * raw;
}

float tc_update(time_ctrl_t *tc, float raw01)
{
    if (raw01 < 0.0f) raw01 = 0.0f;
    else if (raw01 > 1.0f) raw01 = 1.0f;

    float target = map_taper(raw01, tc->lo, tc->hi);
    tc->mult += (target - tc->mult) * tc->slew;   /* one-pole slew */
    return tc->mult;
}
