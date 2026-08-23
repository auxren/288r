/* dl_cache.h — per-tap streaming window cache for the delay-line reads.
 *
 * WHY.  The Cortex-M4 has NO data cache and the delay buffer is in external
 * SDRAM over the FMC (~10-18 cycles a word; nobody has pinned it down more
 * precisely on this board).  The engine does four of those loads per tap per
 * sample — 32 SDRAM words a sample — and the field measurements put the core
 * per-sample read path at the top of an ISR running 110-117% of its budget.
 *
 * The observation this exploits: a tap read position walks FORWARD roughly one
 * sample per frame, and each frame's 4-word stencil overlaps the previous one by
 * three words.  So fetch a contiguous RUN of DC_W words once and read out of it
 * for the next ~DC_W frames.  Sequential SDRAM reads are also the FMC's best
 * case (same row, burstable) where scattered ones are its worst.
 *
 *      direct :  4 SDRAM words per frame per tap
 *      cached : ~1 SDRAM word per frame per tap at drift 1, plus four
 *               zero-wait CCM words
 *
 * IT CANNOT CHANGE THE AUDIO.  dc_stencil() either returns a pointer to four
 * words identical to the ones the direct path would have loaded, or NULL and the
 * caller does exactly what it did before.  Every decision in here — refill rate,
 * lifetime, enable — is therefore a pure performance decision.  The ONLY way
 * this module can hurt is by returning STALE words, so all the care below is
 * about coherence and nothing else.
 *
 * COHERENCE RULES, each one a hazard that was reasoned about:
 *
 *  1. CLEARANCE, both ends.  In WRITE the head is the only writer and it moves
 *     forward one sample a frame.  A line filled for a tap at integer delay
 *     d_int spans buffer indices [a0-2, a0-2+DC_W), and the head sits d_int
 *     samples ABOVE a0 — so if d_int > DC_W + 8 the head is already past the
 *     top of the line at fill time and is walking away from it.  It can only
 *     come back by lapping the whole buffer, which takes len - d_int + DC_W
 *     frames; requiring d_int + DC_LIFE + 8 < len makes that longer than the
 *     line is allowed to live.  Both conditions are one compare each.
 *  2. LIFETIME.  Every line expires after DC_LIFE frames whether it is used or
 *     not.  It is what makes rule 1's lap argument finite, and it also covers
 *     the long stretches where the taps are not read at all (pitch and string
 *     modes set skip_tap_reads) — rule 1 alone only reasons about a line that
 *     is being consumed.
 *  3. FOREIGN WRITERS.  The cache is switched off whole while anything else
 *     writes where taps read: the overdub write loop (writes at the recirc head,
 *     which taps on a short loop do read behind) and the seam splice job
 *     (rewrites the window tail, which is exactly where taps read right after a
 *     capture — a stale line there would silently un-do the splice).
 *  4. TRANSITIONS.  Every transport change invalidates every line: the recirc
 *     entries move the head, remap the window and write guard samples past the
 *     seam.
 *  5. WRAP.  A stencil or a fill run that would cross the end of the buffer is
 *     never served from cache; it falls back to the general path.
 *
 * WHAT MAKES IT BOUNDED (contract clause C-3a).  A lane may not refill again
 * until DC_MIN_SPAN frames after its last fill; until then it reads SDRAM
 * directly.  So the refill traffic is at most DC_W/DC_MIN_SPAN = 3 words per
 * frame per lane, which is BELOW the 4 words the direct path costs — the cache
 * cannot be worse than no cache, however hard a tap's read position is being
 * dragged (a preset recall, a hard multiplier sweep, varispeed at the 4.0 rail).
 * That bound is per FRAME and holds for both entry points, so it does not
 * depend on the still-unsettled block size (contract blocker #0).
 *
 * NOT YET MEASURED ON HARDWARE.  The saving depends on the FMC's real word
 * latency and on whether the compiler turns the fill into multi-word bursts.
 * DL_CACHE_ENABLE flips the whole thing (including the allocation) in one
 * define; the bench gate is isr_pk with it on vs off in TIME/RECIRC.
 */
#ifndef DL_CACHE_H
#define DL_CACHE_H

#include <stdint.h>

/* ONE DEFINE REVERTS IT (the deal made with the bench): 0 = every read goes
 * straight to SDRAM exactly as before, and the struct is not even allocated. */
#ifndef DL_CACHE_ENABLE
#define DL_CACHE_ENABLE 1
#endif

/* Samples per lane. 96 words = 384 B a lane, 3 KB for eight — CCM has ~57 KB
 * free. Sized so ONE refill covers a whole 32-frame block at drift up to
 * (DC_W-4)/32 = 2.9 samples per frame: normal play, chorus/flanger FM (the
 * per-tap slew limiter caps read-position drift at 1.5/frame) and varispeed up
 * to ~2.9x all fit; above that the lane hits the refill rate limit and goes
 * direct. */
#ifndef DC_W
#define DC_W 96u
#endif
#ifndef DC_LANES
#define DC_LANES 8u
#endif
/* Minimum frames between refills of one lane. DC_W/DC_MIN_SPAN must stay under
 * 4 — that inequality is the whole "can never be worse than the direct path"
 * guarantee. 96/32 = 3. */
#ifndef DC_MIN_SPAN
#define DC_MIN_SPAN 32u
#endif
/* Frames a line may live (rule 2). Must stay well under the number of frames
 * the write head needs to lap the buffer back onto a line — rule 1's second
 * condition enforces that against the actual buffer length. 256 frames is ~2.7
 * ms; a line is normally consumed and refilled long before that (drift of one
 * sample a frame walks off a 96-word line in 92), so this is a backstop, not
 * the thing that sets the refill rate. It was 64 in the first cut, which made
 * expiry — not drift — the binding constraint: measured 8 fills per 500 frames
 * instead of 5. */
#ifndef DC_LIFE
#define DC_LIFE 256u
#endif

typedef struct {
    uint32_t base;              /* buffer index of line[0]                     */
    uint32_t age;               /* frames since fill (rule 2)                  */
    uint8_t  valid;
    float    line[DC_W];
} dl_line_t;

typedef struct {
    dl_line_t lane[DC_LANES];
    uint8_t   on;               /* off = every read goes straight to SDRAM     */
    /* Telemetry counted on the MISS path only — a counter on the hit path would
     * be three instructions x 8 taps x every sample, ~1.4% of the block budget
     * to measure a thing that is supposed to be saving budget. Misses are rare
     * by construction, and hits = reads - miss for anyone who wants the ratio
     * (host tests do; so will the SWD snapshot). */
    uint32_t  miss;             /* reads not served from a line                */
    uint32_t  fill;             /* subset of miss that refilled                */
} dl_cache_t;

static inline void dc_init(dl_cache_t *c)
{
    for (unsigned i = 0; i < DC_LANES; i++) {
        c->lane[i].valid  = 0u;
        c->lane[i].base   = 0u;
        c->lane[i].age    = DC_MIN_SPAN;
    }
    c->on = 1u;
    c->miss = c->fill = 0u;
}

/* Drop every line (rules 3 and 4). */
static inline void dc_invalidate(dl_cache_t *c)
{
    for (unsigned i = 0; i < DC_LANES; i++) c->lane[i].valid = 0u;
}

/* Start of a run of `frames` frames: age the lines and expire the old ones.
 * `enable` is the caller's "nothing else is writing where taps read" verdict.
 * Age is what both the lifetime rule and the refill rate limit are built on, so
 * `frames` must be the true number of audio frames about to be processed — 1
 * from the per-sample API, n from the block API. */
static inline void dc_block_begin(dl_cache_t *c, int enable, unsigned frames)
{
    c->on = (uint8_t)(enable ? 1 : 0);
    for (unsigned i = 0; i < DC_LANES; i++) {
        dl_line_t *L = &c->lane[i];
        if (L->age < 0xFFFF0000u) L->age += frames;   /* saturate, never wrap */
        if (L->age > DC_LIFE) L->valid = 0u;
    }
}

/* Four contiguous samples at buffer indices a0-2 .. a0+1, from CCM if it can,
 * else NULL and the caller uses the direct path. `d_int` is the tap's integer
 * delay — rule 1's write-head clearance test. */
static inline const float *dc_stencil(dl_cache_t *c, unsigned lane,
                                      const float *buf, uint32_t len,
                                      uint32_t a0, uint32_t d_int)
{
    dl_line_t *L = &c->lane[lane];
    if (a0 < 2u || a0 + 1u >= len) return 0;           /* rule 5              */
    {
        /* HIT TEST, four instructions: unsigned wraparound turns "below the
         * line" into a huge offset, so one compare covers both ends.
         *
         * The compare must be `off <= DC_W-4`, NOT `off+4 <= DC_W`. The second
         * form has a hole: a read exactly one to four samples BELOW the line
         * gives off = 0xFFFFFFFC..0xFFFFFFFF, and off+4 wraps to 0..3, which
         * passes — and hands back a pointer four gigabytes out of range. Caught
         * by test_golden segfaulting the moment the cache was wired in, which
         * is the entire argument for having written that fixture first. */
        uint32_t off = (a0 - 2u) - L->base;
        if (L->valid && off <= DC_W - 4u) return &L->line[off];
    }
    c->miss++;
    if (!c->on)                             return 0;
    if (L->age < DC_MIN_SPAN)               return 0;  /* refill rate limit   */
    if (d_int <= DC_W + 8u)                 return 0;  /* rule 1, near end    */
    if (d_int + DC_LIFE + 8u >= len)        return 0;  /* rule 1, far end     */
    {
        uint32_t src = a0 - 2u;
        if (src + DC_W > len) return 0;                /* rule 5: fill would wrap */
        /* Sequential fill in 16-byte chunks. Written as __builtin_memcpy of a
         * constant 16 bytes rather than four float assignments SPECIFICALLY to
         * get the codegen right, verified with objdump:
         *   - four float assignments -> gcc interleaves ldr/str through one
         *     scratch register, so every SDRAM read is an isolated NONSEQ
         *     transfer;
         *   - one memcpy of the whole 384 bytes -> a CALL to memcpy, which in
         *     this freestanding build (bsp/freestanding.c) is a BYTE loop.
         *     384 byte-reads off the FMC would be a catastrophe;
         *   - 16 bytes at a time -> four back-to-back sequential ldr then four
         *     str, which is what lets the FMC's read burst (RBURST is set in
         *     SDCR1, bsp/sdram.c) actually stream.
         * If this ever needs more, the next step is inline LDM/STM asm — but
         * that is only worth doing against a bench measurement. */
        {
            const float *s = buf + src;
            float *d = L->line;
            for (unsigned k = 0; k < DC_W; k += 4u)
                __builtin_memcpy(&d[k], &s[k], 4u * sizeof(float));
        }
        L->base  = src;
        L->age   = 0u;
        L->valid = 1u;
        c->fill++;
        return &L->line[0];
    }
}

#endif /* DL_CACHE_H */
