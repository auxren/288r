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
    /* gain*phase, folded at CONTROL rate. Level and polarity are panel state
     * that changes at human speed; multiplying them together on every tap of
     * every sample was 8 loads and 8 multiplies per frame for a product that
     * had not changed in minutes. Kept as a cache (gain[]/phase[] remain the
     * authoritative, inspectable state) so nothing outside this file changes. */
    float  coef[NUM_TAPS];
    float  master;           /* overall output gain (headroom/scaling)      */
} mixer_t;
/* COEF IS A CACHE: mixer_set_tap() is the ONLY thing allowed to write gain[] or
 * phase[]. Anything that writes them directly (a preset recall, a slider path)
 * leaves coef stale and the audio is silently wrong — with no test able to see
 * it, because both arrays still read back correct. */

void  mixer_init(mixer_t *m);

/* Set one tap's level (0..1) and polarity (+1/-1). */
void  mixer_set_tap(mixer_t *m, int i, float gain, float phase);

/* Per-tap channel outputs (each tap * its gain * phase) -> the 8 DAC channels of the
 * CS42888 (each 288 tap has its own physical output). out[] must hold NUM_TAPS. */
void  mixer_channels(const mixer_t *m, const float taps[NUM_TAPS], float out[NUM_TAPS]);

/* Sum the 8 taps with gains/phase, add auto-control correction, apply master.
 * (The "mixed" output jacks; equals master*(sum of channels)+correction.) */
float mixer_sum(const mixer_t *m, const float taps[NUM_TAPS], float auto_correction);

/* (mixer_sum_chan() lived here: a sum-from-channels variant added for a block
 * path that then did not compute a sum at all — the "mixed" jacks are an ANALOG
 * sum on PCB1, so nothing in the firmware consumes one. Deleted rather than
 * left as a second, subtly-not-bit-equal way to do something no caller wants.) */

/* Simple input mix: signal * gain (+ cv-scaled, placeholder for INPUT MIXER). */
float mixer_input(float signal, float gain);

#endif /* MIXER_H */
