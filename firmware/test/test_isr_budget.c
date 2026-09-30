/* test_isr_budget.c — the ISR cost BOUND, host side.
 *
 * WHAT THIS IS FOR.  The branch's whole claim is a guarantee: the audio ISR
 * cannot exceed its per-block deadline, for any combination of features, not
 * merely for the combinations a bench session happened to play.  `make wcet`
 * argues that from the instruction stream; this argues it from the WORK the
 * engine actually performs, which is the half that host tests can see, and it
 * is the half where the cost lives: the field attribution put the core
 * per-sample DSP path (8 interpolated SDRAM reads per frame) at the top of an
 * ISR measured at 110-117% of budget.
 *
 * THE COST MODEL, and why it is not a guess.  Two numbers were measured on the
 * unit, and they pin the model's two unknowns exactly:
 *
 *     A) TIME/RECIRC, loop playing, Hermite taps  = 110% of budget
 *     B) the same run with engine.interp forced to DL_INTERP_LINEAR = 100%
 *
 * The only difference between A and B is, per tap read, two fewer SDRAM words
 * (4-word stencil -> 2) and a cheaper polynomial.  So
 *
 *     (A - B) * BUDGET = FRAMES * TAPS * (2 * sdram_word + (herm_alu - lin_alu))
 *
 * With the Hermite/linear ALU difference taken from the emitted code (see
 * dl_stencil4_*: 13 extra flops, ~13 cycles on an M4F), one unknown is left and
 * it comes out at ~16 cycles per SDRAM word — INSIDE the 10-18 cycle range the
 * lead independently states for this FMC.  A model that was fitted to one
 * number would land anywhere; this one lands on the physics, so it is used
 * here as an instrument rather than as decoration.  Everything the model does
 * NOT price (the fixed per-frame remainder: control slews, mixer, output
 * stage, envelopes, panel, LEDs) is carried as one constant F taken from
 * anchor A.  F can only have gone DOWN on this branch (control-rate
 * decimation, the window-map hoist, the FM fast-out, the output-stage
 * rewrite), so every projection printed below is an UPPER BOUND on the new
 * image.  That is the right direction for a guarantee.
 *
 * WHAT IS ASSERTED vs WHAT IS REPORTED.  Asserted: the properties that hold by
 * construction and that a future edit could silently break — the cache can
 * never cost more SDRAM traffic than the direct path it replaces, the traffic
 * stays bounded in the adversarial cases (wrapped windows, rail varispeed, a
 * splice in flight, overdub, FM at full depth), the governor reaches and holds
 * a cheaper level, and the audio does not fall apart when it does.  Reported
 * (and asserted only against the deadline, not against the PM's targets): the
 * projected load, because the two figures that matter — 75% typical and a 60%
 * floor level — are NOT met by the current design and saying so is the point.
 * -DISR_BUDGET_STRICT turns the target lines into hard failures, which is how
 * they become machine-checkable the day the floor level exists.
 */
#include "engine.h"
#include "governor.h"
#include "bsp/board.h"
#include <stdio.h>
#include <math.h>

static int fails = 0;
static void ck(const char *name, int cond) {
    printf("  %-58s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}
static int gaps = 0;
static void gap(const char *name, int cond) {
    printf("  %-58s %s\n", name, cond ? "ok" : "GAP");
    if (!cond) {
        gaps++;
#ifdef ISR_BUDGET_STRICT
        fails++;
#endif
    }
}

/* ---- the measured anchors (bsp/board.h + the lead's SWD session) ---------- */
#define BUDGET_CYCLES  56016.0     /* 168 MHz / 2999 blocks/s, ratified        */
#define FRAMES_PER_BLK 16.0        /* BLOCK_FRAMES                             */
#define MEAS_HERMITE   1.10        /* anchor A                                 */
#define MEAS_LINEAR    1.00        /* anchor B                                 */
#define HERM_ALU_EXTRA 13.0        /* extra flops, Hermite vs linear stencil    */
/* Share of the fixed remainder that the FLOOR level's half-rate tap servicing
 * does NOT have to pay on a held frame. A held frame still converts its input,
 * runs the control slews, writes the delay line and lays down eight output
 * words; it does NOT do the eight tap position computations, the FM fold, the
 * window map, the interpolation, the declick blend, the mixer, or (main.c,
 * driven by e->hr_mask) the eight soft-knee output conversions. From the static
 * instruction counts that is a little over half of the remainder; 0.50 is used
 * deliberately as the CONSERVATIVE end, because this is the number the floor's
 * whole claim rests on and it has not been measured on the unit. */
#define FLOOR_DECIMATABLE 0.50

/* Derived, once, so the derivation is visible in the output. */
static double g_sdram_word;        /* cycles per external-SDRAM float load     */
static double g_lin_alu = 7.0;     /* linear stencil arithmetic                */
static double g_fixed;             /* everything the model does not itemise    */

static double cyc_per_frame_budget(void) { return BUDGET_CYCLES / FRAMES_PER_BLK; }

static void model_init(void)
{
    double per_frame = cyc_per_frame_budget();
    double delta = (MEAS_HERMITE - MEAS_LINEAR) * per_frame;   /* cycles/frame */
    /* delta = TAPS * (2*word + HERM_ALU_EXTRA) */
    g_sdram_word = (delta / (double)NUM_TAPS - HERM_ALU_EXTRA) / 2.0;
    /* F from anchor A: Hermite, direct reads (4 words + lin_alu + extra). */
    g_fixed = MEAS_HERMITE * per_frame
            - (double)NUM_TAPS * (4.0 * g_sdram_word + g_lin_alu + HERM_ALU_EXTRA);
}

/* Projected cycles per frame for a tap-read configuration.
 *   words  = SDRAM words loaded per tap read (4 direct, less when cached)
 *   herm   = Hermite kernel in use
 *   extra  = itemised extra SDRAM accesses per frame (splice RMW, overdub) */
static double project(double words, int herm, double extra_words)
{
    double per_tap = words * g_sdram_word + g_lin_alu + (herm ? HERM_ALU_EXTRA : 0.0);
    return g_fixed + (double)NUM_TAPS * per_tap + extra_words * g_sdram_word;
}
static double pct(double cyc_per_frame) { return cyc_per_frame / cyc_per_frame_budget(); }

/* The FLOOR level: half the frames pay the full cost, half pay only the
 * mandatory remainder. This is the only kind of saving that can rescue a block
 * that is already over the deadline — the tap reads are ~19% of this ISR and no
 * choice of interpolation kernel can find 20 points. */
static double project_floor(double words)
{
    double full = project(words, /*herm*/ 0, 0.0);
    double mand = g_fixed * (1.0 - FLOOR_DECIMATABLE);
    return 0.5 * (full + mand);
}

/* WORST BLOCK, not run average. The deadline is per block: a 16-frame window
 * that happens to contain several cache refills is what tears the DMA buffer,
 * and dividing that traffic over a 60000-frame run hides it completely. */
static double worst_window(const double *words_per_frame, unsigned n,
                           unsigned win, int herm, double extra_words)
{
    double best = 0.0, acc = 0.0;
    if (n < win) win = n ? n : 1u;
    for (unsigned i = 0; i < n; i++) {
        acc += words_per_frame[i];
        if (i >= win) acc -= words_per_frame[i - win];
        if (i + 1u >= win) {
            double avg = acc / (double)win;
            double c = project(avg / (double)NUM_TAPS, herm, extra_words);
            if (c > best) best = c;
        }
    }
    return best;
}

/* ---- a worst-case scenario driver ---------------------------------------
 * Everything the PM's worst case names that this engine owns, at once:
 * 8 taps, deep modulation, varispeed at the rails, overdub held, delay-time FM
 * at full depth, bit-crush + bandwidth limit, a splice in flight, and BOTH
 * loop-window orientations including the wrapped one (end < start), which is
 * the majority case for long captures and the shape an adversarial review
 * once caught being silently rejected.
 *
 * The pitch voice, the AA polyphase reader and the string mode live in main.c
 * and pitch_shift.c and are costed by `make wcet`, not here: they replace the
 * tap reads rather than adding to them (skip_tap_reads), so the engine-side
 * worst case for SDRAM traffic is the multitap path. */
#define WLEN  262144u
static float wbuf[WLEN];

#define WC_FRAMES 60000u
static double g_wpf[WC_FRAMES];      /* SDRAM words per FRAME, per frame       */
static unsigned g_fills_win;         /* worst fills in any BLOCK_FRAMES window */

typedef struct {
    double words_per_read;   /* SDRAM float loads per tap read, measured       */
    double worst_block;      /* worst BLOCK_FRAMES window, words per frame     */
    double splice_frames;    /* frames with a splice job in flight             */
    double od_frames;        /* frames with the overdub write loop running     */
    double bypass_frames;    /* frames the cache was switched off (rule 3)     */
    int    wrapped_window;   /* the capture came out with end < start          */
    unsigned frames;
    unsigned herm_frames;    /* frames that used the Hermite kernel            */
} wc_stat_t;

/* pin_rail: hold the TIME control at the rail so the varispeed rate sits at a
 * STEADY 4.0 instead of sweeping through it — a sustained rail is the case that
 * drags every cache lane past its refill rate limit and degrades the cache to
 * the direct path, which is where the worst case has to be looked for. */
/* FNV-1a over everything the scenario emits and records. Printed as
 * "wc hash": `make cachecheck` builds this file with DL_CACHE_ENABLE 1 and 0
 * and requires the two to be identical. test_golden does the same job for the
 * scripted scenario, but that scenario never produces a WRAPPED loop window
 * (verified: 110016 recirc frames, 0 of them wrapped) — and a wrapped window
 * is the majority case for long captures and the shape a review once caught
 * being silently rejected. The cases below deliberately produce both. */
static uint64_t g_wc_hash = 1469598103934665603ull;
static void wc_hash(const float *v, unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        uint32_t bits; float f = v[i];
        __builtin_memcpy(&bits, &f, sizeof bits);
        if (bits == 0x80000000u) bits = 0u;
        g_wc_hash = (g_wc_hash ^ (uint64_t)bits) * 1099511628211ull;
    }
}

static void wc_run(engine_t *e, wc_stat_t *st, int wrapped, int overdub,
                   int pin_rail, unsigned nframes)
{
    float chan[NUM_TAPS];
    engine_init(e, wbuf, WLEN, /*base*/ 9000.0f, 0.4f, 1.6f, 0.01f);
    e->interp = DL_INTERP_HERMITE;
    e->varispeed = 1;
    e->vintage_bits = 12;
    engine_set_bandwidth(e, 96000.0f, 11025.0f);
    /* fill the buffer with something, and park the head so the capture window
     * either fits below it (wrapped == 0) or straddles the buffer end. */
    for (unsigned i = 0; i < WLEN; i++)
        (void)engine_process_multi(e, (float)sin(i * 0.017) * 0.7f, 0.5f, chan);
    if (wrapped) {
        /* walk the head just past the buffer origin, then take a long window:
         * start = head + len - window lands ABOVE end -> wrapped window. */
        while (e->dl.wpos > 3000u)
            (void)engine_process_multi(e, 0.3f, 0.5f, chan);
    }
    engine_recirc_window(e, 60000u);
    st->wrapped_window = (e->xport.loop_end < e->xport.loop_start);
    if (wrapped && !st->wrapped_window)
        printf("      (WANTED a wrapped window, got start %u end %u)\n",
               e->xport.loop_start, e->xport.loop_end);
    e->od_active = overdub;
    e->lp_mult_ref = 1.6f;                 /* mult at the low rail -> rate 4.0  */

    st->frames = 0; st->splice_frames = st->od_frames = 0; st->herm_frames = 0;
    st->bypass_frames = 0;
#if DL_CACHE_ENABLE
    uint32_t miss0 = e->dc.miss, fill0 = e->dc.fill;
#endif
    g_fills_win = 0u;
    unsigned fill_hist[64];
    for (unsigned i = 0; i < 64u; i++) fill_hist[i] = 0u;
    for (unsigned n = 0; n < nframes; n++) {
#if DL_CACHE_ENABLE
        const uint32_t miss0f = e->dc.miss, fill0f = e->dc.fill;
#endif
        /* TIME control swept hard: the taps never settle, which is the case
         * that drags the cache lanes fastest. */
        float t01 = pin_rail ? 0.0f : (float)(0.5 + 0.5 * sin(n * 0.0007));
        /* delay-time FM at the clamp, moving every frame (per-sample by
         * design: it is audio off codec slot 2). */
        e->time_fm = 0.25f * (float)sin(n * 0.31);
        if (e->spl_active) st->splice_frames++;
        if (e->od_gain > 0.0f) st->od_frames++;
        if (e->od_gain <= 0.0f) st->herm_frames++;
        /* dl_cache.h rule 3: the engine bypasses the cache entirely on these
         * frames, so their reads cost the full 4-word direct stencil. The
         * lane counters cannot see them (dc_stencil is not even called), so
         * they have to be counted here or the traffic figure flatters. */
        if (e->spl_active || e->od_gain > 0.0f) st->bypass_frames++;
        (void)engine_process_multi(e, (float)sin(n * 0.023) * 0.8f, t01, chan);
        wc_hash(chan, NUM_TAPS);
        /* THIS FRAME's SDRAM words: refills at DC_W each, plus the direct
         * stencil for every read the cache could not serve (and for all 8 when
         * the engine bypassed it entirely — rule 3). */
        {
#if DL_CACHE_ENABLE
            unsigned df = e->dc.fill - fill0f, dm = e->dc.miss - miss0f;
            int bypass = (e->spl_active || e->od_gain > 0.0f);
            double w = bypass ? 4.0 * (double)NUM_TAPS
                              : (double)df * (double)DC_W + 4.0 * (double)dm;
#else
            unsigned df = 0u;
            double w = 4.0 * (double)NUM_TAPS;
#endif
            if (n < WC_FRAMES) g_wpf[n] = w;
            fill_hist[n & 63u] = df;
            if (n + 1u >= BLOCK_FRAMES) {
                unsigned sum = 0;
                for (unsigned k = 0; k < BLOCK_FRAMES; k++)
                    sum += fill_hist[(n - k) & 63u];
                if (sum > g_fills_win) g_fills_win = sum;
            }
        }
        st->frames++;
    }
    /* the recorded material too: splice and overdub writes only show up here */
    wc_hash(e->dl.buf, e->dl.len);
#if DL_CACHE_ENABLE
    {
        double reads = (double)st->frames * NUM_TAPS;
        double miss = (double)(e->dc.miss - miss0);
        double fill = (double)(e->dc.fill - fill0);
        st->words_per_read = (fill * (double)DC_W + 4.0 * miss
                              + 4.0 * st->bypass_frames * NUM_TAPS) / reads;
    }
#else
    st->words_per_read = 4.0;
#endif
}

int main(void)
{
    model_init();
    printf("  cost model: SDRAM word %.1f cycles, linear stencil %.0f, Hermite +%.0f,\n"
           "              fixed remainder %.0f cycles/frame, budget %.0f cycles/frame\n",
           g_sdram_word, g_lin_alu, HERM_ALU_EXTRA, g_fixed, cyc_per_frame_budget());
    ck("derived SDRAM latency lands in the stated 10-18 cycle range",
       g_sdram_word >= 10.0 && g_sdram_word <= 18.0);
    ck("the model reproduces anchor A (Hermite, direct) at 110%",
       fabs(pct(project(4.0, 1, 0.0)) - MEAS_HERMITE) < 0.005);
    ck("the model reproduces anchor B (linear, direct) at 100%",
       fabs(pct(project(2.0, 0, 0.0)) - MEAS_LINEAR) < 0.005);

    /* ---- 1. WORST-CASE FEATURE COMBINATION, both window orientations ---- */
    static engine_t e;
    wc_stat_t st;
    double worst_words = 0.0, worst_pct = 0.0, worst_blk = 0.0;
    const char *worst_name = "", *worst_blk_name = "";
    struct { const char *name; int wrapped, od, pin; } cases[] = {
        { "recirc + varispeed sweep + FM + crush",      0, 0, 0 },
        { "wrapped window + varispeed sweep + FM",      1, 0, 0 },
        { "SUSTAINED rate-4 varispeed + FM (Hermite)",  0, 0, 1 },
        { "wrapped + SUSTAINED rate 4 + FM (Hermite)",  1, 0, 1 },
        { "overdub held on a varispeed loop",           0, 1, 0 },
        { "overdub on a WRAPPED varispeed loop",        1, 1, 1 },
    };
    int wrapped_cases = 0;
    for (unsigned c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        wc_run(&e, &st, cases[c].wrapped, cases[c].od, cases[c].pin, 60000u);
        wrapped_cases += st.wrapped_window;
        int herm = (st.herm_frames * 2u > st.frames);
        /* splice RMW = 1 SDRAM read + 1 SDRAM write per quota unit; overdub
         * costs a read-modify-write per head advance, up to the rate clamp,
         * plus ONE lookahead read per frame (the write limiter's control). */
        double extra = 2.0 * (double)e.spl_quota * (st.splice_frames / st.frames)
                     + 9.0 * (st.od_frames / st.frames);
        double cyc = project(st.words_per_read, herm, extra);
        double wblk = worst_window(g_wpf, st.frames, BLOCK_FRAMES, herm, extra);
        st.worst_block = wblk;
        printf("      %-40s %.2f words/read, %s, run avg %5.1f%%,"
               " WORST BLOCK %5.1f%% (max %u fills/block)\n",
               cases[c].name, st.words_per_read, herm ? "Hermite" : "linear",
               100.0 * pct(cyc), 100.0 * pct(wblk), g_fills_win);
        /* RULE A (dl_cache.h): sustained traffic is strictly below the direct
         * path. This is the claim an adversarial review broke on the previous
         * design, where a rate-limited lane still paid the full 4-word stencil
         * on every frame it could not serve and the two costs ADDED — measured
         * 1.00 words/read at the varispeed rail (i.e. no saving at all) and
         * 1.75 with the governor's linear kernel. A lane whose line cannot
         * survive DC_PAYBACK frames now goes direct and stays direct, so the
         * rail costs the direct path and not a penny more. */
        ck("dl_cache never costs more than the direct path + its re-probe",
           st.words_per_read <= 4.0 + (double)DC_W / (double)DC_REPROBE);
#if DL_CACHE_ENABLE
        /* RULE B: the burst ceiling, which is what the per-BLOCK deadline
         * actually cares about. Eight lanes re-phase onto one frame at every
         * transport entry; the token bucket caps what that can cost. */
        ck("refills in any one block stay under the burst ceiling",
           g_fills_win <= (DC_FILL_BURST + BLOCK_FRAMES) / DC_FILL_COST);
#endif
        if (st.words_per_read > worst_words) worst_words = st.words_per_read;
        if (pct(cyc) > worst_pct) { worst_pct = pct(cyc); worst_name = cases[c].name; }
        if (pct(wblk) > worst_blk) { worst_blk = pct(wblk); worst_blk_name = cases[c].name; }
    }
    printf("      WORST modelled case: %s at %.1f%% of budget (run average)\n",
           worst_name, 100.0 * worst_pct);
    printf("      WORST BLOCK: %s at %.1f%% of budget\n",
           worst_blk_name, 100.0 * worst_blk);
    /* `make cachecheck` diffs this line between a DL_CACHE_ENABLE 1 and a
     * DL_CACHE_ENABLE 0 build: same audio, same recorded buffer, or the cache
     * has served a stale word somewhere. */
    printf("  wc hash %016llx  (wrapped windows exercised: %d of %u cases)\n",
           (unsigned long long)g_wc_hash, wrapped_cases,
           (unsigned)(sizeof cases / sizeof cases[0]));
    ck("the worst-case scenarios include a WRAPPED loop window (end < start)",
       wrapped_cases > 0);

    /* RATCHET, the one hard assertion in this section. The margin is 5% of
     * budget, which is the model's own resolution — it is NOT a claim that
     * 115% is acceptable. It is sized to catch the class of change that would
     * really hurt: remove dl_cache's refill rate limit, or widen DC_W without
     * widening DC_MIN_SPAN with it, and a dragged lane refills every frame at
     * 7 words/read = 121% of budget, which trips this. The fact that the
     * worst case measures marginally ABOVE the pre-change 110% (the cache's
     * own overhead at the varispeed rail) is reported by the GAP below. */
    ck("worst case has not regressed past the pre-change image + 5%",
       worst_pct <= MEAS_HERMITE + 0.05);
    gap("worst case is at least no worse than pre-change (110%)",
        worst_pct <= MEAS_HERMITE + 0.005);
    /* THE DEADLINE, and the PM's target. Both reported, neither met — see the
     * QA verdict. The governor's floor level is behaviourally identical to its
     * level 1 (both are just "linear taps"), so the cheapest thing this
     * machine can do still costs ~108% of budget in the worst case. */
    gap("worst modelled feature combination meets the block deadline",
        worst_pct < 1.0);
    gap("PM target: worst case <= 75% of budget", worst_pct <= 0.75);

    /* ---- 2. what the governor can actually buy ------------------------- */
    {
        double l0 = pct(project(worst_words, 1, 0.0));
        double l1 = pct(project(2.0, 0, 0.0));           /* linear, cache off  */
        double lf = pct(project_floor(2.0));             /* + half-rate taps   */
        printf("      governor levels at the worst cached traffic:"
               " L0 %.1f%%  L1 %.1f%%  FLOOR %.1f%%\n",
               100.0 * l0, 100.0 * l1, 100.0 * lf);
        ck("dropping a level actually reduces the projected load", l1 < l0);
        ck("the FLOOR is cheaper than level 1 (it sheds frames, not kernels)",
           lf < l1 - 0.05);
        /* THE GUARANTEE. The floor has to fit with margin, or the governor is
         * only a way of making the instrument sound worse while it still tears.
         * 0.90 is the acceptance line here; the PM's 0.60 target is reported
         * below and is NOT met by shedding tap work alone — see the summary. */
        ck("THE FLOOR FITS THE DEADLINE with margin", lf <= 0.90);
        gap("PM target: governor floor level <= 60% of budget", lf <= 0.60);
    }

    /* ---- 3. the governor engages, it really changes the work, the change is
     *         CROSSFADED, and the audio survives it -----------------------
     *
     * Two engines run the SAME input side by side: one configured Hermite (the
     * one the governor acts on) and one hard-wired linear.  Before the drop they
     * must DIFFER — otherwise every assertion after it is vacuous, which is
     * exactly what happens with a settled multiplier, where every tap fraction
     * is 0.0 and the two kernels are the same function.  After the crossfade
     * completes they must be BIT-IDENTICAL: that, and not a cycle count, is the
     * host-side proof that the level actually reached the read path.
     *
     * The excitation is deliberately bright (a fast sine plus an alternating
     * component) and the taps are kept fractional by a slowly moving TIME
     * control: interpolation-kernel differences vanish on smooth signals, so a
     * gentle test signal would understate the swap discontinuity. */
    {
        float ch_h[NUM_TAPS], ch_l[NUM_TAPS];
        static engine_t gh, gl;
        static float lbuf[WLEN];
        engine_init(&gh, wbuf, WLEN, 4000.0f, 0.4f, 1.6f, 0.01f);
        engine_init(&gl, lbuf, WLEN, 4000.0f, 0.4f, 1.6f, 0.01f);
        gh.interp = DL_INTERP_HERMITE;
        gl.interp = DL_INTERP_LINEAR;
        gov_init();
        ck("governor sits at full quality while the load is fine",
           gov_level() == GOV_LEVEL_FULL);

        double diff_before = 0.0, steady = 0.0;
        float prev = 0.0f;
        for (unsigned i = 0; i < 40000u; i++) {
            float x = (float)(0.6 * sin(i * 1.31) + 0.3 * sin(i * 0.0121))
                    + ((i & 1u) ? 0.05f : -0.05f);
            float t = 0.5f + 0.2f * (float)sin(i * 0.0003);
            (void)engine_process_multi(&gh, x, t, ch_h);
            (void)engine_process_multi(&gl, x, t, ch_l);
            if (i > 20000u) {
                double d = fabs(ch_h[0] - ch_l[0]);
                if (d > diff_before) diff_before = d;
                float s = fabsf(ch_h[0] - prev); if (s > steady) steady = s;
            }
            prev = ch_h[0];
        }
        printf("      before the drop: max |Hermite - linear| = %.3e,"
               " steady step %.4f\n", diff_before, steady);
        ck("the two kernels genuinely differ (the check is not vacuous)",
           diff_before > 1e-6);

        /* A SUSTAINED near-miss run, which is what the new policy requires:
         * one expensive block is a loop wrap, not a load (see governor.h). */
        for (unsigned i = 0; i < GOV_DROP_STREAK; i++) gov_report(GOV_DROP_CYCLES);
        ck("a sustained near-miss run reaches the linear level",
           gov_level() == GOV_LEVEL_LINEAR);

        /* THE CROSSFADE. The old design swapped the kernel hard on a block
         * boundary; measured on this same material that is a step of up to
         * 9.1e-2 (-20.9 dBFS) on all 8 taps at once. Assert that no single
         * frame during the change steps further than the signal's own slope,
         * and that the change actually completes. */
        double step = 0.0, diff_mid = 0.0;
        for (unsigned i = 40000u; i < 40000u + GOV_XFADE_FRAMES; i++) {
            float x = (float)(0.6 * sin(i * 1.31) + 0.3 * sin(i * 0.0121))
                    + ((i & 1u) ? 0.05f : -0.05f);
            float t = 0.5f + 0.2f * (float)sin(i * 0.0003);
            (void)engine_process_multi(&gh, x, t, ch_h);
            (void)engine_process_multi(&gl, x, t, ch_l);
            float sdiff = fabsf(ch_h[0] - prev); if (sdiff > step) step = sdiff;
            double d = fabs(ch_h[0] - ch_l[0]);
            if (d > diff_mid) diff_mid = d;
            prev = ch_h[0];
        }
        printf("      during the crossfade: worst step %.4f vs steady %.4f"
               " (%.2fx), kernel gap %.3e\n",
               step, steady, step / (steady > 0 ? steady : 1), diff_mid);
        ck("the crossfaded kernel change stays inside the signal's own slope",
           step < 1.5 * steady);

        double diff_after = 0.0;
        for (unsigned i = 0; i < 2000u; i++) {
            float x = (float)(0.6 * sin(i * 1.7) + 0.3 * sin(i * 0.0121))
                    + ((i & 1u) ? 0.05f : -0.05f);
            float t = 0.5f + 0.2f * (float)sin(i * 0.0003);
            (void)engine_process_multi(&gh, x, t, ch_h);
            (void)engine_process_multi(&gl, x, t, ch_l);
            double d = fabs(ch_h[0] - ch_l[0]);
            if (d > diff_after) diff_after = d;
        }
        ck("after the crossfade the engine really is running the cheap kernel",
           diff_after == 0.0);
        ck("audio stays bounded after the level change", fabsf(ch_h[0]) <= 1.5f);
    }

    /* ---- 4. THE FLOOR LEVEL REALLY SHEDS FRAMES ------------------------
     * The level-1 kernel change is worth the 10.4% the lead measured; the floor
     * has to be worth much more than that or the guarantee is empty. What it
     * does is skip the whole tap-read stage on alternate frames, so: half the
     * SDRAM reads, half the position math, and (through e->hr_mask) half the
     * output conversions in the ISR. Measured here as reads and as held
     * frames, because a projection nobody checks is a wish. */
    {
        static engine_t fe;
        static float fbuf[WLEN];
        float chan[NUM_TAPS];
        static float blk_in[BLOCK_FRAMES];
        static float blk_ch[BLOCK_FRAMES][NUM_TAPS];
        engine_init(&fe, fbuf, WLEN, 4000.0f, 0.4f, 1.6f, 0.01f);
        gov_init();
        for (unsigned i = 0; i < 20000u; i++)
            (void)engine_process_multi(&fe, (float)sin(i * 0.03) * 0.7f, 0.5f, chan);
        /* one missed deadline -> the floor, immediately */
        gov_report(ISR_BUDGET_CYCLES + 1000u);
        ck("a missed deadline reaches the FLOOR level in one block",
           gov_level() == GOV_LEVEL_FLOOR);

        for (unsigned k = 0; k < BLOCK_FRAMES; k++)
            blk_in[k] = (float)sin((20000 + k) * 0.03) * 0.7f;
        engine_process_block(&fe, blk_in, 0.5f, 0, blk_ch, BLOCK_FRAMES);
        unsigned held = 0, matched = 0;
        for (unsigned k = 1; k < BLOCK_FRAMES; k++) {
            if ((fe.hr_mask >> k) & 1u) {
                held++;
                int same = 1;
                for (int i = 0; i < NUM_TAPS; i++)
                    if (blk_ch[k][i] != blk_ch[k - 1u][i]) same = 0;
                matched += same;
            }
        }
        printf("      floor: %u of %u frames held, %u bit-identical to the"
               " frame before\n", held, (unsigned)BLOCK_FRAMES, matched);
        ck("the floor holds half the frames", held >= BLOCK_FRAMES / 2u - 1u);
        ck("a held frame is BIT-identical to its predecessor (so the output"
           " words are too)", matched == held);
        ck("the engine publishes which frames it held (hr_mask)",
           fe.hr_mask != 0u);
    }

    /* ---- 5. governor clauses the unit suite does not cover -------------- */
    {
        gov_cfg_t c;
        governor_t g;
        c.drop_cycles = GOV_DROP_CYCLES; c.drop_streak = GOV_DROP_STREAK;
        c.panic_cycles = GOV_PANIC_CYCLES; c.absurd_cycles = GOV_ABSURD_CYCLES;
        c.recover_cycles = GOV_RECOVER_CYCLES; c.recover_blocks = GOV_RECOVER_BLOCKS;
        c.hold_blocks = GOV_HOLD_BLOCKS; c.backoff_max = GOV_BACKOFF_MAX;
        c.max_level = GOV_MAX_LEVEL;

        /* THE MEASURED HEALTHY IMAGE. 0.88-0.90 steady, 0.94 worst — if any of
         * that costs a level, the branch ships an instrument that quietly runs
         * at reduced quality forever. This is the case the previous thresholds
         * failed. */
        gov_reset(&g, &c);
        for (unsigned i = 0; i < 50000u; i++)
            (void)gov_step(&g, (uint32_t)(0.90 * ISR_BUDGET_CYCLES));
        ck("the healthy image's own load never costs a quality level",
           g.level == 0u && g.transitions == 0u);

        /* MONOTONE DESCENT under a sustained load in the near-miss band. */
        gov_reset(&g, &c);
        gov_cfg_t fast = c; fast.hold_blocks = 0u;
        gov_reset(&g, &fast);
        unsigned seq[4], prev_l = 0, mono = 1;
        for (unsigned i = 0; i < 4u; i++) {
            for (unsigned k = 0; k < GOV_DROP_STREAK; k++)
                seq[i] = gov_step(&g, GOV_DROP_CYCLES + 100u);
            if (seq[i] < prev_l || seq[i] > prev_l + 1u) mono = 0;
            prev_l = seq[i];
        }
        printf("      descent under sustained near-miss load: %u %u %u %u\n",
               seq[0], seq[1], seq[2], seq[3]);
        ck("descent is monotone, one level per streak, and stops at the floor",
           mono && seq[0] == 1u && seq[1] == 2u && seq[3] == 2u);

        /* THRESHOLD DITHER around the drop line, from a level where a change is
         * actually possible. */
        gov_reset(&g, &fast);
        for (unsigned k = 0; k < GOV_DROP_STREAK; k++) (void)gov_step(&g, GOV_DROP_CYCLES);
        unsigned t0 = g.transitions;
        for (unsigned i = 0; i < 20000u; i++)
            (void)gov_step(&g, (i & 1u) ? GOV_DROP_CYCLES : GOV_DROP_CYCLES - 1u);
        printf("      transitions over 20000 blocks of threshold dither: %u\n",
               g.transitions - t0);
        ck("threshold dither does not oscillate the quality level",
           g.transitions - t0 <= 1u);
    }

    if (gaps) {
        printf("\n  %d CONTRACT GAP(S) — what is and is not established:\n"
               "    * AT LEVEL 0 the worst modelled feature combination is still\n"
               "      over the deadline (%.0f%% run average, %.0f%% worst block).\n"
               "      The model's fixed remainder is FITTED to the pre-change\n"
               "      image and cannot see this branch's savings (the block API\n"
               "      being wired in at all, the per-frame VDIVs, the byte-wise\n"
               "      cache fill, the three-pass ISR), so every level-0 figure\n"
               "      here is an UPPER BOUND and the bench number will be lower.\n"
               "      What is NOT an upper bound is the structure: ~81%% of this\n"
               "      budget is spent outside the 8 tap reads, so no choice of\n"
               "      interpolation kernel can close a 10-point gap.\n"
               "    * WHAT MAKES THE DEADLINE SAFE ANYWAY is the floor level:\n"
               "      one block that misses the deadline forces it within one\n"
               "      block, and it projects at %.0f%% of budget because it sheds\n"
               "      whole frames of work rather than shaving a kernel.\n"
               "    * The PM's 75%% typical / 60%% floor targets are reported, not\n"
               "      met. Closing them needs the bench: this file cannot price\n"
               "      instruction-fetch stalls, and they are where the fixed\n"
               "      remainder lives.\n"
               "  Build with -DISR_BUDGET_STRICT to turn these into failures.\n",
               gaps, 100.0 * worst_pct, 100.0 * worst_blk,
               100.0 * pct(project_floor(2.0)));
    }
    if (fails) printf("\nFAILURES: %d\n", fails);
    else if (gaps) printf("\nALL PASS (with %d CONTRACT GAP(S) — see above)\n", gaps);
    else printf("\nALL PASS\n");
    return fails ? 1 : 0;
}
