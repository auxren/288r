/* clock_mode.c — see clock_mode.h. Pure state machine, no hardware. */
#include "clock_mode.h"

void cm_init(clockmode_t *cm)
{
    cm->pairs = 0u;
    cm->singles = 0u;
    cm->engaged = 0u;
    cm->was_paired = 0u;
}

int cm_update(clockmode_t *cm, int wr_edge, int rc_edge, int clock_lost)
{
    const int paired = (wr_edge && rc_edge);

    if (cm->engaged) {
        /* Leave the moment the clock goes away entirely. A module stuck in
         * clocked mode with no clock has a dead delay control. */
        if (clock_lost) { cm->engaged = 0u; cm->pairs = 0u; cm->singles = 0u; }
        else if (paired) {
            cm->singles = 0u;              /* still both cables: stay engaged  */
        } else if (wr_edge || rc_edge) {
            /* An edge on ONE jack only. One of these is jitter; a run of them
             * means a cable came out, and the surviving jack must go back to
             * being a transport input promptly -- otherwise it spends the whole
             * dropout timeout swallowed and then starts retriggering the
             * transport on every clock. */
            if (cm->singles < 0xFFu) cm->singles++;
            if (cm->singles >= CM_EXIT_SINGLES) {
                cm->engaged = 0u; cm->pairs = 0u; cm->singles = 0u;
            }
        }
        cm->was_paired = (uint8_t)paired;
        return cm->engaged ? 1 : 0;
    }

    if (paired) {
        if (cm->pairs < 0xFFu) cm->pairs++;
        if (cm->pairs >= CM_ENTER_PAIRS) cm->engaged = 1u;
    } else if (wr_edge || rc_edge) {
        /* A lone edge on either jack is ordinary transport use, and it breaks
         * the run: someone is playing the module, not clocking it. Ticks with
         * NO edge at all are left alone -- a slow clock has many of them
         * between pulses, and resetting on those would make entry impossible
         * below the tick rate. */
        cm->pairs = 0u;
    }
    cm->was_paired = (uint8_t)paired;
    return cm->engaged ? 1 : 0;
}

int cm_swallows_pulse_jacks(const clockmode_t *cm)
{
    /* Swallow from the FIRST coincident pair, not from engagement.
     *
     * Engagement deliberately needs a run of pairs, but the jacks must be
     * withheld before that or patching the clock in disturbs the transport on
     * the way: the first pulses arrive as simultaneous write AND recirc, which
     * captures a loop. Observed on hardware -- the module went to RECIRC on a
     * silent window and played dry only, because everything wet was reading
     * that capture.
     *
     * One coincident pair is already proof of intent (the pair is
     * contradictory as transport and nobody patches it deliberately), so it is
     * safe to withhold immediately while still requiring the run before the
     * delay window starts following the clock. */
    return (cm->engaged || cm->pairs > 0u) ? 1 : 0;
}

/* ---- edge pairing (see clock_mode.h) ---------------------------------------- */
void cm_pair_init(cm_pair_t *p)
{
    p->t[0] = 0u; p->t[1] = 0u; p->pend = 0u;
}

unsigned cm_pair_block(cm_pair_t *p, unsigned rise, uint32_t block, uint32_t *stamp)
{
    unsigned ev = 0u;
    /* Written out per jack rather than as a loop: this runs inside the audio
     * ISR and the WCET contract (tools/wcet.py) prices every loop there. */
    /* 1. expire pending edges whose partner is now out of the window */
    if ((p->pend & 1u) && (block - p->t[0]) > CM_PAIR_WINDOW_BLOCKS) { p->pend &= (uint8_t)~1u; ev |= CM_EV_SINGLE_W; }
    if ((p->pend & 2u) && (block - p->t[1]) > CM_PAIR_WINDOW_BLOCKS) { p->pend &= (uint8_t)~2u; ev |= CM_EV_SINGLE_R; }
    /* 2. register this block's rises */
    if (rise & 1u) { p->t[0] = block; p->pend |= 1u; }
    if (rise & 2u) { p->t[1] = block; p->pend |= 2u; }
    /* 3. both pending (and, by step 1, within the window of each other) = pair */
    if ((p->pend & 0x3u) == 0x3u) {
        *stamp = (p->t[0] < p->t[1]) ? p->t[0] : p->t[1];
        p->pend = 0u;
        ev |= CM_EV_PAIR;
    }
    return ev;
}
