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
