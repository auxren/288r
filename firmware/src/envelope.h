/* envelope.h — one-pole envelope follower (AUTO CONTROL building block).
 *
 * The stock firmware runs three running-average envelope followers (256-tap and
 * 64-tap moving sums: envfollow_insert_ringbuf_A/B/C, sub_3228/32c0/3358) feeding
 * AUTO CONTROL. Those are equivalent to a one-pole low-pass on the rectified signal
 * but far cheaper — DESIGN.md calls for this swap. Rectify + asymmetric one-pole
 * (independent attack/release) is the standard, allocation-free form.
 *
 * How the follower output drives AUTO CONTROL (its exact scaling/target) is a
 * bench-calibration item; this module is the reusable primitive.
 */
#ifndef ENVELOPE_H
#define ENVELOPE_H

typedef struct {
    float env;   /* current envelope value        */
    float atk;   /* attack coeff, (0,1]           */
    float rel;   /* release coeff, (0,1]          */
} env_follower_t;

/* atk/rel are one-pole coefficients in (0,1]; larger = faster. */
void  env_init(env_follower_t *e, float atk, float rel);

/* Feed one sample; returns the updated (rectified, smoothed) envelope. */
float env_process(env_follower_t *e, float x);

/* Current envelope without advancing. */
static inline float env_value(const env_follower_t *e) { return e->env; }

#endif /* ENVELOPE_H */
