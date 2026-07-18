/* pitch_shift.c — see pitch_shift.h. Standard delay-line pitch shift; clean-room. */
#include "pitch_shift.h"
#include <math.h>

/* the freestanding ARM build's math.h does not declare sqrtf (sinf/fabsf come
 * through it fine); the definition is fast_math.c's VSQRT-backed sqrtf. A
 * redundant identical declaration is harmless on the hosted build. */
float sqrtf(float);

#define PS_PI      3.14159265358979f
#define PS_UNITY_EPS 1.0e-4f          /* |1-ratio| below this -> treat as bypass  */

/* Exact-position read at a float DISTANCE: split into int+frac BEFORE the
 * write pointer enters the math. dl_read() computes (float)wpos - delay, which
 * quantizes to 1/4 sample once wpos is millions of samples into the SDRAM
 * buffer — periodic phase jitter on this voice's continuously-ramping taps,
 * and it re-quantizes the sub-sample splice offsets the correlator refines.
 * The distance itself is < ~16k samples, so this split is exact to ~2^-10. */
static inline float ps_read(const delay_line_t *d, float dist, dl_interp_t interp)
{
    uint32_t di = (uint32_t)dist;
    return dl_read_frac(d, di, dist - (float)di, interp);
}

void ps_init(pitchshift_t *p, float window, float base)
{
    p->ratio  = 1.0f;
    p->window = window;
    p->base   = base;
    p->phase  = 0.0f;
    p->off[0] = 0.0f;
    p->off[1] = 0.0f;
    /* coherence prior = 1 (amplitude-complementary): before the first splice
     * the two taps read overlapping content and ARE coherent; 0 here would
     * put a +3 dB power-law bump on tones until the first service pass. */
    p->rho[0] = 1.0f;
    p->rho[1] = 1.0f;
    p->pend_rho = 1.0f;
    p->pend_off = 0.0f;
    p->pend_tap = -1;
    p->next_tap = 0;
}

void ps_set_ratio(pitchshift_t *p, float ratio)
{
    /* keep the per-sample delay ramp well under 1 sample so interpolation stays
       valid: |1-ratio| <= W is the hard limit; clamp conservatively.           */
    if (ratio < 0.25f) ratio = 0.25f;   /* -2 oct */
    if (ratio > 4.0f)  ratio = 4.0f;    /* +2 oct */
    p->ratio = ratio;
}

void ps_reset(pitchshift_t *p)
{
    p->phase = 0.0f;
    p->off[0] = p->off[1] = 0.0f;
    p->pend_tap = -1;
    p->next_tap = 0;
}

/* ---- de-glitch: correlation-aligned splices (H949-style) -------------------
 * When tap T is about to wrap, its new read position should be WAVEFORM-ALIGNED
 * with the other tap so the crossfade sums coherent material instead of combing.
 * Ranking = acc*|acc|/ein == sign-preserving (NCC)^2 up to the lag-independent
 * outgoing energy (adversarial-verify blocker #2: plain acc/ein is a least-
 * squares gain BIASED TOWARD QUIET WINDOWS — it dug 30 dB holes in enveloped
 * material). Guards: incoming energy >= 1% of outgoing; best NCC >= 0.5, else
 * fall back to offset 0 (= the plain crossfade). Runs in the MAIN LOOP only.
 * Range note: reads reach base + W + MAXLAG + N (~ +1600) — callers must keep
 * that inside the buffer (trivial at SDRAM sizes). Honest low-frequency reach
 * with N=768 correlation span: ~125 Hz. */
#define PS_DEGLITCH_N       768
#define PS_DEGLITCH_MAXLAG  1200
#define PS_DEGLITCH_LOOKA   0.06f     /* start searching within 6% of the wrap */

void ps_service(pitchshift_t *p, const delay_line_t *d)
{
    if (p->pend_tap >= 0) return;                 /* already prepared        */
    if (fabsf(1.0f - p->ratio) < PS_UNITY_EPS) return;  /* bypass: no wraps  */
    float step = (1.0f - p->ratio) / p->window;

    /* derive which tap wraps next FROM PHASE + DIRECTION every call (the
     * stored alternation state desyncs when the ratio crosses unity — the
     * chorus regime; adversarial-verify finding). A wraps at the 0/1 boundary,
     * B at 0.5. Whichever boundary is nearer in the direction of travel. */
    float ph = p->phase;
    float dA, dB;
    if (step > 0.0f) { dA = 1.0f - ph; dB = (ph < 0.5f) ? 0.5f - ph : 1.5f - ph; }
    else             { dA = ph;        dB = (ph >= 0.5f) ? ph - 0.5f : ph + 0.5f; }
    int   tap  = (dA <= dB) ? 0 : 1;
    float dist = (dA <= dB) ? dA : dB;
    p->next_tap = tap;
    float astep = (step > 0.0f) ? step : -step;
    if (dist > PS_DEGLITCH_LOOKA) return;          /* not imminent yet       */
    if (dist < 8.0f * astep) return;               /* too late — skip safely */

    /* correlate: the incoming tap enters at frac 0 for DOWN-shifts (delay base)
     * but frac 1 for UP-shifts (delay base+W; phase runs backward). */
    const float dIn  = (step > 0.0f) ? p->base : p->base + p->window;
    const float dOut = p->base + 0.5f * p->window + p->off[tap ^ 1];

    /* outgoing-window energy once (lag-independent) for the guards */
    float eb = 0.0f;
    for (int k = 0; k < PS_DEGLITCH_N; k += 2) {
        float b = ps_read(d, dOut + (float)k, DL_INTERP_LINEAR);
        eb += b * b;
    }
    if (eb < 1.0e-7f) return;                      /* silence: keep offset 0 */

    float best = 0.0f; int bestlag = 0;
    for (int lag = 0; lag < PS_DEGLITCH_MAXLAG; lag += 4) {   /* coarse grid */
        float acc = 0.0f, ein = 0.0f;
        for (int k = 0; k < PS_DEGLITCH_N; k += 2) {          /* decimate x2 */
            float a = ps_read(d, dIn + (float)lag + (float)k, DL_INTERP_LINEAR);
            float b = ps_read(d, dOut + (float)k, DL_INTERP_LINEAR);
            acc += a * b;
            ein += a * a;
        }
        if (ein < 0.01f * eb) continue;            /* too quiet to trust     */
        float score = acc * fabsf(acc) / (ein + 1.0e-9f);   /* sign-kept NCC^2 * eb */
        if (score > best) { best = score; bestlag = lag; }
    }
    /* accept only a real match: NCC^2 >= 0.25  <=>  score >= 0.25 * eb */
    if (best < 0.25f * eb) { bestlag = 0; }
    float bestoff = (float)bestlag;
    if (best >= 0.25f * eb) {
        /* fine search +/-3 around the coarse winner, keeping neighbours for a
         * parabolic sub-sample refinement (the interpolated dl_read makes a
         * FRACTIONAL splice offset directly usable) */
        float sc[7]; int flag = bestlag; float fbest = -1.0e30f;
        for (int j = 0; j < 7; ++j) {
            int lag = bestlag - 3 + j;
            sc[j] = -1.0e30f;
            if (lag < 0 || lag >= PS_DEGLITCH_MAXLAG) continue;
            float acc = 0.0f, ein = 0.0f;
            for (int k = 0; k < PS_DEGLITCH_N; k += 2) {
                float a = ps_read(d, dIn + (float)lag + (float)k, DL_INTERP_LINEAR);
                float b = ps_read(d, dOut + (float)k, DL_INTERP_LINEAR);
                acc += a * b;
                ein += a * a;
            }
            if (ein < 0.01f * eb) continue;
            sc[j] = acc * fabsf(acc) / (ein + 1.0e-9f);
            if (sc[j] > fbest) { fbest = sc[j]; flag = lag; }
        }
        int j0 = flag - (bestlag - 3);
        bestoff = (float)flag;
        if (j0 >= 1 && j0 <= 5 && sc[j0-1] > -1.0e29f && sc[j0+1] > -1.0e29f) {
            float den = sc[j0-1] - 2.0f * sc[j0] + sc[j0+1];
            if (den < -1.0e-12f) {                 /* concave peak            */
                float dlt = 0.5f * (sc[j0-1] - sc[j0+1]) / den;
                if (dlt > -1.0f && dlt < 1.0f) bestoff += dlt;   /* sub-sample */
            }
        }
        if (bestoff < 0.0f) bestoff = 0.0f;
    }
    /* publish only if the wrap geometry is still the one we searched for
     * (the ISR may have consumed a wrap mid-search at large ratios) */
    if (p->next_tap != tap || p->pend_tap >= 0) return;
    /* coherence of the chosen splice (NCC in 0..1): best = NCC^2 * eb */
    float ncc2 = best / eb;
    if (ncc2 < 0.0f) ncc2 = 0.0f;
    if (ncc2 > 1.0f) ncc2 = 1.0f;
    p->pend_rho = sqrtf(ncc2);
    p->pend_off = bestoff;
    __asm volatile ("" ::: "memory");              /* order: rho/off BEFORE tap */
    p->pend_tap = tap;
}

float ps_process(pitchshift_t *p, const delay_line_t *d, dl_interp_t interp)
{
    const float W = p->window;

    /* Near unity the ramp is frozen and two static taps would comb-filter;
       collapse to a single centered tap so unity is a clean delayed bypass.    */
    if (fabsf(1.0f - p->ratio) < PS_UNITY_EPS) {
        p->pend_tap = -1;                    /* discard stale splice offsets */
        return ps_read(d, p->base + 0.5f * W, interp);
    }

    const float fracA = p->phase;
    const float fracB = (fracA >= 0.5f) ? fracA - 0.5f : fracA + 0.5f;

    const float sA = sinf(PS_PI * fracA);
    const float wA = sA * sA;           /* sin^2(pi*fracA)          */
    const float wB = 1.0f - wA;         /* = sin^2(pi*fracB) exactly */

    /* COHERENCE-ADAPTIVE fade law (fixes grain-rate AM, owner report):
     * aligned grains (rho~1) sum coherently -> amplitude-complementary
     * weights (wA+wB=1) are flat; UNALIGNED grains (rho~0) add in power ->
     * amplitude-complementary dips -3 dB mid-fade. Blend toward
     * power-complementary (sqrt) weights as coherence falls. */
    const float c  = 0.5f * (p->rho[0] + p->rho[1]);
    const float gA = wA + (1.0f - c) * (sqrtf(wA) - wA);
    const float gB = wB + (1.0f - c) * (sqrtf(wB) - wB);

    const float out = gA * ps_read(d, p->base + fracA * W + p->off[0], interp)
                    + gB * ps_read(d, p->base + fracB * W + p->off[1], interp);

    /* advance: delay changes by (1 - ratio) samples per output sample          */
    float ph = p->phase + (1.0f - p->ratio) / W;
    /* tap wraps: consume the de-glitch offset the service prepared (0 if none).
       A wraps when phase crosses 1 (or 0 going down); B when phase crosses 0.5. */
    if (ph >= 1.0f || ph < 0.0f) {
        p->off[0] = (p->pend_tap == 0) ? p->pend_off : 0.0f;
        p->rho[0] = (p->pend_tap == 0) ? p->pend_rho : 0.0f;
        p->pend_tap = -1; p->next_tap = 1;
    } else {
        int sideNew = (ph >= 0.5f), sideOld = (p->phase >= 0.5f);
        if (sideNew != sideOld) {                    /* B's frac wrapped */
            p->off[1] = (p->pend_tap == 1) ? p->pend_off : 0.0f;
            p->rho[1] = (p->pend_tap == 1) ? p->pend_rho : 0.0f;
            p->pend_tap = -1; p->next_tap = 0;
        }
    }
    while (ph >= 1.0f) ph -= 1.0f;
    while (ph <  0.0f) ph += 1.0f;
    p->phase = ph;

    return out;
}
