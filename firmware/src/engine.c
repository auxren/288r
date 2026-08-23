/* engine.c — see engine.h. */
#include "engine.h"

void engine_init(engine_t *e, float *buf, uint32_t len,
                 float base_delay, float time_lo, float time_hi, float slew)
{
    dl_init(&e->dl, buf, len);
    dl_clear(&e->dl);
    taps_init(&e->taps, base_delay, slew);
    tc_init(&e->time, 1.0f, slew, time_lo, time_hi);
    transport_init(&e->xport, /*auto_cycle*/ 0);
    mixer_init(&e->mix);
    bw_init(&e->bw, 1.0f, 0.0f);   /* bandwidth limit off (bypass = identity) */
    e->interp = DL_INTERP_HERMITE;
    e->in_gain = 1.0f;
    e->auto_correction = 0.0f;
    e->vintage_bits = 0;
    e->skip_tap_reads = 0;
    e->dith = 0x1234567u;                 /* dither PRNG seed */
    e->varispeed = 0;
    e->lp_mult_ref = 1.0f;
    e->lp_phase = 0.0f;
    e->lp_rate = 1.0f;
    e->time_fm = 0.0f;
    e->declick_n = 0u;
    e->wr_seam_n = 0u;
    for (int i = 0; i < NUM_TAPS; i++) e->declick_hold[i] = 0.0f;
    for (int i = 0; i < NUM_TAPS; i++) e->fm_off[i] = 0.0f;
    e->fm_lanes = 0u;
    e->od_active = 0;
    e->od_decay = 0.95f;
    e->od_gain = 0.0f;
    e->od_env = 0.0f;
    e->od_lim = 1.0f;
    e->od_xprev = 0.0f;
    e->od_xsum = 0.0f;
    e->od_xn = 0u;
    e->od_lp1 = 0.0f;
    e->od_lp2 = 0.0f;
    e->spl_active = 0;
    e->spl_start = e->spl_end = e->spl_fade = e->spl_idx = 0;
    e->spl_quota = 1u;
#if DL_CACHE_ENABLE
    dc_init(&e->dc);
#endif
    transport_begin_write(&e->xport, e->dl.wpos);
}

void engine_set_bandwidth(engine_t *e, float fs, float cutoff_hz)
{
    bw_init(&e->bw, fs, cutoff_hz);
}

float engine_clamp_base(float base, uint32_t len, float time_hi)
{
    /* deepest tap = base * (PHASE_FULLSCALE/PHASE_FULLSCALE) * time_hi = base*time_hi.
     * keep it + a guard (write-head + Hermite's 4-sample stencil) inside the buffer. */
    float denom   = (time_hi > 0.0f) ? time_hi : 1.0f;
    float max_base = ((float)len - 64.0f) / denom;
    return (base < max_base) ? base : max_base;
}

/* WCET bound for the splice job (C-3a/C-4): quota per frame, and the fastest
 * the recirc head can travel (the varispeed rate clamp in engine_process_multi
 * is 0.25..4.0 — if that clamp ever moves, this must move with it). */
#define SPLICE_CHUNK  8u
#define SPL_HEAD_MAX  4u
/* Exposure numerator: quota = ceil(N*fade/DECLICK_FADE). N is a QUALITY knob,
 * not a performance one — see the measured table in splice_arm. */
#define SPL_EXPOSURE_NUM 8u

/* arm the chunked seam splice: guards written immediately (4 writes), the
 * crossfade amortized into engine_process_multi. Re-arming mid-job simply
 * restarts on the new window. */
static void splice_arm(engine_t *e, uint32_t start, uint32_t end, uint32_t fade)
{
    uint32_t win = (end >= start) ? end - start : end + e->dl.len - start;
    if (fade == 0u || win <= 2u * fade) { e->spl_active = 0; return; }
    for (uint32_t i = 0; i < 4u; i++)   /* stencil guards past the seam */
        e->dl.buf[(end + i) % e->dl.len] = e->dl.buf[(start + i) % e->dl.len];
    e->spl_start = start; e->spl_end = end; e->spl_fade = fade;
    e->spl_idx = 0; e->spl_active = 1;
#if DL_CACHE_ENABLE
    dc_invalidate(&e->dc);        /* rule 4: guard samples just moved */
#endif
    /* PER-FRAME QUOTA — and why the old flat 8 turns out to be load-bearing.
     *
     * The job is amortised, so at any moment the tail is part rewritten and part
     * original with a FRONTIER between them, and a tap read crossing that
     * frontier hears a step. The transport declick is simultaneously opening
     * from 0 to 1 over DECLICK_FADE frames, so what is audible is the product of
     * the frontier step and the declick weight at the frame the crossing
     * happens. Finish sooner -> every crossing happens while the declick is
     * still small.
     *
     * This was measured, not modelled. Dropping the quota to 2 to save SDRAM
     * traffic made test_declick's two CAPTURE cases fail; sweeping the quota and
     * the fill direction against that test's transition step (threshold 0.01345,
     * signal slope 0.00448) gives:
     *
     *      quota  forward (w rises)        reverse (w falls, seam first)
     *              W->R      R->R            W->R      R->R
     *        2    0.0625    0.0359          0.0376    0.0893
     *        3    0.0369    0.0805          0.0322    0.0620
     *        4    0.0251    0.0432          0.0248    0.0426
     *        6    0.0121    0.0165          0.0155    0.0228
     *        8    0.0072    0.0084          0.0106    0.0139   <- fails at R->R
     *
     * So: forward order wins (a closed-form argument said reverse should be 4x
     * better — it is not; which tap crosses the frontier when is a lottery, not
     * a smooth law, and the sweep is not even monotonic), and 8 is the first
     * value with real margin. The quota stays at the shipped schedule; the
     * saving in this commit comes from the inner loop being half the cost, not
     * from doing less of it. If anyone is tempted to trade it for ISR headroom
     * later, that is a QUALITY decision and it belongs in the governor, at a
     * level where the trade is deliberate.
     *
     * Expressed as a derivation rather than a literal 8 so it tracks the two
     * constants it actually depends on. Second constraint: finish before the
     * read head laps the window and wraps through an unfinished seam — head
     * travel is at most SPL_HEAD_MAX per frame (the varispeed clamp); give the
     * job three quarters of one pass. */
    {
        uint32_t qa = (SPL_EXPOSURE_NUM * fade + DECLICK_FADE - 1u) / DECLICK_FADE;
        uint32_t tgt = win - (win >> 2);              /* 0.75 of one pass      */
        uint32_t qb = (fade * SPL_HEAD_MAX + tgt - 1u) / tgt;
        uint32_t q  = (qa > qb) ? qa : qb;
        if (q < 1u) q = 1u;
        if (q > SPLICE_CHUNK) q = SPLICE_CHUNK;
        e->spl_quota = (uint8_t)q;
    }
}

/* Service the splice job for `frames` frames' worth of quota.
 *
 * C-4 (no unbounded amortized work): the work done here is exactly
 * spl_quota * frames RMWs, spl_quota <= SPLICE_CHUNK, so the per-block cost is
 * a compile-time bound and belongs in the static ceiling.
 *
 * Called once per block from engine_process_block (the whole point — the loop
 * invariants below are computed once instead of 32 times) and with frames=1
 * from engine_process_multi, which keeps the two paths identical. */
static void splice_service(engine_t *e, unsigned frames)
{
    if (!e->spl_active) return;
    const uint32_t len = e->dl.len, fade = e->spl_fade;
    float *const buf = e->dl.buf;
    const float inv = 1.0f / (float)fade;
    uint32_t done = e->spl_idx;                   /* samples already rewritten */
    uint32_t n = (uint32_t)e->spl_quota * frames;
    if (n > fade - done) n = fade - done;
    /* Walking indices instead of two `% len` per iteration: end+len-fade+i and
     * start+len-fade+i are both < 2*len for every i < fade, so a conditional
     * subtract is exact — and it replaces two hardware UDIVs (up to 12 cycles
     * each) in the innermost loop of the busiest moment in the machine. Fill
     * order is FORWARD and stays forward: see the sweep in splice_arm. */
    uint32_t i    = done;
    uint32_t tail = (e->spl_end   + len - fade + done) % len;
    uint32_t lead = (e->spl_start + len - fade + done) % len;
    for (uint32_t k = 0; k < n; k++, i++) {
        float w = (float)(i + 1u) * inv;          /* ..end-1 reaches w=1       */
        buf[tail] = (1.0f - w) * buf[tail] + w * buf[lead];
        if (++tail >= len) tail = 0u;
        if (++lead >= len) lead = 0u;
    }
    e->spl_idx = done + n;
    if (e->spl_idx >= fade) e->spl_active = 0;
}

void engine_resplice(engine_t *e)
{
    splice_arm(e, e->xport.loop_start, e->xport.loop_end, LOOP_SPLICE_FADE);
}

/* ---- per-block invariants -------------------------------------------------
 *
 * Everything in here is fixed for the duration of one DMA half-block. It is
 * safe to hoist because the only code that can change any of it — engine_write,
 * engine_recirc*, taps_set_*, the governor — runs in the SUPERLOOP, and the
 * superloop cannot preempt the audio ISR (it is the other way round). Transport
 * transitions therefore always land BETWEEN blocks, never inside one.
 *
 * What this buys: the window mapping (span, base, and the head's modulo into the
 * window) used to be recomputed from scratch inside dl_read_loop_frac() for each
 * of the 8 taps, every sample — 8 copies of a computation that has no `i` in it,
 * including a UDIV. Now it is computed once per block (span/base) and once per
 * frame (the head), and the per-tap work is the part that actually varies. */
typedef struct {
    uint32_t     len;
    uint32_t     span;      /* 0 = whole-buffer addressing (WRITE / degenerate) */
    uint32_t     base;      /* loop_start                                       */
    uint32_t     ls, le;
    int          recirc;
    dl_interp_t  interp;    /* e->interp, after the governor's quality level    */
} eng_blk_t;

static void eng_blk_begin(engine_t *e, eng_blk_t *bk, unsigned frames)
{
#if !DL_CACHE_ENABLE
    (void)frames;
#endif
    bk->len    = e->dl.len;
    bk->recirc = !transport_should_write(&e->xport);
    bk->ls     = e->xport.loop_start;
    bk->le     = e->xport.loop_end;
    bk->base   = bk->ls;
    bk->span   = 0u;
    if (bk->recirc) {
        uint32_t s = (bk->le >= bk->ls) ? bk->le - bk->ls
                                        : bk->len - (bk->ls - bk->le);
        bk->span = (s < 1u) ? 0u : s;   /* degenerate window -> whole buffer    */
    }
    /* LOAD GOVERNOR (workstream B): read ONCE per block. A level change inside
     * a block would swap the interpolation kernel mid-buffer — the same
     * un-crossfaded discontinuity class as #24's AA engage. */
    bk->interp = e->interp;
    if (gov_level() >= GOV_LEVEL_LINEAR) bk->interp = DL_INTERP_LINEAR;
#if DL_CACHE_ENABLE
    /* Window cache: age the lines (lifetime + refill rate limit). The
     * foreign-writer verdict (rule 3) is re-evaluated per FRAME below, because
     * both the overdub ramp and the splice job can change state inside a
     * block; this is only the per-block bookkeeping. */
    dc_block_begin(&e->dc, 1, frames ? frames : 1u);
#endif
}

/* One frame. `bk` carries the block invariants; `want_sum` is folded away by the
 * inliner at both call sites. Keeping engine_process_multi() and
 * engine_process_block() on ONE body is what makes their bit-equality a
 * structural property rather than something to be re-argued after each edit. */
static inline float eng_frame(engine_t *e, float input, float time_raw01,
                              float chan[NUM_TAPS], const eng_blk_t *bk,
                              int want_sum)
{
    /* 1. delay-time control -> tap positions (continuous, slewed) */
    float mult = tc_update(&e->time, time_raw01);
    /* VARISPEED TAP FREEZE (stock tape model): on the original hardware the
     * play heads are fixed — changing speed repitches the loop but the tap
     * distances IN SAMPLES never move. Applying the live multiplier to the
     * taps during varispeed playback double-applied the knob (motor + head
     * movement at once) and the fast read sweeps aliased through Hermite
     * (field: 'zippers and distorts for a little then corrects'). While a
     * loop plays under varispeed, taps hold their capture-time scale; the
     * knob is the motor alone. Live/all-sounds write keeps respacing — that
     * is the chorus/flanger behavior. */
    const int recirc = bk->recirc;
    float m_taps = (e->varispeed && recirc) ? e->lp_mult_ref : mult;
    taps_update(&e->taps, m_taps);

    const uint32_t ls = bk->ls, le = bk->le;

    /* 2. record or recirculate */
    if (!recirc) {
        float x = mixer_input(input, e->in_gain);
        x = bw_process(&e->bw, x);            /* sw2 bandwidth limit (bypass=identity) */
        if (e->vintage_bits > 0) {
            /* TPDF dither (two xorshift uniforms, triangular in [-1,1] quantum
             * units): decorrelates the bit-crush distortion into benign noise —
             * proper lo-fi instead of gritty correlated error. */
            e->dith ^= e->dith << 13; e->dith ^= e->dith >> 17; e->dith ^= e->dith << 5;
            uint32_t r1 = e->dith;
            e->dith ^= e->dith << 13; e->dith ^= e->dith >> 17; e->dith ^= e->dith << 5;
            float tp = ((float)(r1 >> 8) + (float)(e->dith >> 8)) * (1.0f / 16777216.0f) - 1.0f;
            x = dl_vintage_quantize(x, e->vintage_bits, tp);
        }
        if (e->wr_seam_n) {
            /* Blend from what is already at this position into the new input,
             * so the buffer has no step for later passes to reproduce (#28). */
            float w = 1.0f - (float)e->wr_seam_n / (float)WR_SEAM_FADE;
            float old = e->dl.buf[e->dl.wpos];
            x = old + (x - old) * w;
            e->wr_seam_n--;
        }
        dl_write(&e->dl, x);
    } else {
        /* Varispeed (#9): the head advances lp_rate samples per output sample.
         * rate = mult(at capture)/mult(now) — turn the multiplier down and the
         * loop plays faster AND higher, tape-style, matching the stock family
         * whose delay knob moves the sample clock itself. Rate rides the same
         * slewed tc multiplier, so it glides zipper-free with knob and CV. */
        float r = 1.0f;
        if (e->varispeed) {
            float m = (mult > 0.05f) ? mult : 0.05f;
            r = e->lp_mult_ref / m;
            if (r < 0.25f) r = 0.25f;
            if (r > 4.0f)  r = 4.0f;
        }
        e->lp_rate = r;
        e->lp_phase += r;
        /* OVERDUB: ramp the layered input in/out over ~10 ms at the hold
         * edges (a step at engage/release wrote a click front every tap
         * crossed each pass — field: "staticy"), and knee the WRITTEN value
         * at 0.75 with asymptote 1.0 — the same regime the output stage is
         * linear in, so stacked layers compress gracefully instead of
         * parking the whole loop in the output limiter (field: "digitally
         * clippy"; the old 2.0 ceiling overshot DAC-linear range). */
        if (e->od_active)      { e->od_gain += (1.0f - e->od_gain) * 0.001f; }
        else if (e->od_gain > 0.0f) { e->od_gain -= e->od_gain * 0.001f;
                                      if (e->od_gain < 1e-4f) e->od_gain = 0.0f; }
        if (e->od_gain > 0.0f) {
            float xo = mixer_input(input, e->in_gain);
            xo = bw_process(&e->bw, xo) * e->od_gain;
            /* squeal guard: 2x one-pole at ~10 kHz (see od_lp1 in engine.h) */
            #define OD_LP_A 0.480f
            e->od_lp1 += (xo - e->od_lp1) * OD_LP_A;
            e->od_lp2 += (e->od_lp1 - e->od_lp2) * OD_LP_A;
            xo = e->od_lp2;
            /* The layered input is RESAMPLED onto the loop's varispeed clock:
             * rate < 1 box-averages the samples between writes (ZOH dropped
             * them — zipper when the multiplier moved mid-overdub); rate > 1
             * interpolates linearly across the multi-writes (ZOH duplicated
             * them — images). */
            e->od_xsum += xo;
            e->od_xn++;
            int nw = (int)e->lp_phase, j = 0;
            /* WCET (C-3a): nw <= 4 because lp_phase < 1 on entry and the rate is
             * clamped to 4.0 in the branch above — this while-loop has a
             * compile-time bound of 4 iterations, and `make wcet` may rely on it.
             * The reciprocal is computed once instead of a VDIV per write (~14
             * cycles each on the M4F). Bit-exact for the only values that can
             * occur here: nw in 2..4, j in 1..nw, so the products are exactly
             * the halves, thirds and quarters, and j*(1/3) rounds to the same
             * float as j/3 for j = 1, 2, 3. */
            const float inv_nw = (nw > 1) ? 1.0f / (float)nw : 0.0f;
            while (e->lp_phase >= 1.0f) {
                float val;
                j++;
                if (nw <= 1) {
                    val = e->od_xsum / (float)e->od_xn;
                } else {
                    val = e->od_xprev + (xo - e->od_xprev) * ((float)j * inv_nw);
                }
                float v = e->dl.buf[e->dl.wpos] * e->od_decay + val;
                /* WRITE LIMITER: peak-hold envelope + a ~5 ms slewed GAIN.
                 * The gain must move far below audio rate — a fast-attack
                 * follower modulated the gain at 2x the signal frequency,
                 * i.e. harmonic distortion written INTO the loop (field:
                 * 'second overdub = distortion and grit'). Brief overs while
                 * the gain settles ride the float headroom + output knee. */
                float av = (v < 0.0f) ? -v : v;
                if (av > e->od_env) e->od_env = av;
                else                e->od_env += (0.0f - e->od_env) * 0.0002f
                                              + av * 0.0002f;
                float tgt = (e->od_env > 0.75f) ? 0.75f / e->od_env : 1.0f;
                e->od_lim += (tgt - e->od_lim) * 0.002f;
                v *= e->od_lim;
                /* hard FS clamp: the gain slew lags transients, and anything
                 * written >1.0 is BAKED into the loop forever (field forensics:
                 * content peak 1.092 after an od session). The clamp only
                 * touches the brief settling overs the slew already misses. */
                if (v >  1.0f) v =  1.0f;
                if (v < -1.0f) v = -1.0f;
                e->dl.buf[e->dl.wpos] = v;
                dl_advance_loop(&e->dl, ls, le);
                e->lp_phase -= 1.0f;
            }
            if (nw > 0) { e->od_xsum = 0.0f; e->od_xn = 0u; }
            e->od_xprev = xo;
        } else {
            while (e->lp_phase >= 1.0f) {
                dl_advance_loop(&e->dl, ls, le);
                e->lp_phase -= 1.0f;
            }
        }
    }

    /* pitch mode at full wet: the crossfade discards the tap outputs, so skip
     * the 8 SDRAM reads entirely (control, write, recirc all ran above). */
    if (e->skip_tap_reads) {
        for (int i = 0; i < NUM_TAPS; i++) chan[i] = 0.0f;
        /* Keep the declick consistent with what is actually leaving here (zero),
         * and let an armed fade expire rather than lurk: without this, a
         * transport change made while the pitch/string voice owns the outputs
         * would fire its fade from a stale held value on the way back to the
         * delay taps. */
        for (int i = 0; i < NUM_TAPS; i++) e->declick_hold[i] = 0.0f;
        if (e->declick_n) e->declick_n--;
        return 0.0f;
    }

    /* 3. read the 8 taps (loop-aware in RECIRC) — exact int+frac path, so the
     * fraction survives at SDRAM buffer sizes (see dl_read_frac) */
    /* delay-time FM (signal-in, owner feature 2026-07-25): scale every tap
     * distance by (1 + fm). The OFFSET is computed in float (error <= ULP at
     * |offset|<=0.25*base ~ 0.005 samples — inaudible) and folded into the
     * exact int+frac position, preserving the SDRAM-size precision rule. */
    float fm = e->time_fm;
    if (fm >  0.25f) fm =  0.25f;
    if (fm < -0.25f) fm = -0.25f;
#define FM_MAX_STEP 0.5f   /* max read-position slew, samples per sample: keeps
                              the resample ratio inside [0.5, 1.5] — Doppler,
                              never teleporting. Also rounds the corners of a
                              rail-clipped modulator. */
    /* FM FAST-OUT. With no signal-in patched, fm is EXACTLY zero (main.c snaps
     * it below FM_EPS for precisely this reason) and every lane has already
     * decayed to exactly zero — target 0, delta 0, offset 0, nothing happens.
     * The arithmetic still ran: ~10 instructions x 8 taps x every sample, for a
     * modulation nobody patched. e->fm_lanes is the bitmask of lanes that are
     * still non-zero, maintained where the offsets are written, so the whole
     * block costs one compare when the feature is idle. (Same lesson as the
     * FM_EPS snap itself, measured at 4.3% of budget in the ISR — an asymptotic
     * "off" is not off.) */
    const int fm_idle = (fm == 0.0f) && (e->fm_lanes == 0u);
    /* Read geometry shared by all 8 taps: the loop window map, or whole-buffer
     * addressing in WRITE. span/base are block invariants; only `pos` (the head)
     * moves per frame, and it moved when we wrote/advanced above. */
    dl_map_t map;
    map.buf  = e->dl.buf;
    map.len  = bk->len;
    map.span = bk->span;
    map.base = bk->base;
    map.pos  = bk->span ? dl_map_head(e->dl.wpos, bk->len, bk->base, bk->span)
                        : e->dl.wpos;
    /* OD HEADROOM: while overdub is held the write loop costs up to rate x
     * (RMW + limiter) per sample ON TOP of the ~90% baseline — a ~3x varispeed
     * overdub SATURATED the ISR and starved the superloop into a freeze (field
     * 2026-07-29, SWD-captured). Linear interp halves the tap SDRAM loads for
     * the duration of the hold; Hermite returns the moment od_gain decays out.
     * Hoisted to the loop head so the kernel choice is one branch per frame
     * instead of one per tap, and so each kernel inlines with the polynomial
     * fully specialised. */
    const int herm = (e->od_gain <= 0.0f) && (bk->interp != DL_INTERP_LINEAR);
#if DL_CACHE_ENABLE
    /* dl_cache.h rule 3, per frame: while the overdub loop or the splice job is
     * writing into the buffer, no line may be trusted OR kept. Dropping them
     * every frame (8 byte stores) rather than on the edge keeps the reasoning
     * to one line — and both states are rare and already expensive. */
    const int dc_ok = !e->spl_active && (e->od_gain <= 0.0f);
    if (!dc_ok) dc_invalidate(&e->dc);
#endif
    float taps[NUM_TAPS];
    for (int i = 0; i < NUM_TAPS; i++) {
        uint32_t d_int; float d_frac;
        taps_delay_frac(&e->taps, i, &d_int, &d_frac);
        if (!fm_idle) {
            float target = ((float)d_int + d_frac) * fm;
            float delta = target - e->fm_off[i];
            if (delta >  FM_MAX_STEP) delta =  FM_MAX_STEP;
            if (delta < -FM_MAX_STEP) delta = -FM_MAX_STEP;
            float off = e->fm_off[i] + delta;
            e->fm_off[i] = off;
            /* lane bookkeeping: `off` reaches EXACTLY 0 on the release ramp
             * (delta = -off once |off| <= FM_MAX_STEP), so the mask clears
             * cleanly and the fast-out above re-arms. */
            if (off != 0.0f) e->fm_lanes |=  (uint8_t)(1u << i);
            else             e->fm_lanes &= (uint8_t)~(1u << i);
            if (off != 0.0f) {
                float off_fl = (off >= 0.0f) ? (float)(int32_t)off
                                             : (float)((int32_t)off - 1);
                int32_t off_i = (int32_t)off_fl;
                d_frac += off - off_fl;
                if (d_frac >= 1.0f) { d_frac -= 1.0f; off_i += 1; }
                if (off_i >= 0) d_int += (uint32_t)off_i;
                else {
                    uint32_t mag = (uint32_t)(-off_i);
                    d_int = (d_int > mag) ? d_int - mag : 1u;
                }
            }
        }
        if (recirc) {
            /* the true head is wpos + lp_phase: a tap D behind the true head
             * sits D - lp_phase behind wpos — this sub-sample term is what
             * makes varispeed playback continuous instead of stair-stepped */
            d_frac -= e->lp_phase;
            if (d_frac < 0.0f) {
                d_frac += 1.0f;
                d_int = (d_int > 0u) ? d_int - 1u : 0u;
            }
        }
        if (d_int < 1) { d_int = 1; d_frac = 0.0f; }   /* keep off the write head */
        {
            uint32_t a0 = dl_map_index(&map, d_int);
            const float *p = 0;
#if DL_CACHE_ENABLE
            /* CCM window cache. Returns four words identical to the ones the
             * direct path would load, or NULL — it cannot change the audio,
             * only where the words came from (dl_cache.h). */
            if (dc_ok) p = dc_stencil(&e->dc, (unsigned)i, map.buf, map.len,
                                      a0, d_int);
#endif
            if (p) taps[i] = herm ? dl_stencil4_hermite(p, d_frac)
                                  : dl_stencil4_linear (p, d_frac);
            else   taps[i] = herm ? dl_stencil_hermite(map.buf, map.len, a0, d_frac)
                                  : dl_stencil_linear (map.buf, map.len, a0, d_frac);
        }
    }

    /* 4. transport declick: crossfade the read jump away (#27-#31). Idle, this
     * just tracks the last tap values so an arm has somewhere continuous to
     * start from. */
    if (e->declick_n) {
        float w = 1.0f - (float)e->declick_n / (float)DECLICK_FADE;
        for (int i = 0; i < NUM_TAPS; i++)
            taps[i] = e->declick_hold[i] + (taps[i] - e->declick_hold[i]) * w;
        e->declick_n--;
    } else {
        for (int i = 0; i < NUM_TAPS; i++) e->declick_hold[i] = taps[i];
    }

    /* 5. mix: 8 per-tap DAC channels + (only if asked) the summed output.
     *
     * The "mixed" jacks are an ANALOG sum of the 8 channel outputs on PCB1
     * (re/notes/hardware.md: 68k -> phase switch -> bus -> 4.7k -> TL072).
     * Nothing in the firmware consumes mixer_sum()'s result — both ISR call
     * sites cast it to (void) — so the block path does not compute it. It stays
     * on the per-sample API because the host tests and engine_process() use it
     * as a convenient single-number view of the mix. */
    mixer_channels(&e->mix, taps, chan);
    return want_sum ? mixer_sum(&e->mix, taps, e->auto_correction) : 0.0f;
}

float engine_process_multi(engine_t *e, float input, float time_raw01, float chan[NUM_TAPS])
{
    eng_blk_t bk;
    splice_service(e, 1u);
    eng_blk_begin(e, &bk, 1u);
    return eng_frame(e, input, time_raw01, chan, &bk, /*want_sum*/ 1);
}

float engine_process(engine_t *e, float input, float time_raw01)
{
    float chan[NUM_TAPS];
    return engine_process_multi(e, input, time_raw01, chan);
}

/* Weak default for the governor accessor (see engine.h): full quality. When
 * workstream B's governor.c lands it provides the strong definition and this
 * one drops out at link time — the engine never has to know whether the
 * governor exists. */
__attribute__((weak)) unsigned gov_level(void) { return GOV_LEVEL_FULL; }

void engine_process_block(engine_t *e, const float *in, float time_raw01,
                          const float *fm, float (*chan_out)[NUM_TAPS], unsigned n)
{
    eng_blk_t bk;
    eng_blk_begin(e, &bk, n);       /* the whole point: once, not 8x per frame */
    for (unsigned k = 0; k < n; k++) {
        /* The splice stays INTERLEAVED rather than batched at the block edge.
         * Two reasons, and they agree: it keeps engine_process_block() bit-equal
         * to the per-frame path (the taps can read the tail the job is
         * rewriting), and a batch at the block edge would be a miniature of
         * exactly the burst that chunking exists to remove. The saving is the
         * quota (8 -> typically 1 per frame), not the batching. */
        splice_service(e, 1u);
        if (fm) e->time_fm = fm[k];
        /* WRITE PATH STAYS PER FRAME (see engine.h): the pitch voice reads this
         * same buffer between our frames. Only the READ geometry is batched. */
        (void)eng_frame(e, in[k], time_raw01, chan_out[k], &bk, /*want_sum*/ 0);
    }
}

/* Arm the output crossfade. declick_hold[] already carries the previous
 * sample's tap values (tracked every sample the fade is idle), so the outgoing
 * side starts exactly where the audio was - no step at the start of the fade
 * either. */
static void declick_arm(engine_t *e)
{
    e->declick_n = DECLICK_FADE;
#if DL_CACHE_ENABLE
    /* Every transport entry passes through here, and every one of them moves
     * the head and/or remaps the window (dl_cache.h rule 4). */
    dc_invalidate(&e->dc);
#endif
}

void engine_write(engine_t *e)
{
    const int was_recirc = !transport_should_write(&e->xport);
    transport_begin_write(&e->xport, e->dl.wpos);
    declick_arm(e);
    /* Writing resumes mid-buffer on top of old content: blend into it (#28). */
    if (was_recirc) e->wr_seam_n = WR_SEAM_FADE;
}
void engine_recirc(engine_t *e)
{
    transport_begin_recirc(&e->xport, e->dl.wpos);
    splice_arm(e, e->xport.loop_start, e->xport.loop_end, LOOP_SPLICE_FADE);
    e->lp_mult_ref = e->time.mult;   /* varispeed rate reference (#9) */
    e->lp_phase = 0.0f;
    declick_arm(e);
}

void engine_recirc_window(engine_t *e, uint32_t window)
{
    if (window < 2u) window = 2u;
    if (window > e->dl.len - 4u) window = e->dl.len - 4u;
    uint32_t head = e->dl.wpos;
    uint32_t start = (head >= window) ? head - window : head + e->dl.len - window;
    e->xport.mode = XP_RECIRC;
    e->xport.loop_start = start;
    e->xport.loop_end = head;
    /* snap the head INTO the window: leaving it on loop_end lets dl_advance_loop
     * increment past the boundary and free-run the whole buffer (loop AUDIO was
     * right — reads are window-mapped — but the head never wrapped, so no
     * end-of-cycle events fired). */
    e->dl.wpos = start;
    splice_arm(e, start, e->xport.loop_end, LOOP_SPLICE_FADE);
    e->lp_mult_ref = e->time.mult;   /* varispeed rate reference (#9) */
    e->lp_phase = 0.0f;
    declick_arm(e);
}

void engine_recirc_span(engine_t *e, uint32_t start, uint32_t end)
{
    if (start >= e->dl.len) start %= e->dl.len;
    if (end   >= e->dl.len) end   %= e->dl.len;
    if (start == end) return;
    e->xport.mode = XP_RECIRC;
    e->xport.loop_start = start;
    e->xport.loop_end = end;
    e->dl.wpos = start;
    splice_arm(e, start, end, LOOP_SPLICE_FADE);
    e->lp_mult_ref = e->time.mult;   /* varispeed rate reference (#9) */
    e->lp_phase = 0.0f;
    declick_arm(e);
}

void engine_recirc_between(engine_t *e, uint32_t start)
{
    uint32_t head = e->dl.wpos;
    if (start >= e->dl.len) start %= e->dl.len;
    if (start == head) return;                /* zero-length: ignore */
    e->xport.mode = XP_RECIRC;
    e->xport.loop_start = start;
    e->xport.loop_end = head;
    e->dl.wpos = start;
    splice_arm(e, start, head, LOOP_SPLICE_FADE);
    e->lp_mult_ref = e->time.mult;   /* varispeed rate reference (#9) */
    e->lp_phase = 0.0f;
    declick_arm(e);
}
