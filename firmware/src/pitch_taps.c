/* pitch_taps.c — see pitch_taps.h. */
#include "pitch_taps.h"

void pt_init(ptaps_t *p, float *buf, uint32_t len)
{
    p->buf  = buf;
    p->mask = len - 1u;
    p->w    = 0u;
}

void pt_clear(ptaps_t *p)
{
    for (uint32_t i = 0; i <= p->mask; i++) p->buf[i] = 0.0f;
    p->w = 0u;
}

void pt_write(ptaps_t *p, float y)
{
    p->buf[p->w & p->mask] = y;
    p->w++;
}

float pt_read(const ptaps_t *p, uint32_t d_int, float frac)
{
    /* clamp to ring depth (leave room for the interp neighbour and keep off
     * the freshly-written sample) */
    const uint32_t maxd = p->mask - 1u;
    if (d_int > maxd) { d_int = maxd; frac = 0.0f; }
    /* most recent sample is at w-1; delay d reads w-1-d */
    uint32_t a0 = (p->w - 1u - d_int) & p->mask;
    uint32_t a1 = (a0 - 1u) & p->mask;          /* one sample older */
    float x0 = p->buf[a0], x1 = p->buf[a1];
    return x0 + frac * (x1 - x0);
}
