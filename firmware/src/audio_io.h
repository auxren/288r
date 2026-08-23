/* audio_io.h — block processing between the CS42888 TDM stream and the engine.
 *
 * On hardware the SAI2 DMA hands us a block of TDM frames: 4 ADC-in slots + 8 DAC-out
 * slots per frame (CS42888 = 4-in / 8-out). This module converts int24↔float, runs
 * engine_process_multi() per frame, and lays the 8 tap outputs into the 8 DAC slots.
 * Host-testable: pass ordinary int32 buffers.
 *
 * Word format assumed **left-justified 24-bit in int32** — TODO(bench): verify against
 * the codec-init capture; the shift constants below are the single place to change.
 */
#ifndef AUDIO_IO_H
#define AUDIO_IO_H

#include <stdint.h>
#include "engine.h"     /* engine_t, NUM_TAPS */

#define ADC_SLOTS 4u
#define DAC_SLOTS ((unsigned)NUM_TAPS)   /* 8 */

/* Process `frames` TDM frames.
 *   in  : frames * ADC_SLOTS int32, interleaved (24-bit left-justified in int32)
 *   out : frames * DAC_SLOTS int32, interleaved (the 8 per-tap outputs)
 *   in_slot   : which ADC slot carries the audio input (0..ADC_SLOTS-1)
 *   time_raw01: TIME control in [0,1] (from the panel/CV layer), applied to the block
 * Output is clamped to full-scale so a DSP fault can't wrap to the opposite rail. */
void audio_io_block(engine_t *e, const int32_t *in, int32_t *out,
                    unsigned frames, unsigned in_slot, float time_raw01);

/* Exposed for tests: the codec word conversions (left-justified 24-bit <-> float). */
float   audio_in_to_f(int32_t codec_word);
int32_t audio_f_to_out(float x);

/* Was this output word produced by a sample at or beyond full scale? The soft
 * knee maps |x| = 1.0 to exactly 0.875 FS, so testing the WORD against that
 * point is the same predicate as |x| >= 1.0 on the float — but it is an
 * integer compare on a value the caller already has, instead of two float
 * compares (each a VCMPE plus an FPSCR read on an M4F) per tap per sample.
 * Diagnostic use only (the clip counter): exact to the 24-bit LSB. */
#define AUDIO_OVERRANGE_WORD 7340031   /* 0.875 * (2^23 - 1), truncated */
static inline int audio_word_overrange(int32_t w24)
{
    int32_t s = (w24 << 8) >> 8;                  /* sign-extend 24-bit */
    return (s >= AUDIO_OVERRANGE_WORD) | (s <= -AUDIO_OVERRANGE_WORD);
}

#endif /* AUDIO_IO_H */
