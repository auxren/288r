/* clockfollow.h — external clock follow for the delay engine.
 *
 * Ported from the MARF's clockfollow (same author, same idea), keeping the two
 * parts that carry the hard-won behaviour and dropping the two that are
 * sequencer-specific:
 *
 *   KEPT   ratio-from-knob with hysteresis  — a knob resting on a zone
 *          boundary must not flicker between ratios.
 *   KEPT   period smoothing                 — light averaging for jitter, but
 *          SNAP for a real tempo change, so following a tempo ramp does not
 *          lag behind it.
 *   DROPPED humanize (per-step timing spread) — the MARF frees its time
 *          sliders in clocked mode; ours are analog and the firmware never
 *          reads them, so there is no control to host it.
 *   DROPPED step widths — an AFG needs a step duration; a delay needs a
 *          window length, which is cf_cycle_samples() here.
 *
 * ONE DELIBERATE IMPROVEMENT OVER THE MARF. It anchors its ratio zones to raw
 * ADC counts, with a different table per board revision, because it has no
 * calibration curve. We do: cal_knob_panel_mult() already maps the multiplier
 * knob onto the printed 0.4..1.6 panel legend from a 7-point owner measurement.
 * Zones here are therefore expressed in LEGEND units, which makes them
 * hardware-independent and puts x1 exactly on the printed "1" by construction
 * rather than by a measured constant that could drift.
 *
 * Ratio encoding: -8..-2 = divide, +1 = unity, +2..+8 = multiply.
 * All periods are in SAMPLES (the MARF's were in 32 kHz AFG ticks).
 *
 * PASS THE TRUE FRAME RATE TO cf_init(), NOT SAMPLE_RATE_HZ.
 *
 * The module runs at ~47,984 Hz, not the 96,000 that board.h's SAMPLE_RATE_HZ
 * claims. Measured three independent ways on the unit (2026-08-23): the SAI in
 * master mode with MCKDIV=1 and NODIV=0 gives MCLK 12.286 MHz => Fs 47,990;
 * the block rate counted against wall-clock is 2999/s; and the ratio of
 * dl.wpos to g_blocks is exactly 16.00 samples per block, which needs no
 * register decoding at all. 2999 x 16 = 47,984.
 *
 * That constant is deliberately NOT being corrected -- every ear-calibrated
 * value in the firmware was tuned against current behaviour, and the owner is
 * happy with how it sounds. But it means SAMPLE_RATE_HZ is a scaling constant,
 * not a measurement, and everything here is genuinely time-based: pass it
 * 96000 and the qualification window silently becomes 40 ms..4 s instead of
 * 20 ms..2 s, and a clock would take four seconds to drop out instead of two.
 * Use CF_TRUE_FS_HZ. */
#ifndef CLOCKFOLLOW_H
#define CLOCKFOLLOW_H

#define CF_TRUE_FS_HZ  47984.0f

#include <stdint.h>

#define CF_RATIO_MIN   (-8)
#define CF_RATIO_MAX   ( 8)

/* Qualification: pulses spaced outside these bounds never lock. Same intent as
 * the MARF's 20 ms .. 2 s, re-expressed in samples at the engine rate. */
#define CF_MIN_PERIOD_SEC   0.020f    /*  50 Hz ceiling */
#define CF_MAX_PERIOD_SEC   2.000f    /* 0.5 Hz floor   */
#define CF_TIMEOUT_SEC      2.000f    /* silence this long -> free-run again */

/* Legend-space zone geometry (multiplier legend runs 0.4 .. 1.6, noon = 1.0). */
#define CF_LEGEND_LO   0.4f
#define CF_LEGEND_HI   1.6f
#define CF_X1_HALF     0.04f   /* half-width of the unity band around 1.0     */
#define CF_HYST        0.010f  /* zone stickiness, legend units               */

typedef struct {
    uint32_t period;      /* smoothed clock period, samples. 0 = never locked */
    uint32_t since;       /* samples since the last qualified pulse           */
    uint32_t min_period;  /* derived from the engine rate at init             */
    uint32_t max_period;
    uint32_t timeout;
    uint8_t  locked;      /* 1 once two qualified pulses have been seen       */
    int8_t   ratio;       /* current integer ratio (see encoding above)       */
} clockfollow_t;

/* fs: the TRUE frame rate -- CF_TRUE_FS_HZ, not SAMPLE_RATE_HZ. Values far
 * above the measured rate are rejected and replaced with CF_TRUE_FS_HZ, so a
 * miswiring degrades to correct timing rather than to a silently doubled
 * window. */
void cf_init(clockfollow_t *cf, float fs);

/* A clock edge arrived, `dt` samples after the previous one. Returns 1 if the
 * pulse qualified (and therefore updated the period), 0 if it was rejected as
 * too fast or too slow to be a clock. */
int  cf_pulse(clockfollow_t *cf, uint32_t dt);

/* Advance the dropout timer by `elapsed` samples. Returns 1 while still
 * locked, 0 once the clock has gone away and the knob should take over. */
int  cf_tick(clockfollow_t *cf, uint32_t elapsed);

/* Integer ratio for a multiplier knob expressed in PANEL LEGEND units
 * (0.4..1.6). `current` is the ratio in force, for hysteresis; pass 0 when
 * there is none. */
int8_t cf_ratio_from_legend(float legend_mult, int8_t current);

/* The cycle length a given period and ratio ask for, in samples.
 * Multiply ratios lengthen the window, divide ratios shorten it. */
uint32_t cf_cycle_samples(uint32_t period, int8_t ratio);

/* Smoothing law, exposed for testing: light averaging for jitter, snap for a
 * genuine tempo change. */
uint32_t cf_update_period(uint32_t period, uint32_t delta);

#endif /* CLOCKFOLLOW_H */
