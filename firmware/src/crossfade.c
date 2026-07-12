/* crossfade.c — see crossfade.h. */
#include "crossfade.h"

void xfade_init(xfade_t *x, float fade_samples, float initial_delay)
{
    if (fade_samples < 1.0f) fade_samples = 1.0f;
    x->from_delay = initial_delay;
    x->to_delay   = initial_delay;
    x->g          = 1.0f;
    x->g_inc      = 1.0f / fade_samples;
    x->fading     = 0;
}

void xfade_trigger(xfade_t *x, float new_delay)
{
    x->from_delay = x->to_delay;   /* the current head fades out */
    x->to_delay   = new_delay;
    x->g          = 0.0f;
    x->fading     = 1;
}

int xfade_set_delay(xfade_t *x, float new_delay, float snap_threshold)
{
    float d = new_delay - x->to_delay;
    if (d < 0.0f) d = -d;
    if (d >= snap_threshold) {
        xfade_trigger(x, new_delay);
        return 1;
    }
    x->to_delay = new_delay;   /* small change: move directly (slew upstream) */
    return 0;
}

float xfade_read(xfade_t *x, const delay_line_t *dl, dl_interp_t interp)
{
    if (!x->fading)
        return dl_read(dl, x->to_delay, interp);

    float a = dl_read(dl, x->from_delay, interp);
    float b = dl_read(dl, x->to_delay,   interp);
    float g = x->g;
    float out = a + (b - a) * g;          /* linear crossfade */

    g += x->g_inc;
    if (g >= 1.0f) {
        g = 1.0f;
        x->fading = 0;
        x->from_delay = x->to_delay;
    }
    x->g = g;
    return out;
}
