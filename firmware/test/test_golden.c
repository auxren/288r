/* test_golden.c — the anti-regression baseline for the ISR-budget rework.
 *
 * Two things are asserted here, and they answer two different questions.
 *
 * 1. EQUIVALENCE (bit-exact, host-independent).  engine_process_block() over a
 *    block of 1, 7, 16 and 32 frames must produce byte-identical channel output
 *    and an identical final buffer to the per-frame engine_process_multi() path.
 *    This is the seam contract: the block entry point is where the per-block
 *    hoisting happens, so if hoisting ever changes the audio, this fails.  It is
 *    exact on any host because both sides run the same arithmetic on the same
 *    host — nothing here depends on how the compiler contracts a*b+c.
 *
 * 2. FINGERPRINT (tolerant, cross-host).  Per-phase energy / |sum| / first
 *    moment / peak are compared against recorded constants at 1e-4 relative.
 *    Deliberately NOT a bit hash: -ffp-contract=fast is the compiler default, so
 *    an aarch64 host fuses the Hermite Horner chain into FMAs and an x86-64 host
 *    does not — a committed bit hash would be red on half the machines that run
 *    `make test`.  1e-4 is ~60 dB below the signal: far tighter than any real
 *    behavioural change (a dropped splice, a mis-hoisted window map, a lost FM
 *    lane all move these by percent, not by ppm) and far looser than FP noise.
 *
 * The exact hash is printed on every run anyway.  Within one machine it does not
 * move unless the audio moved, which makes it the working tool during a refactor:
 * note it, refactor, run again, compare.  -DGOLDEN_HASH=0x... turns it into a
 * hard assertion for that purpose.
 */
#include "golden_scenario.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
static void ck(const char *name, int cond) {
    printf("  %-52s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

static float g_buf[GS_LEN];

/* ---- coverage ------------------------------------------------------------
 * A fingerprint baseline is only worth anything if the run still touches the
 * things it claims to. If a future edit quietly stops the overdub engaging or
 * the splice job running, the fingerprints would just shift and the next person
 * would re-baseline them — so count the interesting states and assert them. */
typedef struct {
    unsigned splice, overdub, varispeed, declick, fm, recirc, crush;
} cov_t;

static void cov_note(cov_t *c, engine_t *e)
{
    if (e->spl_active)                   c->splice++;
    if (e->od_gain > 0.0f)               c->overdub++;
    if (e->lp_rate < 0.99f || e->lp_rate > 1.01f) c->varispeed++;
    if (e->declick_n)                    c->declick++;
    if (e->time_fm != 0.0f)              c->fm++;
    if (!transport_should_write(&e->xport)) c->recirc++;
    if (e->vintage_bits > 0)             c->crush++;
}

/* ---- drivers ------------------------------------------------------------- */
static void drv_frame(engine_t *e, const float *in, float t, const float *fm,
                      float (*chan)[NUM_TAPS], unsigned n, void *user)
{
    for (unsigned k = 0; k < n; k++) {
        if (fm) e->time_fm = fm[k];
        (void)engine_process_multi(e, in[k], t, chan[k]);
        if (user) cov_note((cov_t *)user, e);
    }
}

static void drv_block(engine_t *e, const float *in, float t, const float *fm,
                      float (*chan)[NUM_TAPS], unsigned n, void *user)
{
    (void)user;
    engine_process_block(e, in, t, fm, chan, n);
}

/* ---- recorded fingerprints ------------------------------------------------
 * Regenerate ONLY when a change is meant to alter the audio, and say why in the
 * commit message. Order: energy, absum, moment, peak.
 *
 * CHANGELOG (empty so far: every optimisation in the 2026-08 ISR-budget work
 * has come out bit-exact, which is the bar it was held to). */
typedef struct { double energy, absum, moment, peak; } fp_t;
static const fp_t REF[GS_PHASES + 1] = {
    /* phase 0  WRITE, taps sweeping                */ { 4.076158e+04, 9.289537e+04, 1.070551e+05, 9.464567e-01 },
    /* phase 1  RECIRC + varispeed                  */ { 4.219866e+04, 9.651235e+04, 8.733943e+03, 9.248127e-01 },
    /* phase 2  WRITE + FM + 12-bit crush           */ { 3.562626e+04, 9.080051e+04,-1.733997e+05, 8.854008e-01 },
    /* phase 3  RECIRC + overdub + varispeed        */ { 2.987272e+04, 7.953895e+04, 5.491765e+05, 9.362196e-01 },
    /* phase 4  RECIRC re-splice -> WRITE           */ { 3.302951e+04, 8.706226e+04,-1.856166e+05, 9.421390e-01 },
    /* total                                        */ { 1.814887e+05, 4.468094e+05, 3.614641e+05, 9.464567e-01 },
};

static int close_rel(double a, double b, double tol)
{
    double d = a - b; if (d < 0) d = -d;
    double m = (b < 0 ? -b : b);
    if (m < 1e-9) return d < tol;
    return d / m < tol;
}

int main(void)
{
    static gs_acc_t a_frame[GS_PHASES + 1], a_blk[GS_PHASES + 1];
    engine_t e;
    uint64_t h_frame, h_blk;

    /* ---- 1. per-frame reference run ---- */
    static cov_t cov;
    gs_run(&e, g_buf, drv_frame, &cov, 1u, a_frame);
    h_frame = gs_buf_hash(g_buf, GS_LEN);
    printf("  coverage: splice %u overdub %u varispeed %u declick %u fm %u"
           " recirc %u crush %u\n", cov.splice, cov.overdub, cov.varispeed,
           cov.declick, cov.fm, cov.recirc, cov.crush);
    ck("coverage: splice job serviced",   cov.splice   > 100u);
    ck("coverage: overdub engaged",       cov.overdub  > 30000u);
    ck("coverage: varispeed off unity",   cov.varispeed> 30000u);
    ck("coverage: transport declick ran", cov.declick  > 3000u);
    ck("coverage: FM lanes driven",       cov.fm       > 50000u);
    ck("coverage: both transport states", cov.recirc   > 50000u
                                       && cov.recirc   < GS_SAMPLES - 50000u);
    ck("coverage: bit-crush path",        cov.crush    > 50000u);
#if DL_CACHE_ENABLE
    /* SPAN COVERAGE for the CCM window cache. The audio is already proven
     * unchanged (the hashes below); what this checks is that the thing is
     * actually EARNING its 3 KB of CCM across the whole scenario — every
     * transport state, varispeed, FM, overdub and splice included, i.e. with
     * all its own disable rules firing. If a future change quietly makes it
     * bypass everything, the audio stays right and only this notices. */
    {
        uint32_t reads = GS_SAMPLES * (uint32_t)NUM_TAPS;
        printf("  dl_cache: %u reads, %u miss (%.1f%% served), %u fills"
               " (%.2f SDRAM words/read vs 4.00 direct)\n",
               reads, e.dc.miss, 100.0 * (1.0 - (double)e.dc.miss / reads),
               e.dc.fill, (double)e.dc.fill * DC_W / reads
                        + 4.0 * (double)e.dc.miss / reads);
        ck("dl_cache: majority of reads served from CCM",
           e.dc.miss * 2u < reads);
        ck("dl_cache: fewer SDRAM words than the direct path",
           (double)e.dc.fill * DC_W + 4.0 * (double)e.dc.miss < 4.0 * reads);
    }
#endif

    printf("  scenario: %u frames, %u ch-samples/phase-total\n",
           GS_SAMPLES, a_frame[GS_PHASES].n);
    for (unsigned p = 0; p <= GS_PHASES; p++)
        printf("  %-9s energy %.6e absum %.6e moment %.6e peak %.6e\n",
               p == GS_PHASES ? "TOTAL" : "phase", a_frame[p].energy,
               a_frame[p].absum, a_frame[p].moment, a_frame[p].peak);
    printf("  out hash %016llx   buf hash %016llx\n",
           (unsigned long long)a_frame[GS_PHASES].hash, (unsigned long long)h_frame);

    /* ---- 2. block path must be bit-identical at every batch size ---- */
    {
        const unsigned sizes[] = { 1u, 7u, 16u, 32u };
        for (unsigned s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
            char nm[64];
            gs_run(&e, g_buf, drv_block, 0, sizes[s], a_blk);
            h_blk = gs_buf_hash(g_buf, GS_LEN);
            snprintf(nm, sizeof nm, "block n=%u: output bit-identical", sizes[s]);
            ck(nm, a_blk[GS_PHASES].hash == a_frame[GS_PHASES].hash);
            snprintf(nm, sizeof nm, "block n=%u: delay buffer bit-identical", sizes[s]);
            ck(nm, h_blk == h_frame);
        }
    }

    /* ---- 3. fingerprints vs the recorded baseline ---- */
    for (unsigned p = 0; p <= GS_PHASES; p++) {
        char nm[64];
        int ok = close_rel(a_frame[p].energy, REF[p].energy, 1e-4)
              && close_rel(a_frame[p].absum,  REF[p].absum,  1e-4)
              && close_rel(a_frame[p].moment, REF[p].moment, 1e-4)
              && close_rel(a_frame[p].peak,   REF[p].peak,   1e-4);
        if (p == GS_PHASES) snprintf(nm, sizeof nm, "fingerprint: TOTAL matches baseline");
        else                snprintf(nm, sizeof nm, "fingerprint: phase %u matches baseline", p);
        ck(nm, ok);
    }

    /* ---- 4. optional exact-hash gate (development / single-host use) ---- */
#ifdef GOLDEN_HASH
    ck("exact output hash matches -DGOLDEN_HASH",
       a_frame[GS_PHASES].hash == (uint64_t)GOLDEN_HASH);
#endif

    printf(fails ? "\nFAILURES: %d\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
