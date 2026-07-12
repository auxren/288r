/* crossfade.h — dual read-head crossfade over a delay line.
 *
 * A raw delay-time change teleports the read pointer -> a waveform step -> click
 * (and it recirculates through feedback). This gives a **click-free handoff**: read
 * from two heads and crossfade old->new over a short fade. Two uses:
 *   1. glide = 0 "snap": on a discrete TIME change, crossfade instead of jumping.
 *   2. Pitch/Time buffer-wrap glitch: crossfade the read across the wrap.
 *
 * One xfade_t per tap. Interpolation/fade are hardware float; v1 uses a linear
 * crossfade (simple, allocation-free; swap for equal-power later if needed).
 */
#ifndef CROSSFADE_H
#define CROSSFADE_H

#include "delay_line.h"

typedef struct {
    float from_delay;   /* outgoing head (samples)              */
    float to_delay;     /* incoming head / current (samples)    */
    float g;            /* fade position 0..1 toward `to`       */
    float g_inc;        /* per-sample step = 1 / fade_samples   */
    int   fading;       /* nonzero while a crossfade is active  */
} xfade_t;

/* fade_samples: crossfade length (e.g. ~2-10 ms * fs). Clamped to >= 1. */
void  xfade_init(xfade_t *x, float fade_samples, float initial_delay);

/* Force a crossfade to `new_delay` (snap / wrap handoff). If already fading, the
 * in-progress incoming head becomes the new outgoing head. */
void  xfade_trigger(xfade_t *x, float new_delay);

/* Auto: if |new - current| >= snap_threshold, trigger a crossfade; otherwise move
 * the incoming head directly (small changes are meant to be slewed upstream).
 * Returns 1 if it snapped (crossfade started), 0 if it moved continuously. */
int   xfade_set_delay(xfade_t *x, float new_delay, float snap_threshold);

/* Read one crossfaded, interpolated sample; advances any active fade. */
float xfade_read(xfade_t *x, const delay_line_t *dl, dl_interp_t interp);

#endif /* CROSSFADE_H */
