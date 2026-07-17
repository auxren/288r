/* saturation.h — soft-clip "analog voice" waveshaper (vintage character).
 *
 * The third leg of the 288r's vintage character, alongside the bit-crush
 * (audio_buffer) and the bandwidth limit (bwlimit): a smooth, odd-symmetric soft
 * clipper that adds odd harmonics and rounds transients. Uses the rational shaper
 * w(u) = u/(1+|u|) — unity slope at zero (clean at low level), asymptotically
 * bounded to (-1,1), odd (only odd harmonics), and no libm. `drive` sets how hard,
 * `mix` blends dry->wet. Stateless. Wire on the engine's output (or per tap).
 */
#ifndef SATURATION_H
#define SATURATION_H

typedef struct {
    float drive;   /* input gain into the shaper; 1 = gentle, higher = more grit */
    float mix;     /* 0 = clean (bypass), 1 = fully shaped                        */
} saturation_t;

void  sat_init(saturation_t *s, float drive, float mix);
void  sat_set(saturation_t *s, float drive, float mix);   /* clamps to sane ranges */
float sat_process(const saturation_t *s, float x);

#endif /* SATURATION_H */
