/* pitch_shift.h — crossfaded-tap ("H910-style") pitch shifter over delay_line.
 *
 * A read mode on the fixed-rate buffer: two fractional taps a half-window apart,
 * their delay ramped so the read rate = pitch ratio, crossfaded so the ramp wrap
 * is silent. Subtle ratios (≈1.0) give detune/chorus; large ratios give shift.
 * Feed it a delay_line that something else is writing (e.g. the main engine).
 */
#ifndef PITCH_SHIFT_H
#define PITCH_SHIFT_H

#include "delay_line.h"

typedef struct {
    float ratio;    /* pitch ratio: 2=+1oct, 0.5=-1oct, 1=unity (see gotcha)     */
    float window;   /* crossfade window length W in samples (grain length)       */
    float base;     /* base delay offset in samples; keeps both taps in range    */
    float phase;    /* fracA accumulator in [0,1)                                */
    /* --- de-glitch (H949-style correlation-aligned splices) ---------------- */
    float off[2];    /* per-tap splice offsets (>=0, deeper into the past)       */
    float rho[2];    /* per-tap splice coherence (NCC of the chosen offset, 0..1):
                        drives the ADAPTIVE fade law — coherent grains fade
                        amplitude-complementary (flat sum), incoherent grains
                        fade power-complementary (flat power). Fixes the -3 dB
                        mid-fade AM dip on unaligned material.                   */
    volatile float pend_rho;  /* coherence for the NEXT wrap (same protocol)     */
    volatile float pend_off;  /* offset chosen by ps_service for the NEXT wrap   */
    /* --- up-shift anti-aliasing (polyphase band-limited read) -------------- */
    /* Per-grain streaming sample cache: 32 consecutive buffer samples at
     * absolute positions [aapos, aapos+31]. The grain's read point advances
     * FORWARD through the buffer at `ratio` per output sample for up-shifts,
     * so refills amortize to ~2*ratio SDRAM loads/sample instead of 32
     * (adversarial-verify blocker: uncached SDRAM loads blew the ISR budget). */
    float    aawin[2][32];
    uint32_t aapos[2];        /* absolute index of aawin[g][0]; ~0u = invalid    */
    /* Fast coefficient rows published by the platform (e.g. CCM copy of the
     * active band). NULL / band mismatch -> read from the const flash tables. */
    const float (*aarows)[16];
    int      aarows_band;     /* band aarows holds, -1 = none                    */
    volatile int aaband_req;  /* band the ISR wants published (platform reads)   */
    int      aa_bypass;       /* test hook: 1 = force Hermite path (no AA)       */
    /* --- looper awareness (#19) -------------------------------------------
     * When the engine RECIRCs, the write head snaps back at the loop boundary
     * — every read must window-map or the grains fetch garbage (bench: ~100+
     * full-scale overrange events/s with silent input). span==0 = live path. */
    uint32_t lp_start, lp_end, lp_span;
    /* --- period-adaptive splice search (bass reach) ------------------------ */
    /* A background autocorrelation (idle ps_service calls) estimates the
     * source period; confident LOW material widens the splice search to cover
     * ~2 periods, extending clean splicing from ~125 Hz down to ~35 Hz.
     * (H949 principle: time the splice by the signal's own period.) */
    float    period;          /* estimated source period, samples (0 = none)     */
    float    per_conf;        /* normalized periodicity confidence 0..1          */
    int      per_tick;        /* idle-call divider for the background scan       */
    volatile int   pend_tap;  /* which tap it is for (-1 = none). PROTOCOL:
                                 service writes pend_off, BARRIER, then pend_tap;
                                 the ISR reads pend_tap first and consumes both.  */
    int   next_tap;  /* which tap wraps next (0=A at phase 1->0, 1=B at 0.5)     */
} pitchshift_t;

/* window: W in samples (e.g. 30 ms * fs). base >= 2; the de-glitch search reads
 * to base + W + ~1600, so keep base + W + 1600 <= len - 3. */
void  ps_init(pitchshift_t *p, float window, float base);
/* Publish fast coefficient rows for `band` (e.g. a CCM copy; rows layout =
 * aa_tables.h [AA_PHASES+1][AA_TAPS]). Call with band=-1/rows=NULL to revoke
 * BEFORE overwriting the memory, then re-publish when the copy is complete. */
void  ps_set_aa_rows(pitchshift_t *p, int band, const float (*rows)[16]);
/* The const flash coefficient rows for `band` (source for platform copies). */
const float (*ps_aa_flash_rows(int band))[16];
void  ps_set_ratio(pitchshift_t *p, float ratio);   /* clamp to a sane span      */
void  ps_reset(pitchshift_t *p);                     /* phase = 0                 */

/* Looper window (#19): call at RECIRC entry/exit. span 0 (start==end) = live
 * path. While a window is set, reads window-map through dl_read_loop_frac and
 * the AA path is bypassed (its streaming cache reads raw sequential addresses
 * and cannot cross the seam) — loop playback uses the exact Hermite reads. */
void  ps_set_loop_window(pitchshift_t *p, uint32_t start, uint32_t end,
                         uint32_t len);

/* One output sample. `d` is the (externally written) delay line. */
float ps_process(pitchshift_t *p, const delay_line_t *d, dl_interp_t interp);

/* De-glitch service — call from the MAIN LOOP (not the ISR): when a tap wrap is
 * imminent it runs a correlation search (a few hundred k MACs, ~1-2 ms — far too
 * heavy for the ISR, trivial in the superloop) and posts the splice offset the
 * ISR consumes at the wrap. Without it, splices land at offset 0 = the plain
 * crossfaded shifter (graceful fallback). */
void ps_service(pitchshift_t *p, const delay_line_t *d);

#endif /* PITCH_SHIFT_H */
