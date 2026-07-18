/* pitch_shift.h — crossfaded-tap ("H910-style") pitch shifter over delay_line.
 *
 * A read mode on the fixed-rate buffer: two fractional taps a half-window apart,
 * their delay ramped so the read rate = pitch ratio, crossfaded so the ramp wrap
 * is silent. Subtle ratios (≈1.0) give detune/chorus; large ratios give shift.
 * Feed it a delay_line that something else is writing (e.g. the main engine).
 */
#ifndef PITCH_SHIFT_H
#define PITCH_SHIFT_H

#include "delay_line.h"

typedef struct {
    float ratio;    /* pitch ratio: 2=+1oct, 0.5=-1oct, 1=unity (see gotcha)     */
    float window;   /* crossfade window length W in samples (grain length)       */
    float base;     /* base delay offset in samples; keeps both taps in range    */
    float phase;    /* fracA accumulator in [0,1)                                */
    /* --- de-glitch (H949-style correlation-aligned splices) ---------------- */
    float off[2];    /* per-tap splice offsets (>=0, deeper into the past)       */
    volatile float pend_off;  /* offset chosen by ps_service for the NEXT wrap   */
    volatile int   pend_tap;  /* which tap it is for (-1 = none). PROTOCOL:
                                 service writes pend_off, BARRIER, then pend_tap;
                                 the ISR reads pend_tap first and consumes both.  */
    int   next_tap;  /* which tap wraps next (0=A at phase 1->0, 1=B at 0.5)     */
} pitchshift_t;

/* window: W in samples (e.g. 30 ms * fs). base: >= 2, and base + W <= len - 3. */
void  ps_init(pitchshift_t *p, float window, float base);
void  ps_set_ratio(pitchshift_t *p, float ratio);   /* clamp to a sane span      */
void  ps_reset(pitchshift_t *p);                     /* phase = 0                 */

/* One output sample. `d` is the (externally written) delay line. */
float ps_process(pitchshift_t *p, const delay_line_t *d, dl_interp_t interp);

/* De-glitch service — call from the MAIN LOOP (not the ISR): when a tap wrap is
 * imminent it runs a correlation search (a few hundred k MACs, ~1-2 ms — far too
 * heavy for the ISR, trivial in the superloop) and posts the splice offset the
 * ISR consumes at the wrap. Without it, splices land at offset 0 = the plain
 * crossfaded shifter (graceful fallback). */
void ps_service(pitchshift_t *p, const delay_line_t *d);

#endif /* PITCH_SHIFT_H */
