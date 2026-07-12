/* taps.h — 288r 8-tap "time domain" model (faithful clone on the fixed-rate engine).
 *
 * The 288 reads 8 delay taps whose positions are set by PHASE SELECT (0..160) and
 * scaled by the TIME MULTIPLIER. In the stock firmware (sub_2030/sub_1968) the tap
 * target was  roundf(phase[i] * time_mult)  and the "current" positions chased it in
 * integer steps -> stepping. Here we keep the same model but the current positions
 * are FLOAT and chase the target with a one-pole slew, so delay-time modulation is
 * continuous. Delay values are in samples at the current fixed base rate; feed them
 * to dl_read().
 *
 * Values recovered from the binary:
 *   - PHASE SELECT full scale = 160 (preset_phase_table holds 20,40,..,160).
 *   - tap delay(samples) = base_delay * (phase[i]/160) * time_mult.
 * `base_delay` (the SHORT/FULL cycle length in samples) and the exact phase presets
 * are set by the caller from panel/preset state; see panel.* (to come).
 */
#ifndef TAPS_H
#define TAPS_H

#include <stdint.h>

#define NUM_TAPS        8
#define PHASE_FULLSCALE 160.0f

typedef struct {
    float phase[NUM_TAPS];   /* per-tap PHASE SELECT position, 0..PHASE_FULLSCALE */
    float cur[NUM_TAPS];     /* current (slewing) delay in samples                */
    float base_delay;        /* samples at phase=fullscale, time_mult=1 (cycle len)*/
    float slew;              /* one-pole coeff per update, (0,1]; 1 = instant      */
} taps_t;

/* base_delay: cycle length in samples (SHORT/FULL). slew in (0,1]. */
void  taps_init(taps_t *t, float base_delay, float slew);

/* Load 8 phase-select positions (0..160), e.g. from a preset row. */
void  taps_set_phase(taps_t *t, const float phase[NUM_TAPS]);

/* Change the active cycle length (SHORT/FULL) without a clock glitch. */
void  taps_set_base_delay(taps_t *t, float base_delay);

/* Target delay (samples) for tap i at the given time multiplier. */
float taps_target(const taps_t *t, int i, float time_mult);

/* Advance all taps one slew step toward the current target. */
void  taps_update(taps_t *t, float time_mult);

/* Current (slewed) delay in samples for tap i — pass to dl_read(). */
float taps_delay(const taps_t *t, int i);

#endif /* TAPS_H */
