/* looper.h — AUTO CONTROL transport/looper state machine (extracted from
 * main.c after the v1.2.1 rc line so it is host-testable — the rc3 store-end
 * regression, #10, lived exactly in this logic and no suite could catch it).
 *
 * One looper_tick() per panel tick (~5 ms on hardware) drives the whole
 * red-switch behavior:
 *   automode 1 ("all sounds")  — plain delay: continuous write, manual
 *                                 momentary punches honored
 *   automode 0 (center)        — the looper: READY sits armed-on-silence and
 *                                 captures on the next onset; the loop re-arms
 *                                 the same way (#10)
 *   automode 2 ("next sound")  — a flick manually triggers the capture
 * plus the two capture-end policies (#10, field-designed):
 *   store beg.  — cycle-quantized: capture runs one cycle, then loops
 *   store end   — signal-gated for auto takes: write while the signal plays,
 *                 loop the phrase (minus the release hang) when it stops;
 *                 manual takes keep the stock hold-and-recall (LP_HOLD)
 * and the physical-movement resets (#13/#16): any red-switch or store-selector
 * MOVEMENT returns to READY, armed unless entering plain delay.
 *
 * The machine calls engine_write()/engine_recirc_*() directly and reports LED
 * intents; pin polarity, the end-of-cycle blip (which needs the block-rate
 * head position), and the SWD debug snapshot stay in main.c.
 */
#ifndef LOOPER_H
#define LOOPER_H

#include "engine.h"

typedef enum { LP_READY = 0, LP_WRITE, LP_HOLD, LP_LOOP } lp_state_t;

typedef struct {
    float    sens_ref;      /* auto-trigger threshold (fires above this)        */
    float    arm_frac;      /* hysteresis: re-arm below sens_ref * arm_frac     */
    uint16_t release_ticks; /* store-end auto punch-out: silent ticks required  */
    uint32_t release_samp;  /* the same hang in samples (trimmed off the loop)  */
    uint32_t min_loop_samp; /* shortest auto-captured loop                      */
    uint32_t delay_len;     /* delay buffer length (head arithmetic)            */
    uint16_t floor_win;     /* MINIMUM-STATISTICS floor window in ticks
                               (~1.5 s): the ambient floor is the envelope's
                               MINIMUM over the recent window — what the level
                               dips to BETWEEN notes. Staccato playing never
                               lifts it (the gaps reset it); continuous loop-
                               playback bleed is absorbed within the window.
                               Replaces the one-pole baseline, which tracked
                               the player's own material and ratcheted the
                               threshold up until staccato couldn't trigger
                               (field #13: "signal re-zeroes the threshold"). */
    float    onset_ratio;   /* fire when fast > floor * this (and > sens_ref)  */
} looper_cfg_t;

typedef struct {
    looper_cfg_t cfg;
    uint8_t  state;         /* lp_state_t                                       */
    uint8_t  armed;         /* silence seen since the last trigger              */
    uint8_t  take_auto;     /* current take was signal/pulse-started            */
    uint8_t  prev_auto;     /* red-switch position last tick (0xFF = boot)      */
    uint8_t  prev_store;    /* store-selector position last tick (0xFF = boot)  */
    uint16_t sil_ticks;     /* consecutive ticks below the arm threshold        */
    float    baseline;      /* published ambient floor (min-statistics)         */
    float    min_cur, min_prev; /* two-bucket sliding-minimum state             */
    uint16_t min_tick;      /* bucket age                                        */
    uint8_t  od_prev;       /* overdub hold state last tick (release detection)  */
    uint32_t start;         /* head when the current take began                 */
    uint32_t end;           /* store-end hold: head when the window completed   */
    /* LED intents after each tick (1 = lit). led_ready is -1 when this state
     * does not own the READY lamp (loop playback: the end-of-cycle blip in
     * main.c drives it). */
    uint8_t  led_write, led_recirc;
    int8_t   led_ready;
} looper_t;

void looper_init(looper_t *lp, const looper_cfg_t *cfg);

/* One panel tick. wr_edge/rc_edge: momentary rising edges; arm_in: arm-jack
 * pulse (fires a capture regardless of the arm state); sens: the sens-channel
 * envelope this tick. */
/* sens: the sens-channel envelope (threshold by analog knob gain; pre-pot
 * pickup, but it also hears output bleed — the adaptive baseline inside
 * absorbs that). */
/* rc_lvl: the recirc momentary's LEVEL (held = 1). In the LOOP state,
 * holding recirc = OVERDUB while held (sound-on-sound; auto re-arm is
 * suspended for the duration). Other states keep their edge meanings. */
void looper_tick(looper_t *lp, engine_t *e,
                 unsigned automode, unsigned store_end,
                 int wr_edge, int rc_edge, int rc_lvl, int arm_in, float sens);

#endif /* LOOPER_H */
