/* pitch_voice.h — a playable pitch voice around the pitch_shift core.
 *
 * Adds the two things the raw pitchshift_t doesn't have: the Buchla 1.2 V/oct
 * CV->ratio map (ratio = 2^(volts/1.2)) and a one-pole ratio slew so CV steps
 * glide instead of jumping the read rate (per PITCH_SHIFT.md "Control mapping").
 * One global voice reads the engine's delay buffer and is mixed alongside the dry
 * taps; feed it the same delay_line the engine writes.
 */
#ifndef PITCH_VOICE_H
#define PITCH_VOICE_H

#include "pitch_shift.h"

typedef struct {
    pitchshift_t ps;
    float ratio;    /* current (slewed) ratio */
    float target;   /* target ratio            */
    float slew;     /* one-pole coeff per sample, (0,1] */
} pitch_voice_t;

void  pv_init(pitch_voice_t *v, float window, float base, float slew);
void  pv_set_ratio(pitch_voice_t *v, float ratio);   /* set target directly */
void  pv_set_cv(pitch_voice_t *v, float volts);      /* 1.2 V/oct -> target ratio */

/* One output sample: slew the ratio toward target, then process one pitch sample
 * from the (externally written) delay line. */
float pv_process(pitch_voice_t *v, const delay_line_t *d, dl_interp_t interp);

#endif /* PITCH_VOICE_H */
