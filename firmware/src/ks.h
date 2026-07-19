/* ks.h — Karplus-Strong string bank: 8 strings on the 8 tap channels.
 *
 * Each string is a short fractional-delay loop with internal feedback and the
 * classic two-point damping average — the one structure that CANNOT be patched
 * externally (the codec round trip is ~1 ms; string loops must close at zero
 * extra latency inside the ISR). This is a NEW engine mode, gesture-entered;
 * the delay engine's no-internal-feedback rule is untouched in delay modes.
 *
 * Tuning (owner-designed): the tap PHASES are the chord's intervals
 * (base period = KS_BASE_PERIOD * phase/160); c.v. in transposes the whole
 * chord at 1.2 V/oct, always and directly (no attenuverter — attenuation
 * would break tracking). The multiplier knob maps to damping/brightness.
 * Excitation: the module input is injected into every string. Rings live in
 * SRAM (fast, DMA-free).
 *
 * Host-tested by test/test_ks.c.
 */
#ifndef KS_H
#define KS_H

#include <stdint.h>

#define KS_STRINGS     8
#define KS_RING_LEN    2400          /* 40 Hz floor @96k                     */
#define KS_MIN_PERIOD  64.0f         /* 1.5 kHz ceiling                      */
#define KS_BASE_PERIOD 1920.0f       /* phase 160 string @ 0 V = 50 Hz @96k  */

typedef struct {
    float ring[KS_STRINGS][KS_RING_LEN];
    float period[KS_STRINGS];        /* current (slewed) period, samples     */
    float target[KS_STRINGS];        /* target period (set by ks_set_period)  */
    uint32_t w[KS_STRINGS];          /* per-string write index               */
    float fb;                        /* loop gain (<1)                       */
    float damp;                      /* 0..1: two-point-average mix          */
} ks_t;

void ks_init(ks_t *k, float fb, float damp);
/* set string i's target period (clamped to the ring); slewed inside process */
void ks_set_period(ks_t *k, int i, float period);
/* one sample: excite all strings with x, return per-string outputs in chan */
void ks_process(ks_t *k, float x, float chan[KS_STRINGS]);

#endif /* KS_H */
