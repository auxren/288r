/* clock_mode.c — see clock_mode.h. Pure state machine, no hardware. */
#include "clock_mode.h"

void cm_init(clockmode_t *cm)
{
    cm->pairs = 0u;
    cm->engaged = 0u;
    cm->was_paired = 0u;
}

int cm_update(clockmode_t *cm, int wr_edge, int rc_edge, int clock_lost)
{
    const int paired = (wr_edge && rc_edge);

    if (cm->engaged) {
        /* Leave the moment the clock goes away. A module stuck in clocked mode
         * with no clock has a dead delay control, which reads as a fault. */
        if (clock_lost) { cm->engaged = 0u; cm->pairs = 0u; }
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
    return cm->engaged ? 1 : 0;
}
