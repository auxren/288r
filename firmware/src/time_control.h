/* time_control.h — TIME MULTIPLIER control path (faithful clone, no PLL, no zipper).
 *
 * Stock firmware (sub_2030) read the TIME control in software double precision, then
 * committed the value only past a hysteresis threshold and retuned the SAI PLL in
 * octave steps -> discrete, un-modulatable. Here: single-precision, continuous, one-pole
 * slewed. The output is a unitless multiplier applied to the tap delays (see taps.h),
 * so a slow LFO/CV on the control sweeps the delay smoothly (chorus/flanger).
 *
 * Range recovered from the binary: multiplier spans roughly x0.25 .. x20 depending on
 * cal/cycle switches (q4 seeds 1.0/2.0/20.0, octave /1,/2,/4). Exact mapping curve is a
 * calibration item for the bench session; a log/exponential map is used by default as a
 * musically-even default and is trivial to swap.
 */
#ifndef TIME_CONTROL_H
#define TIME_CONTROL_H

typedef struct {
    float mult;    /* current smoothed multiplier                    */
    float slew;    /* one-pole coeff per update, (0,1]               */
    float lo, hi;  /* multiplier range clamp (e.g. 0.25 .. 20)       */
} time_ctrl_t;

/* initial: starting multiplier; slew in (0,1]; lo<hi range. */
void  tc_init(time_ctrl_t *tc, float initial, float slew, float lo, float hi);

/* Set the range (e.g. when cal/cycle switches change the coarse span). */
void  tc_set_range(time_ctrl_t *tc, float lo, float hi);

/* One update: raw in [0,1] (ADC/CV pot+CV, summed & normalized) -> mapped to
 * [lo,hi] exponentially, then one-pole slewed. Returns the smoothed multiplier. */
float tc_update(time_ctrl_t *tc, float raw01);

#endif /* TIME_CONTROL_H */
