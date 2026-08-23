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
 * WHAT MAKES IT BOUNDED (contract clause C-3a).  The first cut argued this from
 * the per-lane refill rate limit alone: "at most DC_W/DC_MIN_SPAN = 3 words per
 * frame per lane, below the 4 the direct path costs, so the cache can never be
 * worse".  THAT ARGUMENT WAS WRONG, and an adversarial review measured it wrong
 * at exactly the place it named (varispeed at the 4.0 rail, 1.00 words/read; a
 * preset-recall slam, 1.66; both against 4.00 for the direct path it is supposed
 * to be beating -- i.e. no saving at the rail).  The hole: a rate-limited lane
 * still pays the FULL 4-word direct stencil on every frame it cannot serve, and
 * the two costs ADD.  Refill traffic is not substitutive unless the line it
 * bought actually gets used.
 *
 * So the bound is now made of two rules that are each true on their own:
 *
 *  A. SELF-FINANCING (steady state).  A fill costs DC_W words and replaces 4
 *     words per frame of service, so it only pays for itself if the line lives
 *     at least DC_W/4 = DC_PAYBACK frames.  A lane may only refill if its
 *     PREVIOUS line lasted that long (L->life, recorded when a line stops
 *     serving).  A lane whose read position is being dragged too fast to hold a
 *     line therefore goes direct and STAYS direct -- which is the correct
 *     behaviour at the varispeed rail, where the cache has nothing to offer.
 *     Every lane gets one speculative fill after an invalidate (there is no
 *     history then), and re-probes once every DC_REPROBE frames so a lane that
 *     went direct during a sweep comes back when the sweep stops.  The re-probe
 *     costs DC_W words per DC_REPROBE frames per lane = 0.02 words/frame.
 *
 *  B. BURST CEILING (the block deadline).  The deadline is per BLOCK, and rule A
 *     is an average.  Eight lanes are free to re-phase onto the same frame (every
 *     transport entry invalidates all eight at once), which is 8 x DC_W = 768
 *     SDRAM words in ONE frame -- 21% of a whole block budget in refills alone.
 *     A token bucket now caps that: +1 token per frame, cap DC_FILL_BURST, and a
 *     fill costs DC_FILL_COST.  Worst case for an n-frame block is therefore
 *     (DC_FILL_BURST + n) / DC_FILL_COST fills, = 3 fills = 288 words for n = 16,
 *     and it is a compile-time bound that `make wcet` can use.
 *
 * The honest claim, then: SUSTAINED traffic is strictly below the direct path
 * (rule A), and the WORST BLOCK is bounded above it by a known, small constant
 * (rule B).  Not "can never be worse" -- that was never true.
 *
 * THE FILL MUST BE WORD-GRANULAR.  It is written through a may_alias uint32_t
 * pointer, four words at a time, and that is not a stylistic choice: with
 * -flto the __builtin_memcpy this used to call bound to the freestanding
 * BYTE-LOOP memcpy (bsp/freestanding.c) in one of the two inlined copies of the
 * tap loop, and gcc unrolled it as 16 x ldrb/strb per 16 bytes.  That is 384
 * byte reads off the FMC per fill instead of 96 word reads -- verified in
 * objdump on the -O3 image (bsp_audio_isr+0x26bc), and it is the kind of thing
 * that only ever shows up as "the cache made it slower".  Word loads also let
 * the FMC read burst (RBURST in SDCR1) stream.
 *
 * NOT YET MEASURED ON HARDWARE.  The saving depends on the FMC's real word
 * latency.  DL_CACHE_ENABLE flips the whole thing (including the allocation) in
 * one define; the bench gate is isr_pk with it on vs off in TIME/RECIRC.
 */
#ifndef DL_CACHE_H
#define DL_CACHE_H

#include <stdint.h>

/* The fill's transfer unit. may_alias because the delay buffer is float and the
 * copy is a bit move; without it -O3 is entitled to assume the loads and the
 * float stores elsewhere in the frame cannot touch the same memory. */
typedef uint32_t dc_word_t __attribute__((may_alias));

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
/* Rule A: a fill of DC_W words replaces 4 words/frame, so it breaks even after
 * DC_W/4 frames of service. A lane may not refill unless its previous line
 * lasted at least this long. */
#ifndef DC_PAYBACK
#define DC_PAYBACK (DC_W / 4u)
#endif
/* Rule A, re-probe: a lane that went direct tries once more this often, so a
 * lane parked direct by a sweep comes back when the sweep stops. DC_W words per
 * DC_REPROBE frames per lane = 0.023 words/frame — three orders below the
 * traffic it is trying to recover. */
#ifndef DC_REPROBE
#define DC_REPROBE 4096u
#endif
/* Rule B, the burst ceiling: +1 token/frame, a fill costs DC_FILL_COST, bucket
 * cap DC_FILL_BURST. Fills in an n-frame block <= (BURST + n)/COST. */
#ifndef DC_FILL_COST
#define DC_FILL_COST 8u
#endif
#ifndef DC_FILL_BURST
#define DC_FILL_BURST 8u
#endif

typedef struct {
    uint32_t base;              /* buffer index of line[0]                     */
    uint32_t age;               /* frames since fill (rule 2)                  */
    uint32_t life;              /* frames the PREVIOUS line served (rule A)    */
    uint8_t  valid;
    float    line[DC_W];
} dl_line_t;

typedef struct {
    dl_line_t lane[DC_LANES];
    uint32_t  tokens;           /* rule B refill-burst bucket                  */
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
        c->lane[i].life   = DC_PAYBACK;   /* one speculative fill per lane */
    }
    c->tokens = DC_FILL_BURST;
    c->on = 1u;
    c->miss = c->fill = 0u;
}

/* Drop every line (rules 3 and 4).
 *
 * This is also where every lane gets its one speculative fill back: after a
 * transport transition nothing is known about how long a line will survive, and
 * refusing to cache until a line has proven itself would mean never caching
 * again. The burst bucket (rule B) is what keeps the eight of them from landing
 * on the same frame, so it is deliberately NOT refilled here. */
static inline void dc_invalidate(dl_cache_t *c)
{
    for (unsigned i = 0; i < DC_LANES; i++) {
        c->lane[i].valid = 0u;
        c->lane[i].life  = DC_PAYBACK;
    }
}

/* Start of a run of `frames` frames: age the lines and expire the old ones.
 * `enable` is the caller's "nothing else is writing where taps read" verdict.
 * Age is what both the lifetime rule and the refill rate limit are built on, so
 * `frames` must be the true number of audio frames about to be processed — 1
 * from the per-sample API, n from the block API. */
static inline void dc_block_begin(dl_cache_t *c, int enable, unsigned frames)
{
    if (!enable) {
        if (!c->on) return;           /* already off: nothing to drop */
        /* `enable` is the COHERENCE kill switch (rule 3), not a refill switch:
         * a line that is still marked valid would keep being served out of CCM
         * while a foreign writer moves the buffer underneath it. Turning the
         * cache off has to drop the lines, or "off" does not mean off. (It did
         * not: dc_stencil's hit test ran before the `on` check, so a disabled
         * cache still served stale words — the test that claimed to cover this
         * invalidated first and could never see it.) */
        c->on = 0u;
        dc_invalidate(c);
        return;
    }
    c->on = 1u;
    c->tokens += frames;                              /* rule B: +1 per frame */
    if (c->tokens > DC_FILL_BURST) c->tokens = DC_FILL_BURST;
    for (unsigned i = 0; i < DC_LANES; i++) {
        dl_line_t *L = &c->lane[i];
        if (L->age < 0xFFFF0000u) L->age += frames;   /* saturate, never wrap */
        if (L->age > DC_LIFE && L->valid) {
            L->life  = L->age;      /* it served to expiry: it paid for itself */
            L->valid = 0u;
        }
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
    if (!c->on) return 0;             /* OFF MEANS OFF — before the hit test  */
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
    /* The line (if there was one) has stopped serving: record how long it did,
     * which is what rule A spends. */
    if (L->valid) { L->life = L->age; L->valid = 0u; }
    if (L->age < DC_MIN_SPAN)               return 0;  /* refill rate limit   */
    /* RULE A, self-financing: refill only if the previous line lived long
     * enough to pay for its own DC_W words, or if enough frames have passed to
     * be worth one cheap re-probe. */
    if (L->life < DC_PAYBACK && L->age < DC_REPROBE) return 0;
    if (c->tokens < DC_FILL_COST)           return 0;  /* rule B, burst cap   */
    if (d_int <= DC_W + 8u)                 return 0;  /* rule 1, near end    */
    if (d_int + DC_LIFE + 8u >= len)        return 0;  /* rule 1, far end     */
    {
        uint32_t src = a0 - 2u;
        if (src + DC_W > len) return 0;                /* rule 5: fill would wrap */
        /* Sequential fill, FOUR WORDS AT A TIME, through a may_alias 32-bit
         * pointer. The four loads are issued before the four stores, so the FMC
         * sees four back-to-back sequential reads and its read burst (RBURST in
         * SDCR1, bsp/sdram.c) can stream them.
         *
         * This used to be __builtin_memcpy of a constant 16 bytes, with a
         * comment saying objdump had confirmed word transfers. It had — for ONE
         * of the two inlined copies of the tap loop. In the other, -flto bound
         * the builtin to the freestanding BYTE-LOOP memcpy and unrolled it as
         * 16 x ldrb/strb per 16 bytes: 384 single-byte FMC reads per fill.
         * Never write a hot copy as memcpy in a -nostdlib image; the compiler is
         * allowed to call whatever memcpy the link provides. */
        {
            const dc_word_t *s = (const dc_word_t *)(const void *)(buf + src);
            dc_word_t *d = (dc_word_t *)(void *)L->line;
            for (unsigned k = 0; k < DC_W; k += 4u) {
                dc_word_t w0 = s[k], w1 = s[k + 1u], w2 = s[k + 2u], w3 = s[k + 3u];
                d[k] = w0; d[k + 1u] = w1; d[k + 2u] = w2; d[k + 3u] = w3;
            }
        }
        L->base  = src;
        L->age   = 0u;
        L->valid = 1u;
        c->tokens -= DC_FILL_COST;
        c->fill++;
        return &L->line[0];
    }
}

#endif /* DL_CACHE_H */
