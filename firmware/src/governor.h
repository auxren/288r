/* governor.h — the ISR load governor: clause C-3c of the budget contract.
 *
 * THE ARGUMENT. `make wcet` proves a STATIC CEILING for each quality level;
 * level 0 (full quality) is allowed to sit near 1.00 of budget, and the FLOOR
 * level must be proven to fit with margin. That proof is only useful if we can
 * always REACH the floor level, and reach it fast enough that an overrun cannot
 * repeat. So: the ISR measures its own cycle count (DWT, already there for
 * telemetry) and hands it to gov_report(). A block that actually MISSED the
 * deadline costs every level on the spot — the next block already runs at the
 * floor. Therefore no more than one consecutive block can overrun, and with the
 * static floor ceiling proven, sustained overrun is impossible. The audible
 * failure this replaces is the field's "fizzy static": blocks that miss the DMA
 * deadline tear the output buffer, every pass, forever.
 *
 * WHAT A LEVEL COSTS THE LISTENER is the whole reason the thresholds below are
 * where they are. A level change is a change of DSP quality — it is not free, it
 * is not silent, and it must not be spent on anything but an actual emergency.
 * The first cut of this file got that wrong in a way worth recording, because
 * the shape of the mistake is generic:
 *
 *   - it dropped a level at 0.88 of budget. The lead's own measurements of the
 *     HEALTHY image are 88-90% steady and 94% worst. So the governor would have
 *     latched to its floor on the first audio block, on a unit that was working
 *     perfectly, and stayed there: recovery wanted 1500 consecutive blocks below
 *     0.60, and TIME mode has never been measured below 0.76 in its life. The
 *     branch would have shipped Hermite permanently disabled and every
 *     bit-exactness proof taken in a state the unit is never in.
 *
 * The rules that follow from that:
 *
 *  1. DROP ONLY ON EVIDENCE OF A MISS. Panic (>= the deadline) is unambiguous:
 *     that block tore, act immediately. Below the deadline, a single expensive
 *     block is NORMAL (a loop wrap, a splice arm, an AA republish, a transport
 *     transition) — it costs a level only if it is SUSTAINED, i.e.
 *     GOV_DROP_STREAK consecutive blocks in the near-miss band.
 *  2. RECOVERY MUST BE REACHABLE. The recover threshold has to sit ABOVE the
 *     normal operating load of the mode we are in, or "recovery" is a synonym
 *     for "never". It is set from the measured steady state with headroom, and
 *     below the drop band by a hysteresis gap.
 *  3. A MEASUREMENT IS NOT ALWAYS A LOAD. A flash sector erase stalls every
 *     instruction fetch in the machine for ~0.5 s (bsp/flash_preset.c) and the
 *     ISR's DWT delta for that block reads in the tens of millions of cycles.
 *     That is not information about DSP cost, and treating it as one meant that
 *     SAVING A PRESET permanently degraded the audio until the next power cycle.
 *     Blocks above GOV_ABSURD_CYCLES are discarded, and long stalls are also
 *     expected to bracket themselves with gov_suspend()/gov_resume().
 *  4. CHANGES ARE RATE-LIMITED. GOV_HOLD_BLOCKS between changes bounds how often
 *     the listener can be made to hear one, whatever the programme material does.
 *     The engine ALSO crossfades the kernel change over ~5 ms (engine.c) — the
 *     hard swap this file used to assume was safe measured -3.3 dB at 20 kHz
 *     applied to all 8 taps on one sample boundary, which is the same
 *     un-crossfaded-table-swap class as issue #24 layer 2.
 *  5. FLUTTER MUST DIE OUT. Load here is program-dependent and PERIODIC — an
 *     echo pattern re-crosses the same loop wrap every pass. A fixed recovery
 *     window turns that into a metronome: spike, drop, quiet, recover, spike.
 *     The recovery window therefore DOUBLES with each drop (up to backoff_max)
 *     and halves back on each recovery, so a repeating spike converges on
 *     "stay one level down" instead of oscillating at the loop rate — which is
 *     what the original design argument claimed a slow recovery alone would do,
 *     and it does not: measured 41 level changes in 20 s of a once-per-second
 *     spike before this rule existed.
 *
 * WHY NOT AVERAGE THE LOAD. Averaging is exactly wrong for a deadline: the
 * deadline is per block, and the expensive blocks are the rare ones. The
 * governor therefore reacts to the WORST block — but only when the worst block
 * is either over the line or refuses to stop.
 *
 * Pure integer, no BSP, no float: host-tested by test/test_governor.c.
 */
#ifndef GOVERNOR_H
#define GOVERNOR_H

#include <stdint.h>

typedef struct {
    uint32_t drop_cycles;     /* near-miss band floor (see drop_streak)      */
    uint32_t drop_streak;     /* consecutive near-miss blocks before a drop  */
    uint32_t panic_cycles;    /* one block >= this -> straight to the floor  */
    uint32_t absurd_cycles;   /* >= this is a stall artefact, not a load     */
    uint32_t recover_cycles;  /* blocks below this count toward recovery     */
    uint32_t recover_blocks;  /* consecutive quiet blocks to climb a level   */
    uint32_t hold_blocks;     /* minimum blocks between level CHANGES        */
    uint8_t  backoff_max;     /* how far the recovery window may double      */
    uint8_t  max_level;       /* floor level index                           */
} gov_cfg_t;

typedef struct {
    gov_cfg_t cfg;
    uint8_t  level;        /* current quality level, 0 = full              */
    uint8_t  defer;        /* set for this block: no UPWARD change (below)  */
    uint8_t  susp;         /* gov_suspend(): reports are discarded          */
    uint16_t transitions;  /* level changes since reset (SWD telemetry)     */
    uint32_t quiet;        /* consecutive blocks under recover_cycles       */
    uint32_t near;         /* consecutive blocks in the near-miss band      */
    uint32_t since;        /* blocks since the last level change            */
    uint32_t peak;         /* worst block seen since reset                  */
    uint32_t over;         /* blocks at/over panic (i.e. missed deadlines)  */
    uint32_t artefacts;    /* blocks discarded as stall artefacts           */
    uint8_t  backoff;      /* recovery window doubles per drop (rule 5)     */
} governor_t;

void     gov_reset(governor_t *g, const gov_cfg_t *cfg);

/* Feed one block's measured cycle count; returns the level to use from the
 * NEXT block on. Cost: a few compares and counters — this runs once per block,
 * so it must never be more than that. */
unsigned gov_step(governor_t *g, uint32_t cycles);

/* A transport transition (declick + splice arm + window change) landed in this
 * block. Transitions are the historically click-prone class, so a level change
 * must not ride along with one: this suppresses the next UPWARD (recovery)
 * change only. Drops are safety and are never deferred. */
void     gov_defer_recovery(governor_t *g);

/* ---- the frozen cross-stream API (engine.c reads, main.c writes) ---------
 * gov_level() is read ONCE per block at the top of the engine's block
 * function; gov_report() is called from the ISR tail with the block's DWT
 * delta. Both act on one file-static governor so the engine needs no state. */
unsigned gov_level(void);
void     gov_report(uint32_t cycles);
void     gov_init(void);                 /* boot: apply the board.h config    */
void     gov_note_transition(void);      /* gov_defer_recovery on the singleton */
const governor_t *gov_state(void);       /* telemetry (SWD snapshot)          */

/* Bracket anything that stalls the whole machine — a flash erase/program, a
 * long bus-hogging init — so the ISR blocks it stretches are not read as DSP
 * load. Nestable (a counter), and resume clears the run counters so the first
 * block after the stall starts clean. gov_suspend() does NOT change the level:
 * a level already dropped for real reasons stays dropped. */
void     gov_suspend(void);
void     gov_resume(void);

#endif /* GOVERNOR_H */
