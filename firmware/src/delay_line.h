/* delay_line.h — fixed-rate fractional delay line for the 288r community firmware.
 *
 * Core of the rewrite (see firmware/DESIGN.md). Unlike the stock firmware, delay
 * TIME is never changed by retuning the sample clock: the codec runs at a fixed
 * base rate and all time variation is a fractional read offset into this buffer.
 * That is what makes continuous modulation (chorus/flanger/pitch) possible.
 *
 * Samples are normalized float32 in an SDRAM circular buffer (same 4 bytes as the
 * stock int32 layout, so no memory penalty) — interpolation and mixing are then
 * branch-free hardware single-precision float on the Cortex-M4F FPU.
 *
 * The engine is host-testable: point `buf` at ordinary RAM in a unit test.
 */
#ifndef DELAY_LINE_H
#define DELAY_LINE_H

#include <stdint.h>

typedef enum {
    DL_INTERP_LINEAR = 0,   /* 2-point: cheapest, ~0.5 dB HF droop near Nyquist   */
    DL_INTERP_HERMITE,      /* 4-point cubic: better HF, still stateless          */
} dl_interp_t;

typedef struct {
    float   *buf;           /* circular buffer of normalized samples (SDRAM)      */
    uint32_t len;           /* length in samples (arbitrary, not required 2^n)    */
    uint32_t wpos;          /* write head index                                   */
} delay_line_t;

/* Bind a buffer (does not allocate). len>=4. */
void dl_init(delay_line_t *d, float *buf, uint32_t len);

/* Clear the buffer to silence. */
void dl_clear(delay_line_t *d);

/* Push one input sample and advance the write head (call once per sample in). */
void dl_write(delay_line_t *d, float x);

/* Read a tap `delay` samples behind the write head (delay may be fractional,
 * 1.0 .. len-2). `interp` selects the interpolation kernel. */
float dl_read(const delay_line_t *d, float delay, dl_interp_t interp);

/* Read at an absolute fractional buffer index (wrapped mod len). */
float dl_read_at(const delay_line_t *d, float index, dl_interp_t interp);

/* Loop/recirc read: a tap `delay` behind the head, but confined to the loop
 * window [loop_start, loop_end] (inclusive-ish, wraps within the window).
 * Used in RECIRC/looper mode. */
float dl_read_loop(const delay_line_t *d, float delay,
                   uint32_t loop_start, uint32_t loop_end, dl_interp_t interp);

/* Advance the head one sample within a loop window (RECIRC playback, no write):
 * head++ ; if it passes loop_end it snaps back to loop_start. */
void dl_advance_loop(delay_line_t *d, uint32_t loop_start, uint32_t loop_end);

/* Optional "vintage" quantizer for the write path: reduce to `bits` (e.g. 12) with
 * triangular dither. Apply to the sample before dl_write() when in vintage mode.
 * `dither` is a value in [-1,1] (e.g. from a cheap PRNG); pass 0 for none. */
float dl_vintage_quantize(float x, int bits, float dither);

#endif /* DELAY_LINE_H */
