/* governor.h — the ISR load governor: clause C-3c of the budget contract.
 *
 * THE ARGUMENT. `make wcet` proves a STATIC CEILING for each quality level:
 * level 0 (full quality) is allowed to sit near 1.00 of budget, but the FLOOR
 * level must be proven <= 0.60. That proof is only useful if we can always
 * REACH the floor level, and reach it fast enough that an overrun cannot
 * repeat. So: the ISR measures its own cycle count (DWT, already there for
 * telemetry) and hands it to gov_report(). Any single block at or above the
 * drop threshold costs a quality level on the spot — the NEXT block already
 * runs cheaper. Therefore no more than one consecutive block can overrun, and
 * with the static floor ceiling proven, sustained overrun is impossible.
 * The audible failure this replaces is the field's "fizzy static": blocks that
 * miss the DMA deadline tear the output buffer, every pass, forever.
 *
 * WHAT A LEVEL MEANS is the engine's business, not the governor's. The
 * governor only ranks: 0 = full quality, rising = cheaper. engine.c reads
 * gov_level() ONCE per block (never per sample — a level that changed
 * mid-block would swap interpolators inside a crossfade) and picks its tap
 * interpolation / prefetch depth from it.
 *
 * WHY NOT AVERAGE THE LOAD. Averaging is exactly wrong for a deadline: the
 * deadline is per block, and the expensive blocks are the rare ones (loop
 * wrap, splice arm, AA republish, a transport transition). The governor
 * therefore reacts to the WORST block and recovers on a long quiet run.
 *
 * Recovery is deliberately asymmetric and slow (GOV_RECOVER_BLOCKS of
 * continuously quiet blocks): the load here is program-dependent and periodic
 * (an echo pattern re-crosses the same wrap every loop pass), so a fast
 * recovery would toggle the interpolator at the loop rate — an audible
 * periodic timbre flutter, which is worse than simply staying one level down.
 *
 * Pure integer, no BSP, no float: host-tested by test/test_governor.c.
 */
#ifndef GOVERNOR_H
#define GOVERNOR_H

#include <stdint.h>

typedef struct {
    uint32_t drop_cycles;     /* one block >= this -> drop a level          */
    uint32_t panic_cycles;    /* one block >= this -> go straight to floor  */
    uint32_t recover_cycles;  /* blocks below this count toward recovery    */
    uint32_t recover_blocks;  /* consecutive quiet blocks to climb a level  */
    uint8_t  max_level;       /* floor level index                          */
} gov_cfg_t;

typedef struct {
    gov_cfg_t cfg;
    uint8_t  level;        /* current quality level, 0 = full              */
    uint8_t  defer;        /* set for this block: no UPWARD change (below)  */
    uint16_t transitions;  /* level changes since reset (SWD telemetry)     */
    uint32_t quiet;        /* consecutive blocks under recover_cycles       */
    uint32_t peak;         /* worst block seen since reset                  */
    uint32_t over;         /* blocks at/over panic (i.e. missed deadlines)  */
} governor_t;

void     gov_reset(governor_t *g, const gov_cfg_t *cfg);

/* Feed one block's measured cycle count; returns the level to use from the
 * NEXT block on. Cost: two compares and a counter — this runs once per block,
 * so it must never be more than that. */
unsigned gov_step(governor_t *g, uint32_t cycles);

/* A transport transition (declick + splice arm + window change) landed in this
 * block. Transitions are the historically click-prone class, so a level change
 * must not ride along with one un-crossfaded: this suppresses the next UPWARD
 * (recovery) change only. Drops are safety and are never deferred — dropping a
 * level on a transition block is precisely when it is needed. */
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

#endif /* GOVERNOR_H */
