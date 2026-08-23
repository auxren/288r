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
