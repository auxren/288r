/* clock_mode.h — the entry gesture for clocked mode.
 *
 * Patch the clock into BOTH the write and recirc pulse jacks. That pair is
 * *contradictory* as transport — one starts recording, the other stops it —
 * so an edge arriving on both within the same tick cannot be anyone's intent
 * except this. The patch declares the mode: no menu, no rear DIP to forget,
 * and the arm jack stays free for loop capture. The MARF uses exactly this
 * idiom (a clock into Start and Stop together).
 *
 * Kept as a pure state machine so main.c needs only a couple of lines, and so
 * the whole gesture — including the cases that bite, like one jack unplugged
 * mid-session — is table-testable on the host.
 */
#ifndef CLOCK_MODE_H
#define CLOCK_MODE_H

#include <stdint.h>

/* Coincident pairs required before engaging. More than one, because a single
 * coincidence can happen by accident when a player hits both momentaries; a
 * clock produces them forever. */
#define CM_ENTER_PAIRS   3u

/* Unpaired pulses tolerated before handing the jacks back. Mirrors the entry
 * run for the same reason: two cables fed from one clock can have their edges
 * land either side of a panel-tick boundary, so a single unpaired tick is
 * jitter, not intent. A RUN of them means a cable came out. */
#define CM_EXIT_SINGLES  3u

/* Coincident-edge tolerance is implicit: both edges must be latched by the same
 * tick. At the panel tick rate (~5 ms) that is far wider than any skew between
 * two jacks fed from one clock, and far narrower than a human pressing two
 * momentaries "together". */

typedef struct {
    uint8_t pairs;      /* consecutive coincident write+recirc edges          */
    uint8_t singles;    /* consecutive UNPAIRED edges while engaged           */
    uint8_t engaged;    /* clocked mode active                                */
    uint8_t was_paired; /* last tick was a pair (for run detection)           */
} clockmode_t;

void cm_init(clockmode_t *cm);

/* One call per panel tick.
 *   wr_edge, rc_edge : latched pulse edges consumed this tick
 *   clock_lost       : the clock-follow dropout fired (see cf_tick)
 * Returns 1 while clocked mode is engaged.
 *
 * THREE ways out, and the middle one is the important one:
 *   1. the clock stops entirely      -> `clock_lost` (the 2 s dropout)
 *   2. ONE JACK IS UNPLUGGED         -> a run of unpaired edges
 *   3. (never) some timeout while still paired -- a valid clock keeps it.
 *
 * Case 2 exists because the obvious design gets it wrong. If the only exit is
 * the dropout timeout, pulling one cable leaves the OTHER jack still clocking:
 * no more pairs arrive, so the module sits engaged for the full timeout with
 * that jack's pulses swallowed and doing nothing, and then drops out and lets
 * the same pulse train hammer the transport several times a second. Watching
 * for unpaired edges ends it within a few clocks instead, so the jacks go back
 * to being transport inputs while the player still has their hand on the cable.
 *
 * Entry needs a RUN of coincidences (deliberate); exit needs a RUN of
 * non-coincidences (robust to a pair being split across a tick boundary). */
int  cm_update(clockmode_t *cm, int wr_edge, int rc_edge, int clock_lost);

/* True when the PULSE JACKS should be withheld from the looper, because their
 * edges are clock ticks rather than transport commands: passing them on would
 * retrigger capture on every clock.
 *
 * SWALLOWS THE JACKS ONLY -- NEVER THE PANEL. main.c combines three sources:
 *
 *     wr_act = pc.write_trig | bsp_pulse_in(0) | latched_edge
 *              ^^^^^^^^^^^^^   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 *              red momentary   the jack: swallow THESE
 *
 * so the wiring must mask the jack terms and pass pc.write_trig / pc.recirc_trig
 * straight through. Clocked mode sets the delay's WINDOW LENGTH; it does not
 * take over the transport. Manual write and recirculate, hold-to-overdub,
 * hold-write-to-save, the sens auto-capture and the arm jack all keep working
 * exactly as they do now -- loops are simply sized to the clock grid.
 *
 * Getting this wrong would kill the red momentaries whenever a clock is
 * patched, which is the kind of surprise that reads as a dead module. */
/* NOTE: true from the FIRST coincident pair, before engagement. Waiting for
 * engagement let the first pulses through as transport and captured a loop. */
int  cm_swallows_pulse_jacks(const clockmode_t *cm);

#endif /* CLOCK_MODE_H */
