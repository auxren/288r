/* bwlimit.h — one-pole low-pass for the config-DIP sw2 "bandwidth limit" feature.
 *
 * Stock sw2 "limits frequency range to 11025 Hz". This is the faithful first cut:
 * a single-pole RC low-pass on the record path, so the delay memory (and anything
 * it recirculates) is band-limited. Single-precision and NO libm — the coefficient
 * is the closed-form RC one-pole  a = w/(1+w),  w = 2*pi*fc/fs  (no expf) — so it
 * cross-compiles for the freestanding STM32F429 target. cutoff <= 0 or >= fs/2
 * selects bypass (a = 1 -> y = x exactly). A steeper brick-wall/decimating limit is
 * a later refinement ([BENCH] against the stock character); 6 dB/oct is the start.
 */
#ifndef BWLIMIT_H
#define BWLIMIT_H

typedef struct {
    float coeff;   /* one-pole coefficient in (0,1]; 1 = bypass */
    float state;   /* filter memory */
} bwlimit_t;

/* cutoff_hz <= 0 or >= fs/2 => bypass. */
void  bw_init(bwlimit_t *b, float fs, float cutoff_hz);
float bw_process(bwlimit_t *b, float x);

#endif /* BWLIMIT_H */
