/* mixer.h — 288r input & output mixers + phase select (faithful clone).
 *
 * Output stage: each of the 8 taps has a LEVEL (OUTPUT MIXER slider, read over I2C
 * into slider_raw_table in the stock firmware) and a PHASE SELECT polarity; the taps
 * are summed to the output. AUTO CONTROL adds a per-sample correction term
 * (auto_control_correction @0x20002088) — modeled as an added offset here; its exact
 * generation is a bench-calibration item.
 *
 * Gain law (slider count -> linear gain) and any output scaling are calibration
 * items; we normalize to [0,1] linear as a documented placeholder.
 */
#ifndef MIXER_H
#define MIXER_H

#include "taps.h"   /* NUM_TAPS */

typedef struct {
    float  gain[NUM_TAPS];   /* per-tap output level, 0..1 (from sliders)   */
    float  phase[NUM_TAPS];  /* per-tap polarity, +1.0 or -1.0 (phase sel)  */
    float  master;           /* overall output gain (headroom/scaling)      */
} mixer_t;

void  mixer_init(mixer_t *m);

/* Set one tap's level (0..1) and polarity (+1/-1). */
void  mixer_set_tap(mixer_t *m, int i, float gain, float phase);

/* Per-tap channel outputs (each tap * its gain * phase) -> the 8 DAC channels of the
 * CS42888 (each 288 tap has its own physical output). out[] must hold NUM_TAPS. */
void  mixer_channels(const mixer_t *m, const float taps[NUM_TAPS], float out[NUM_TAPS]);

/* Sum the 8 taps with gains/phase, add auto-control correction, apply master.
 * (The "mixed" output jacks; equals master*(sum of channels)+correction.) */
float mixer_sum(const mixer_t *m, const float taps[NUM_TAPS], float auto_correction);

/* Simple input mix: signal * gain (+ cv-scaled, placeholder for INPUT MIXER). */
float mixer_input(float signal, float gain);

#endif /* MIXER_H */
