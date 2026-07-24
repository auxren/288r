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
static inline float ps_read(const pitchshift_t *p, const delay_line_t *d,
                            float dist, dl_interp_t interp)
{
    if (p->lp_span) {
        /* looper (#19): window-map the read — dl_read_loop_frac wraps the
         * distance inside [lp_start, lp_end) whose seam content is spliced +
         * guard-sampled at capture, so grains cross the wrap continuously. */
        uint32_t di = (uint32_t)dist;
        return dl_read_loop_frac(d, di, dist - (float)di,
                                 p->lp_start, p->lp_end, interp);
    }
    uint32_t di = (uint32_t)dist;
    return dl_read_frac(d, di, dist - (float)di, interp);
}

#include "aa_tables.h"

/* Band-limited read for UP-shifts (anti-aliasing): reading the buffer faster
 * than it was written is decimation — content above Nyquist/ratio folds into
 * the output, and Hermite does not band-limit. Polyphase Kaiser-sinc whose
 * cutoff tracks the ratio band (aa_tables.h), linear phase interpolation
 * between rows. Engaged only when ratio > ~1; down-shifts keep the exact
 * Hermite path untouched. Reads span delays [dist-8, dist+7]: causal because
 * dist >= base (256).
 *
 * ISR-budget design (adversarial-verify blocker: 32 raw SDRAM loads/sample +
 * flash-coefficient thrash = ~2x the idle headroom): samples come from a
 * per-grain 32-sample streaming cache (the grain's absolute read position
 * only moves FORWARD, by `ratio`/sample, so a refill every ~16/ratio samples
 * amortizes to ~2*ratio loads/sample); coefficients come from platform-
 * published fast rows (CCM) when the band matches, flash otherwise. */
static float ps_read_bl(pitchshift_t *p, const delay_line_t *d, int g,
                        float dist, const float (*rows)[AA_TAPS])
{
    uint32_t di = (uint32_t)dist;
    float    fr = dist - (float)di;
    float pf = fr * (float)AA_PHASES;
    int   pr = (int)pf;
    float pw = pf - (float)pr;
    const float *h0 = rows[pr];
    const float *h1 = rows[pr + 1];
    const uint32_t len = d->len;
    uint32_t dd = di + (uint32_t)AA_CENTER;          /* deepest tap's delay   */
    uint32_t a_deep = (dd <= d->wpos) ? d->wpos - dd
                                      : d->wpos + len - dd;
    uint32_t off = a_deep - p->aapos[g];             /* unsigned: jump -> huge */
    if (off > (32u - (uint32_t)AA_TAPS)) {           /* refill (also first use)*/
        p->aapos[g] = a_deep;
        uint32_t a = a_deep;
        for (int j = 0; j < 32; ++j) {
            p->aawin[g][j] = d->buf[a];
            if (++a >= len) a = 0;
        }
        off = 0;
    }
    const float *w = &p->aawin[g][off];
    float acc = 0.0f;
    for (int j = 0; j < AA_TAPS; ++j) {              /* j+1 = one sample later */
        float c = h0[j] + pw * (h1[j] - h0[j]);
        acc += c * w[j];
    }
    return acc;
}

const float (*ps_aa_flash_rows(int band))[16]
{
    if (band < 0) band = 0;
    if (band >= AA_NBANDS) band = AA_NBANDS - 1;
    return aa_tab[band];
}

void ps_set_aa_rows(pitchshift_t *p, int band, const float (*rows)[16])
{
    if (rows == 0 || band < 0) {                     /* revoke BEFORE overwrite */
        p->aarows_band = -1;
        __asm volatile ("" ::: "memory");
        p->aarows = 0;
        return;
    }
    p->aarows = rows;                                /* publish rows THEN band  */
    __asm volatile ("" ::: "memory");
    p->aarows_band = band;
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
    /* invalid marker = mid-range: buffer positions are < len (<= ~4M), so
     * (a_deep - 0x80000000) is always >> 16 and forces the first refill —
     * 0xFFFFFFFF would collide when a_deep <= 16 after a buffer wrap. */
    p->aapos[0] = p->aapos[1] = 0x80000000u;
    p->aarows = 0;
    p->aarows_band = -1;
    p->aaband_req = -1;
    p->aa_bypass = 0;
    p->lp_start = 0u;
    p->lp_end = 0u;
    p->lp_span = 0u;
    p->period = 0.0f;
    p->per_conf = 0.0f;
    p->per_tick = 0;
}

void ps_set_loop_window(pitchshift_t *p, uint32_t start, uint32_t end,
                        uint32_t len)
{
    /* end < start is a WRAPPED window (the majority of long-window captures:
     * engine_recirc_window computes start = head + len - window) — same span
     * law as dl_read_loop_frac. Review catch: treating it as degenerate
     * silently disabled the whole mapping for wrapped captures. */
    uint32_t span = (end >= start) ? end - start : len - (start - end);
    if (span < 8u || span >= len) { p->lp_span = 0u; return; }
    p->lp_start = start;
    p->lp_end = end;
    p->lp_span = span;
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
 * Range note: reads reach base + W + EXT_ML + EXT_N (~ +6600 worst) — callers must keep
 * that inside the buffer (trivial at SDRAM sizes). Honest low-frequency reach
 * with N=768 correlation span: ~125 Hz. */
#define PS_DEGLITCH_N       768
#define PS_DEGLITCH_MAXLAG  1200
#define PS_DEGLITCH_LOOKA   0.06f     /* start searching within 6% of the wrap */

/* period-adaptive extension: with a confident low-frequency period estimate,
 * the splice search widens to see ~2 periods (bass reach ~35 Hz @96k). Caps
 * bound the superloop stall; the wrap-headroom gate below keeps the long
 * search out of tight timing windows. */
#define PS_PER_SCAN_EVERY   256       /* idle service calls between scans (the
                                         scan's SDRAM reads contend with the ISR) */
#define PS_PER_SPAN         2048      /* autocorr integration span (samples)   */
#define PS_PER_MINLAG       240       /* 400 Hz — shorter periods don't need us */
#define PS_PER_MAXLAG       3400      /* ~28 Hz                                */
#define PS_EXT_N_CAP        3000
#define PS_EXT_ML_CAP       3600

/* normalized (sign-kept NCC^2) autocorrelation score at one lag */
static float ps_lag_score(pitchshift_t *p, const delay_line_t *d,
                          float dRef, float e0, int lag)
{
    (void)p;
    float acc = 0.0f, e1 = 0.0f;
    for (int k = 0; k < PS_PER_SPAN; k += 4) {
        float a = ps_read(p, d, dRef + (float)k, DL_INTERP_LINEAR);
        float b = ps_read(p, d, dRef + (float)(k + lag), DL_INTERP_LINEAR);
        acc += a * b;
        e1  += b * b;
    }
    if (e1 < 0.01f * e0) return 0.0f;
    return acc * fabsf(acc) / (e0 * e1 + 1.0e-9f);
}

/* background source-period estimate via decimated NCC autocorrelation of the
 * outgoing-tap region. Runs ONLY on idle service calls (superloop), never in
 * the ISR. Coarse 8-sample lag grid — the fine splice search still refines
 * alignment; we only need the period to SIZE the search. */
static void ps_period_scan(pitchshift_t *p, const delay_line_t *d)
{
    if (++p->per_tick < PS_PER_SCAN_EVERY) return;
    p->per_tick = 0;
    const float dRef = p->base + 0.5f * p->window;
    float e0 = 0.0f;
    for (int k = 0; k < PS_PER_SPAN; k += 4) {
        float a = ps_read(p, d, dRef + (float)k, DL_INTERP_LINEAR);
        e0 += a * a;
    }
    if (e0 < 1.0e-6f) { p->per_conf = 0.0f; return; }
    float best = 0.0f; int bestlag = 0;
    for (int lag = PS_PER_MINLAG; lag < PS_PER_MAXLAG; lag += 8) {
        float s = ps_lag_score(p, d, dRef, e0, lag);
        if (s > best) { best = s; bestlag = lag; }
    }
    /* subharmonic disambiguation: a periodic signal scores ~equally at every
     * period MULTIPLE — if ~half (or ~a third of) the winning lag scores
     * nearly as well, the true period is the shorter one. Each candidate is
     * refined over +/-16 samples: on real (slightly drifting/inharmonic)
     * material the sub-period peak sits a few samples off the exact division,
     * and sampling the exact point rejected valid halvings (seen live). */
    while (bestlag >= 2 * PS_PER_MINLAG) {
        int moved = 0;
        static const int divs[2] = { 2, 3 };
        for (int di = 0; di < 2 && !moved; di++) {
            int c0 = bestlag / divs[di];
            if (c0 < PS_PER_MINLAG) continue;
            float sb = 0.0f; int lb = c0;
            for (int lag = c0 - 16; lag <= c0 + 16; lag += 4) {
                if (lag < PS_PER_MINLAG) continue;
                float sc = ps_lag_score(p, d, dRef, e0, lag);
                if (sc > sb) { sb = sc; lb = lag; }
            }
            if (sb >= 0.85f * best) { bestlag = lb; best = sb > best ? sb : best; moved = 1; }
        }
        if (!moved) break;
    }
    p->per_conf  = (best > 0.0f) ? sqrtf(best > 1.0f ? 1.0f : best) : 0.0f;
    p->period    = (float)bestlag;
}

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
    if (dist > PS_DEGLITCH_LOOKA) {                /* not imminent yet       */
        ps_period_scan(p, d);                      /* keep the period fresh  */
        return;
    }
    if (dist < 8.0f * astep) return;               /* too late — skip safely */

    /* period-adaptive sizing: confident bass -> widen the search to ~2
     * periods so the correlator can actually find a period-multiple lag.
     * Only when the wrap allows the longer stall (headroom gate). */
    int srchN = PS_DEGLITCH_N, srchML = PS_DEGLITCH_MAXLAG;
    /* headroom gate: samples-to-wrap must exceed the extended search's
     * superloop stall (~10 ms worst). The publish-staleness check already
     * discards a search the wrap outran, so this only avoids wasted work —
     * at extreme down-shift rates (wraps every ~10 ms) bass extension simply
     * stays off, which those grain rates mask anyway. */
    if (p->per_conf > 0.5f && p->period > 600.0f
        && dist / astep > 600.0f) {
        int n2 = (int)(2.0f * p->period);
        int m2 = (int)(1.6f * p->period);
        srchN  = (n2 > PS_EXT_N_CAP)  ? PS_EXT_N_CAP  : n2;
        srchML = (m2 > PS_EXT_ML_CAP) ? PS_EXT_ML_CAP : m2;
        if (srchML < PS_DEGLITCH_MAXLAG) srchML = PS_DEGLITCH_MAXLAG;
        if (srchN  < PS_DEGLITCH_N)      srchN  = PS_DEGLITCH_N;
    }

    /* correlate: the incoming tap enters at frac 0 for DOWN-shifts (delay base)
     * but frac 1 for UP-shifts (delay base+W; phase runs backward). */
    const float dIn  = (step > 0.0f) ? p->base : p->base + p->window;
    const float dOut = p->base + 0.5f * p->window + p->off[tap ^ 1];

    /* outgoing-window energy once (lag-independent) for the guards */
    float eb = 0.0f;
    for (int k = 0; k < srchN; k += 2) {
        float b = ps_read(p, d, dOut + (float)k, DL_INTERP_LINEAR);
        eb += b * b;
    }
    if (eb < 1.0e-7f) return;                      /* silence: keep offset 0 */

    float best = 0.0f; int bestlag = 0;
    for (int lag = 0; lag < srchML; lag += 4) {   /* coarse grid */
        float acc = 0.0f, ein = 0.0f;
        for (int k = 0; k < srchN; k += 2) {          /* decimate x2 */
            float a = ps_read(p, d, dIn + (float)lag + (float)k, DL_INTERP_LINEAR);
            float b = ps_read(p, d, dOut + (float)k, DL_INTERP_LINEAR);
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
            if (lag < 0 || lag >= srchML) continue;
            float acc = 0.0f, ein = 0.0f;
            for (int k = 0; k < srchN; k += 2) {
                float a = ps_read(p, d, dIn + (float)lag + (float)k, DL_INTERP_LINEAR);
                float b = ps_read(p, d, dOut + (float)k, DL_INTERP_LINEAR);
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
        return ps_read(p, d, p->base + 0.5f * W, interp);
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

    /* UP-shifts read through the band-limited polyphase kernel (anti-alias);
     * down-shifts/unity keep the exact Hermite path. Band steps as the slewed
     * ratio crosses an edge — coefficient steps are tiny and click-free.
     * Coefficients: platform-published fast rows (CCM) when the band matches,
     * const flash tables otherwise (correct either way, CCM is just faster). */
    int band = -1;
    if (!p->aa_bypass && !p->lp_span && p->ratio > 1.02f) {
        band = 0;
        while (band < AA_NBANDS - 1 && p->ratio > aa_band_edge[band]) band++;
    }
    const float dA = p->base + fracA * W + p->off[0];
    const float dB = p->base + fracB * W + p->off[1];
    float out;
    if (band >= 0) {
        p->aaband_req = band;
        const float (*rows)[AA_TAPS] =
            (p->aarows_band == band && p->aarows) ? p->aarows : aa_tab[band];
        out = gA * ps_read_bl(p, d, 0, dA, rows)
            + gB * ps_read_bl(p, d, 1, dB, rows);
    } else {
        out = gA * ps_read(p, d, dA, interp) + gB * ps_read(p, d, dB, interp);
    }

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
