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

/* Coincident-edge tolerance is implicit: both edges must be latched by the same
 * tick. At the panel tick rate (~5 ms) that is far wider than any skew between
 * two jacks fed from one clock, and far narrower than a human pressing two
 * momentaries "together". */

typedef struct {
    uint8_t pairs;      /* consecutive coincident write+recirc edges          */
    uint8_t engaged;    /* clocked mode active                                */
    uint8_t was_paired; /* last tick was a pair (for run detection)           */
} clockmode_t;

void cm_init(clockmode_t *cm);

/* One call per panel tick.
 *   wr_edge, rc_edge : latched pulse edges consumed this tick
 *   clock_lost       : the clock-follow dropout fired (see cf_tick)
 * Returns 1 while clocked mode is engaged.
 *
 * NOTE the asymmetry: entry needs a RUN of coincidences, exit happens the
 * moment the clock stops. Getting in should be deliberate; getting out must be
 * immediate, because a module stuck in clocked mode with no clock is a module
 * whose delay knob does nothing. */
int  cm_update(clockmode_t *cm, int wr_edge, int rc_edge, int clock_lost);

/* True when the transport should IGNORE these pulses because they are the
 * clock rather than transport commands. Once engaged, write/recirc edges are
 * clock ticks; passing them to the looper as well would retrigger capture on
 * every clock. */
int  cm_swallows_transport(const clockmode_t *cm);

#endif /* CLOCK_MODE_H */
