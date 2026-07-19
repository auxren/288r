/* pitch_taps.h — per-tap delayed copies of the pitch voice (stock-faithful
 * transposed multitap).
 *
 * The stock's pitch mode sweeps the read base while the taps keep their
 * spacings: tap N = the SHIFTED signal at tap N's delay time — a pitched echo
 * pattern, not one voice on eight sliders. This module provides that: the
 * voice's output history lives in a ring (a 1 MB / 2.73 s slice of SDRAM
 * carved from the main delay buffer), and each tap channel reads it back at
 * its own current tap delay (linear interpolation; delays beyond the ring
 * clamp to its depth — reachable only in x10-extend corner cases).
 *
 * Host-tested by test/test_pitch_taps.c.
 */
#ifndef PITCH_TAPS_H
#define PITCH_TAPS_H

#include <stdint.h>

typedef struct {
    float   *buf;
    uint32_t mask;     /* len - 1; len must be a power of two */
    uint32_t w;        /* write index (monotonic, masked on access) */
} ptaps_t;

/* len MUST be a power of two (masked ring). Buffer is NOT cleared here on
 * principle (SDRAM may be large); call pt_clear once at init. */
void  pt_init(ptaps_t *p, float *buf, uint32_t len);
void  pt_clear(ptaps_t *p);
void  pt_write(ptaps_t *p, float y);
/* read the voice d_int+frac samples ago (linear interp). Delays are clamped
 * to the ring depth; d_int==0 reads the most recent sample. */
float pt_read(const ptaps_t *p, uint32_t d_int, float frac);

#endif /* PITCH_TAPS_H */
