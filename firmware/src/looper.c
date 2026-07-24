/* looper.c — see looper.h. Extracted 1:1 from main.c's panel-tick transport
 * block (v1.2.1-rc5 semantics); behavior changes belong here WITH a case in
 * test/test_looper.c. */
#include "looper.h"

void looper_init(looper_t *lp, const looper_cfg_t *cfg)
{
    lp->cfg = *cfg;
    lp->state = LP_READY;
    lp->armed = 0;
    lp->take_auto = 0;
    lp->prev_auto = 0xFFu;
    lp->prev_store = 0xFFu;
    lp->sil_ticks = 0;
    lp->start = 0;
    lp->end = 0;
    lp->led_write = 0;
    lp->led_recirc = 0;
    lp->led_ready = 0;
}

static void begin_take(looper_t *lp, engine_t *e, uint8_t take_auto)
{
    lp->start = e->dl.wpos;
    engine_write(e);
    lp->state = LP_WRITE;
    lp->armed = 0;
    lp->take_auto = take_auto;
}

void looper_tick(looper_t *lp, engine_t *e,
                 unsigned automode, unsigned store_end,
                 int wr_edge, int rc_edge, int arm_in, float sens)
{
    /* Physical switch MOVEMENT resets the state machine (#13/#16): all-sounds
     * entry releases any loop back to continuous write; looper entry sits
     * READY *armed* so a present signal captures immediately. The boot latch
     * (0xFF) takes no action, and recalled presets can't fake a movement —
     * these are the physical positions only. */
    if ((uint8_t)automode != lp->prev_auto) {
        if (lp->prev_auto != 0xFFu) {
            lp->state = LP_READY;
            lp->armed = (automode != 1u);
        }
        lp->prev_auto = (uint8_t)automode;
    }
    if ((uint8_t)(store_end != 0) != lp->prev_store) {
        if (lp->prev_store != 0xFFu) {
            lp->state = LP_READY;
            lp->armed = (automode != 1u);
        }
        lp->prev_store = (uint8_t)(store_end != 0);
    }

    if (automode == 1u) {
        /* DELAY mode ("all sounds"): continuous write; manual punches honored */
        if (rc_edge) {
            engine_recirc_window(e, (uint32_t)e->taps.base_delay);
        } else if (wr_edge || (lp->state != LP_LOOP &&
                               !transport_should_write(&e->xport))) {
            engine_write(e);
        }
        if (transport_should_write(&e->xport)) {
            lp->state = LP_READY;
            lp->led_write = 1; lp->led_recirc = 0; lp->led_ready = 0;
        } else {
            lp->state = LP_LOOP;
            lp->led_write = 0; lp->led_recirc = 1; lp->led_ready = -1;
        }
        return;
    }

    /* LOOPER/auto mode (center or next-sound). Shared trigger law, "next
     * sound" semantics: arm when the input dips below the arm threshold, fire
     * on the next onset — a signal present when a state is entered can't
     * trigger until it stops and restarts. Used by READY (first capture) AND
     * by LOOP/HOLD (auto re-arm, #10). The arm-jack pulse fires regardless.
     * sens knob = threshold by analog gain; knob at zero = auto-trigger off. */
    uint32_t cyc = (uint32_t)e->taps.base_delay;
    int lp_silent = (sens < lp->cfg.sens_ref * lp->cfg.arm_frac);
    int lp_auto_fire = (lp->armed && sens > lp->cfg.sens_ref) || arm_in;
    if (lp_silent) {
        lp->armed = 1;
        if (lp->sil_ticks < 0xFFFFu) lp->sil_ticks++;
    } else {
        lp->sil_ticks = 0;
    }

    switch (lp->state) {
    case LP_READY:
        if (!transport_should_write(&e->xport)) engine_write(e);
        if (lp_auto_fire || wr_edge || automode == 2u) {
            begin_take(lp, e, (uint8_t)(!wr_edge && automode != 2u));
        }
        lp->led_write = 0; lp->led_recirc = 0; lp->led_ready = 1;
        break;

    case LP_WRITE: {
        uint32_t written = (e->dl.wpos >= lp->start)
                         ? e->dl.wpos - lp->start
                         : e->dl.wpos + lp->cfg.delay_len - lp->start;
        if (rc_edge) {                             /* manual punch-out         */
            engine_recirc_between(e, lp->start);
            lp->state = LP_LOOP;
        } else if (store_end && lp->take_auto &&
                   lp->sil_ticks >= lp->cfg.release_ticks &&
                   written > lp->cfg.release_samp + lp->cfg.min_loop_samp) {
            /* store end + auto take (#10, field-designed): the signal ended,
             * so the take is the phrase — loop it, minus the release hang.
             * "Signal present = writing, silence = looping"; store beg.
             * quantizes the length to the cycle instead. */
            uint32_t end = (e->dl.wpos + lp->cfg.delay_len
                            - lp->cfg.release_samp) % lp->cfg.delay_len;
            engine_recirc_span(e, lp->start, end);
            lp->state = LP_LOOP;
        } else if (written >= cyc) {
            if (!store_end || lp->take_auto) {
                /* store beg. (and capped auto store-end takes): loop the
                 * cycle. Auto store-end used to go to silent HOLD here, where
                 * the re-arm immediately punched a new take — audible result:
                 * write forever, never a loop (field report #10). */
                engine_recirc_window(e, cyc);
                lp->state = LP_LOOP;
            } else {                               /* manual store end: hold   */
                lp->end = e->dl.wpos;
                lp->state = LP_HOLD;               /* delay keeps running      */
            }
        } else if (wr_edge) {                      /* restart the take         */
            lp->start = e->dl.wpos;
            engine_write(e);
            lp->take_auto = 0;
        }
        lp->led_write = 1; lp->led_recirc = 0; lp->led_ready = 0;
        break; }

    case LP_HOLD:  /* stock mode 5: window stored, delay keeps running */
        if (!transport_should_write(&e->xport)) engine_write(e);
        if (rc_edge) {                             /* recall the saved window  */
            engine_recirc_span(e, lp->start, lp->end);
            lp->state = LP_LOOP;
        } else if (wr_edge || lp_auto_fire) {      /* new take                 */
            begin_take(lp, e, (uint8_t)!wr_edge);
        }
        /* stored-and-waiting: write + ready together */
        lp->led_write = 1; lp->led_recirc = 0; lp->led_ready = 1;
        break;

    default: /* LP_LOOP */
        /* AUTO RE-ARM (#10/#16): stock evidence — in the batchas 288v video
         * auto control cycles write/recirc continuously with the input, so a
         * playing loop re-triggers on the next onset, not holds forever. */
        if (wr_edge || lp_auto_fire) {             /* punch a new take         */
            begin_take(lp, e, (uint8_t)!wr_edge);
        }
        lp->led_write = 0; lp->led_recirc = 1; lp->led_ready = -1;
        break;
    }
}
