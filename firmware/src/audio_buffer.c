/* audio_buffer.c — see audio_buffer.h. int16/int32 storage under a float delay line.
 *
 * Conversion scales (store = load, so full-scale round-trips):
 *   I16: +/-1.0 <-> +/-32767            (16-bit)
 *   I32: +/-1.0 <-> +/-2^30 (1073741824) — exact as float, safely < INT32_MAX, and
 *        30-bit precision is well beyond the 24-bit source (i.e. "24-bit clean").
 * The interpolation kernel mirrors delay_line.c's dl_read_at (kept in lockstep). */
#include "audio_buffer.h"

#define AB_I16_SCALE  32767.0f
#define AB_I32_SCALE  1073741824.0f     /* 2^30 */

void ab_init(audio_buffer_t *ab, void *buf, uint32_t bytes, ab_format_t fmt)
{
    ab->buf = buf;
    ab->fmt = fmt;
    ab->len = bytes / ((fmt == AB_FMT_I16) ? 2u : 4u);
    ab->wpos = 0;
    ab->vintage_bits = 0;
}

void ab_clear(audio_buffer_t *ab)
{
    if (ab->fmt == AB_FMT_I16) {
        int16_t *b = (int16_t *)ab->buf;
        for (uint32_t i = 0; i < ab->len; i++) b[i] = 0;
    } else {
        int32_t *b = (int32_t *)ab->buf;
        for (uint32_t i = 0; i < ab->len; i++) b[i] = 0;
    }
    ab->wpos = 0;
}

void ab_set_vintage(audio_buffer_t *ab, int bits) { ab->vintage_bits = bits; }

uint32_t ab_capacity_samples(const audio_buffer_t *ab) { return ab->len; }

/* --- sample store/load at an absolute (already wrapped) index --------------- */

static inline float ab_load(const audio_buffer_t *ab, uint32_t idx)
{
    if (ab->fmt == AB_FMT_I16)
        return (float)((const int16_t *)ab->buf)[idx] * (1.0f / AB_I16_SCALE);
    return (float)((const int32_t *)ab->buf)[idx] * (1.0f / AB_I32_SCALE);
}

static inline void ab_store(audio_buffer_t *ab, uint32_t idx, float x)
{
    if (x >  1.0f) x =  1.0f;            /* clamp before scaling */
    else if (x < -1.0f) x = -1.0f;

    /* optional vintage bit-crush (dither-free here; the engine can dither upstream) */
    if (ab->vintage_bits > 0 && ab->vintage_bits < 24) {
        float steps = (float)(1 << (ab->vintage_bits - 1));
        float q = x * steps;
        q = (q >= 0.0f) ? (float)(int32_t)(q + 0.5f) : (float)(int32_t)(q - 0.5f);
        x = q / steps;
    }

    if (ab->fmt == AB_FMT_I16) {
        float s = x * AB_I16_SCALE;
        int32_t q = (int32_t)((s >= 0.0f) ? (s + 0.5f) : (s - 0.5f));
        ((int16_t *)ab->buf)[idx] = (int16_t)q;
    } else {
        float s = x * AB_I32_SCALE;
        int32_t q = (int32_t)((s >= 0.0f) ? (s + 0.5f) : (s - 0.5f));
        ((int32_t *)ab->buf)[idx] = q;
    }
}

void ab_write(audio_buffer_t *ab, float x)
{
    ab_store(ab, ab->wpos, x);
    if (++ab->wpos >= ab->len) ab->wpos = 0;
}

/* wrap an index into [0,len) assuming it is at most one length off either side */
static inline uint32_t wrap(int32_t i, uint32_t len)
{
    if (i < 0)                    i += (int32_t)len;
    else if ((uint32_t)i >= len)  i -= (int32_t)len;
    return (uint32_t)i;
}

float ab_read_at(const audio_buffer_t *ab, float index, dl_interp_t interp)
{
    const uint32_t len = ab->len;

    float r = index;
    while (r < 0.0f)        r += (float)len;
    while (r >= (float)len) r -= (float)len;

    const int32_t i0 = (int32_t)r;
    const float   f  = r - (float)i0;

    if (interp == DL_INTERP_LINEAR) {
        const float x0 = ab_load(ab, wrap(i0,     len));
        const float x1 = ab_load(ab, wrap(i0 + 1, len));
        return x0 + (x1 - x0) * f;
    }

    /* 4-point 3rd-order Hermite (Catmull-Rom) — same kernel as delay_line.c */
    const float xm1 = ab_load(ab, wrap(i0 - 1, len));
    const float x0  = ab_load(ab, wrap(i0,     len));
    const float x1  = ab_load(ab, wrap(i0 + 1, len));
    const float x2  = ab_load(ab, wrap(i0 + 2, len));

    const float c0 = x0;
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + c0;
}

float ab_read(const audio_buffer_t *ab, float delay, dl_interp_t interp)
{
    return ab_read_at(ab, (float)ab->wpos - delay, interp);
}

float ab_read_loop(const audio_buffer_t *ab, float delay,
                   uint32_t loop_start, uint32_t loop_end, dl_interp_t interp)
{
    float span = (loop_end >= loop_start)
                 ? (float)(loop_end - loop_start)
                 : (float)(ab->len - (loop_start - loop_end));
    if (span < 1.0f) return ab_read(ab, delay, interp);

    float pos = (float)ab->wpos - (float)loop_start;
    while (pos < 0.0f)  pos += (float)ab->len;
    float r = pos - delay;
    while (r < 0.0f)    r += span;
    while (r >= span)   r -= span;
    return ab_read_at(ab, (float)loop_start + r, interp);
}

void ab_advance_loop(audio_buffer_t *ab, uint32_t loop_start, uint32_t loop_end)
{
    uint32_t p = ab->wpos + 1;
    if (p >= ab->len) p = 0;
    if (p == loop_end) p = loop_start;
    ab->wpos = p;
}
