/* test_knobcurve.c — panel-legend multiplier-knob curve (see calibration.c).
 * Anchors were measured mark-by-mark on the unit (owner session 2026-07-18);
 * this gates: exact reproduction at every anchor, strict monotonicity over the
 * whole 12-bit range, end-stop clamping, the [0,1] normalization, and that the
 * curve meaningfully differs from a naive linear map (the reason it exists).
 * Built by `make test`.
 */
#include "calibration.h"
#include <stdio.h>
#include <math.h>

static int fails = 0;
static void ck(const char *name, int cond) {
    printf("  %-48s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

int main(void)
{
    printf("test_knobcurve\n");

    /* the measured anchor table, duplicated here as the acceptance contract */
    static const struct { uint16_t raw; float mult; } a[] = {
        { 0, 0.4f }, { 566, 0.6f }, { 1409, 0.8f }, { 2054, 1.0f },
        { 2635, 1.2f }, { 3329, 1.4f }, { 4094, 1.6f },
    };
    int exact = 1;
    for (unsigned i = 0; i < sizeof a / sizeof a[0]; i++)
        if (fabsf(cal_knob_panel_mult(a[i].raw) - a[i].mult) > 1e-6f) exact = 0;
    ck("every measured mark reads exactly true", exact);

    ck("noon (2054) reads 1.0",
       fabsf(cal_knob_panel_mult(2054) - 1.0f) < 1e-6f);
    ck("CCW stop clamps to 0.4", cal_knob_panel_mult(0) == 0.4f);
    ck("CW rail clamps to 1.6",  cal_knob_panel_mult(4095) == 1.6f);

    int mono = 1;
    float prev = cal_knob_panel_mult(0);
    for (uint32_t r = 1; r <= 4095; r++) {
        float m = cal_knob_panel_mult((uint16_t)r);
        if (m < prev) mono = 0;
        prev = m;
    }
    ck("strictly non-decreasing over full ADC range", mono);

    int bounded = 1;
    for (uint32_t r = 0; r <= 4095; r++) {
        float m = cal_knob_panel_mult((uint16_t)r);
        if (m < 0.4f || m > 1.6f) bounded = 0;
    }
    ck("always within panel range [0.4, 1.6]", bounded);

    ck("cal_knob01 normalizes 0.4->0", fabsf(cal_knob01(0)) < 1e-6f);
    ck("cal_knob01 normalizes 1.6->1", fabsf(cal_knob01(4094) - 1.0f) < 1e-6f);
    ck("cal_knob01 noon -> 0.5", fabsf(cal_knob01(2054) - 0.5f) < 1e-6f);

    /* the taper is NOT linear: a linear map misreads the 0.6 mark by >0.03 */
    float linear_at_566 = 0.4f + (566.0f / 4094.0f) * 1.2f;
    ck("curve corrects >0.03 error vs naive linear at 0.6",
       fabsf(linear_at_566 - cal_knob_panel_mult(566)) > 0.03f);

    printf(fails ? "FAILURES: %d\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
