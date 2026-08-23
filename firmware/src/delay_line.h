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
    DL_INTERP_HERMITE,      /* 4-point cubic: better HF, still stateless.
                               MEASURED (moving-tap chorus sweep @96 k, vs exact
                               resampler): -67 dB @5 k, -48 dB @10 k, -35 dB @15 k
                               — effectively transparent under modulation. The
                               once-planned all-pass option measured 30+ dB WORSE
                               while sweeping (recursive-state transients; it is
                               flat-magnitude only for STATIC taps) — rejected.  */
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
 * 1.0 .. len-2). `interp` selects the interpolation kernel.
 *
 * PRECISION NOTE: the read position is computed as a single float32, whose ULP
 * grows with the write-head index — at 2M samples (a ~20 s SDRAM bank @96 kHz)
 * the fraction quantizes to 1/8 sample, at 4M to 1/4. Fine for host tests and
 * short buffers; for SDRAM-sized buffers use dl_read_frac(), which is exact at
 * any length. */
float dl_read(const delay_line_t *d, float delay, dl_interp_t interp);

/* Read a tap (d_int + d_frac) samples behind the write head, d_frac in [0,1).
 * Integer index arithmetic + exact fraction: full interpolation precision at
 * ANY buffer size / head position (use this for the SDRAM engine path).
 * Valid d_int: 1 .. len-2 (linear), 1 .. len-3 (Hermite). */
float dl_read_frac(const delay_line_t *d, uint32_t d_int, float d_frac,
                   dl_interp_t interp);

/* Read at an absolute fractional buffer index (wrapped mod len). */
float dl_read_at(const delay_line_t *d, float index, dl_interp_t interp);

/* Loop/recirc read: a tap `delay` behind the head, but confined to the loop
 * window [loop_start, loop_end] (inclusive-ish, wraps within the window).
 * Used in RECIRC/looper mode. */
float dl_read_loop(const delay_line_t *d, float delay,
                   uint32_t loop_start, uint32_t loop_end, dl_interp_t interp);

/* Loop/recirc read with the exact int+frac tap position (see dl_read_frac).
 * Same window semantics as dl_read_loop. */
float dl_read_loop_frac(const delay_line_t *d, uint32_t d_int, float d_frac,
                        uint32_t loop_start, uint32_t loop_end, dl_interp_t interp);

/* Advance the head one sample within a loop window (RECIRC playback, no write):
 * head++ ; if it passes loop_end it snaps back to loop_start. */
void dl_advance_loop(delay_line_t *d, uint32_t loop_start, uint32_t loop_end);

/* One-time loop-seam splice for a captured window [start,end): rewrite the
 * last `fade` samples so the tail crossfades into the content that leads INTO
 * loop_start — buf[end-fade+i] blends toward buf[start-fade+i] (mod len), so
 * when the read head wraps it lands on start with matching context. Kills the
 * per-pass wrap click (bench + field #9: every tap clicks as its read crosses
 * the seam). Pre-start content is the older recording (or silence on a fresh
 * buffer, which fades out benignly). Call ONCE at capture, from superloop
 * context; the reader starts at `start` so the tail isn't reached before the
 * splice completes. */
void dl_loop_splice(delay_line_t *d, uint32_t start, uint32_t end, uint32_t fade);

/* ---- inlinable read kernel + window mapping (ISR-budget work, 2026-08) -----
 *
 * These are the same arithmetic as read_frac_at_index() / dl_read_loop_frac()
 * above, factored so the ENGINE can inline them into its 8-tap loop instead of
 * paying a call (plus a second nested call) eight times per sample, and so the
 * parts of the window mapping that do not change from tap to tap can be lifted
 * out of that loop.  read_frac_at_index() is now a thin wrapper over
 * dl_stencil(), so there is still exactly ONE copy of the interpolation
 * polynomial to keep in lockstep with dl_read_at() and audio_buffer.c.
 *
 * Everything here is bit-exact with what it replaces — test_golden.c asserts it
 * over a 200k-frame run.  That is the whole point: this file is allowed to get
 * faster, it is not allowed to get different. */

/* The kernel over an already-located CONTIGUOUS stencil: p[0..3] = the samples
 * at buffer indices a0-2, a0-1, a0, a0+1. Split out from dl_stencil_hermite so
 * the window cache (dl_cache.h) can hand it four CCM words instead of four
 * SDRAM words without a second copy of the polynomial existing anywhere. */
static inline float dl_stencil4_hermite(const float *p, float f)
{
    const float x2  = p[0];
    const float x1  = p[1];
    const float x0  = p[2];
    const float xm1 = p[3];
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}

static inline float dl_stencil4_linear(const float *p, float f)
{
    const float x1 = p[1];
    const float x0 = p[2];
    return x0 + (x1 - x0) * f;
}

/* 4-point Hermite (Catmull-Rom) at buffer index a0 with fraction f toward the
 * OLDER sample (delay space: x0 = a0, x1 = a0-1, xm1 = a0+1, x2 = a0-2). */
static inline float dl_stencil_hermite(const float *b, uint32_t len,
                                       uint32_t a0, float f)
{
    /* FAST PATH: when the 4-sample stencil a0-2..a0+1 cannot wrap (the ~always
     * case on a 2M buffer), index directly — the wrap branches cost real cycles
     * at 3M fetches/s, and sequential addressing keeps SDRAM row hits. */
    if (a0 >= 2u && a0 + 1u < len)
        return dl_stencil4_hermite(&b[a0 - 2], f);
    {   /* wrapped stencil: each neighbour is at most one length out of range */
        int32_t i = (int32_t)a0;
        int32_t im1 = i - 1; if (im1 < 0) im1 += (int32_t)len;
        int32_t ip1 = i + 1; if ((uint32_t)ip1 >= len) ip1 -= (int32_t)len;
        int32_t im2 = i - 2; if (im2 < 0) im2 += (int32_t)len;
        const float x0  = b[a0];
        const float x1  = b[(uint32_t)im1];
        const float xm1 = b[(uint32_t)ip1];
        const float x2  = b[(uint32_t)im2];
        const float c0 = x0;
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + c0;
    }
}

static inline float dl_stencil_linear(const float *b, uint32_t len,
                                      uint32_t a0, float f)
{
    if (a0 >= 2u && a0 + 1u < len)
        return dl_stencil4_linear(&b[a0 - 2], f);
    {
        int32_t im1 = (int32_t)a0 - 1; if (im1 < 0) im1 += (int32_t)len;
        const float x0 = b[a0];
        const float x1 = b[(uint32_t)im1];
        return x0 + (x1 - x0) * f;
    }
}

static inline float dl_stencil(const float *b, uint32_t len, uint32_t a0,
                               float f, dl_interp_t interp)
{
    return (interp == DL_INTERP_LINEAR) ? dl_stencil_linear(b, len, a0, f)
                                        : dl_stencil_hermite(b, len, a0, f);
}

/* Read geometry for one frame, shared by all 8 taps.
 *
 * span == 0 selects whole-buffer addressing (WRITE, or a degenerate loop
 * window); otherwise reads are confined to [base, base+span) exactly as
 * dl_read_loop_frac() does it.  `pos` is the head: the write index in the
 * whole-buffer case, the head's offset INSIDE the window in the loop case.
 *
 * Splitting it this way is the point of the exercise: span/base/len/buf are
 * fixed for a whole DMA block, pos moves once per frame, and only d_int moves
 * per tap.  The old code recomputed all of it eight times a sample, including
 * a division that no tap needed. */
typedef struct {
    const float *buf;
    uint32_t     len;
    uint32_t     span;   /* 0 = whole buffer */
    uint32_t     base;   /* loop_start (span != 0 only) */
    uint32_t     pos;    /* head (see above) */
} dl_map_t;

static inline uint32_t dl_map_index(const dl_map_t *m, uint32_t d_int)
{
    if (m->span) {
        uint32_t k = d_int;
        if (k >= m->span) k %= m->span;      /* tap wraps within the window   */
        uint32_t r0 = (k <= m->pos) ? m->pos - k : m->pos + m->span - k;
        uint32_t a0 = m->base + r0;
        if (a0 >= m->len) a0 -= m->len;
        return a0;
    }
    {
        uint32_t k = d_int;
        if (k >= m->len) k %= m->len;
        return (k <= m->pos) ? m->pos - k : m->pos + m->len - k;
    }
}

/* Head position inside the window — the once-per-frame half of the mapping
 * (this is where the per-tap UDIV used to live: `pos` does not depend on i). */
static inline uint32_t dl_map_head(uint32_t wpos, uint32_t len,
                                   uint32_t base, uint32_t span)
{
    uint32_t pos = (base <= wpos) ? wpos - base : wpos + len - base;
    if (pos >= span) pos %= span;
    return pos;
}

/* Optional "vintage" quantizer for the write path: reduce to `bits` (e.g. 12) with
 * triangular dither. Apply to the sample before dl_write() when in vintage mode.
 * `dither` is a value in [-1,1] (e.g. from a cheap PRNG); pass 0 for none. */
float dl_vintage_quantize(float x, int bits, float dither);

#endif /* DELAY_LINE_H */
