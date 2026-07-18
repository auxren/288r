/* delay_line.c — see delay_line.h. Reconstruction-derived, independently written.
 *
 * Read model: the fractional read position is  r = wpos - delay  (mod len).
 * The straddling integer indices in buffer-index space are i0 = floor(r) and its
 * neighbours; the fraction f = r - i0 is preserved (this is exactly what the stock
 * firmware threw away with vcvt.s32.f32 — see re/patches/patch1_interp.s). */
#include "delay_line.h"

void dl_init(delay_line_t *d, float *buf, uint32_t len)
{
    d->buf = buf;
    d->len = len;
    d->wpos = 0;
}

void dl_clear(delay_line_t *d)
{
    for (uint32_t i = 0; i < d->len; i++) d->buf[i] = 0.0f;
    d->wpos = 0;
}

void dl_write(delay_line_t *d, float x)
{
    d->buf[d->wpos] = x;
    if (++d->wpos >= d->len) d->wpos = 0;
}

/* wrap an index into [0,len) assuming it is at most one length off either side */
static inline uint32_t wrap(int32_t i, uint32_t len)
{
    if (i < 0)            i += (int32_t)len;
    else if ((uint32_t)i >= len) i -= (int32_t)len;
    return (uint32_t)i;
}

float dl_read_at(const delay_line_t *d, float index, dl_interp_t interp)
{
    const uint32_t len = d->len;

    float r = index;
    while (r < 0.0f)         r += (float)len;
    while (r >= (float)len)  r -= (float)len;

    const int32_t i0 = (int32_t)r;      /* floor for r >= 0            */
    const float   f  = r - (float)i0;   /* fractional part in [0,1)    */

    const float *b = d->buf;

    if (interp == DL_INTERP_LINEAR) {
        const float x0 = b[wrap(i0,     len)];
        const float x1 = b[wrap(i0 + 1, len)];
        return x0 + (x1 - x0) * f;
    }

    /* 4-point, 3rd-order Hermite (Catmull-Rom) over x[-1..2] */
    const float xm1 = b[wrap(i0 - 1, len)];
    const float x0  = b[wrap(i0,     len)];
    const float x1  = b[wrap(i0 + 1, len)];
    const float x2  = b[wrap(i0 + 2, len)];

    const float c0 = x0;
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + c0;
}

float dl_read(const delay_line_t *d, float delay, dl_interp_t interp)
{
    return dl_read_at(d, (float)d->wpos - delay, interp);
}

float dl_read_loop(const delay_line_t *d, float delay,
                   uint32_t loop_start, uint32_t loop_end, dl_interp_t interp)
{
    /* window length in samples (loop_end treated as one-past when < start) */
    float span = (loop_end >= loop_start)
                 ? (float)(loop_end - loop_start)
                 : (float)(d->len - (loop_start - loop_end));
    if (span < 1.0f) return dl_read(d, delay, interp);   /* degenerate window */

    float pos = (float)d->wpos - (float)loop_start;      /* head within window */
    while (pos < 0.0f)    pos += (float)d->len;
    float r = pos - delay;                                /* tap within window  */
    while (r < 0.0f)      r += span;
    while (r >= span)     r -= span;
    return dl_read_at(d, (float)loop_start + r, interp);
}

void dl_advance_loop(delay_line_t *d, uint32_t loop_start, uint32_t loop_end)
{
    uint32_t p = d->wpos + 1;
    if (p >= d->len) p = 0;
    if (p == loop_end) p = loop_start;   /* snap back at window end */
    d->wpos = p;
}

float dl_vintage_quantize(float x, int bits, float dither)
{
    const float steps = (float)(1 << (bits - 1));   /* signed full-scale steps */
    float q = x * steps + dither;
    /* round-to-nearest via truncation of +/-0.5 (no rintf dependency)         */
    q = (q >= 0.0f) ? (float)(int32_t)(q + 0.5f) : (float)(int32_t)(q - 0.5f);
    return q / steps;
}

/* ---- exact int+frac read path (SDRAM-size safe) ----------------------------
 *
 * dl_read()'s float32 position loses fractional resolution as wpos grows (ULP
 * = 1/8 sample at 2M, 1/4 at 4M — i.e. exactly the smooth-modulation artifact
 * this engine exists to kill, reintroduced by the 8 MB SDRAM). Here the integer
 * part of the tap is pure uint32 arithmetic and the fraction is carried
 * separately, so precision is independent of buffer size and head position.
 *
 * The interpolation is evaluated in DELAY space: x0 = sample at integer delay
 * k, x1 at k+1 (one sample older), fraction f toward x1, outer Hermite
 * neighbours at k-1 / k+2. Catmull-Rom is direction-symmetric, so this is the
 * same polynomial dl_read_at evaluates in index space. Kernel kept in lockstep
 * with dl_read_at / audio_buffer.c. */
static float read_frac_at_index(const float *b, uint32_t len, uint32_t a0,
                                float f, dl_interp_t interp)
{
    /* a0 = buffer index of the sample at integer delay k (delay k+1 is a0-1) */
    const float x0 = b[a0];
    const float x1 = b[wrap((int32_t)a0 - 1, len)];

    if (interp == DL_INTERP_LINEAR)
        return x0 + (x1 - x0) * f;

    const float xm1 = b[wrap((int32_t)a0 + 1, len)];   /* delay k-1 (newer)  */
    const float x2  = b[wrap((int32_t)a0 - 2, len)];   /* delay k+2 (older)  */

    const float c0 = x0;
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + c0;
}

float dl_read_frac(const delay_line_t *d, uint32_t d_int, float d_frac,
                   dl_interp_t interp)
{
    const uint32_t len = d->len;
    if (d_int >= len) d_int %= len;
    const uint32_t a0 = (d_int <= d->wpos) ? d->wpos - d_int
                                           : d->wpos + len - d_int;
    return read_frac_at_index(d->buf, len, a0, d_frac, interp);
}

float dl_read_loop_frac(const delay_line_t *d, uint32_t d_int, float d_frac,
                        uint32_t loop_start, uint32_t loop_end, dl_interp_t interp)
{
    const uint32_t len = d->len;
    uint32_t span = (loop_end >= loop_start) ? loop_end - loop_start
                                             : len - (loop_start - loop_end);
    if (span < 1) return dl_read_frac(d, d_int, d_frac, interp);

    uint32_t pos = (loop_start <= d->wpos) ? d->wpos - loop_start
                                           : d->wpos + len - loop_start;
    if (pos >= span) pos %= span;            /* head inside the window       */

    uint32_t k = d_int;
    if (k >= span) k %= span;                /* tap wraps within the window  */
    uint32_t r0 = (k <= pos) ? pos - k : pos + span - k;

    uint32_t a0 = loop_start + r0;           /* absolute buffer index        */
    if (a0 >= len) a0 -= len;
    /* neighbours fetch whole-buffer adjacent samples, matching dl_read_loop's
     * seam behaviour (faithful-clone question — see notes) */
    return read_frac_at_index(d->buf, len, a0, d_frac, interp);
}
