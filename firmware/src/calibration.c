/* calibration.c — see calibration.h. Storage-backed cal record + range mapping. */
#include "calibration.h"
#include "storage.h"

void cal_defaults(calibration_t *c)
{
    c->time_cv_lo = 0;    c->time_cv_hi = 4095;   /* full 12-bit span until measured */
    c->knob_lo    = 0;    c->knob_hi    = 4095;
}

size_t cal_save(const calibration_t *c, uint8_t *blob, size_t cap)
{
    if (cap < STORAGE_HEADER_BYTES + sizeof(*c)) return 0;
    return storage_pack(blob, CAL_VERSION, c, (uint16_t)sizeof(*c));
}

int cal_load(calibration_t *c, const uint8_t *blob, size_t len)
{
    calibration_t tmp;
    int n = storage_load(blob, len, CAL_VERSION, &tmp, (uint16_t)sizeof(tmp));
    if (n == (int)sizeof(tmp)) { *c = tmp; return 1; }
    cal_defaults(c);            /* invalid/blank/old-version -> safe defaults */
    return 0;
}

float cal_map01(uint16_t lo, uint16_t hi, uint16_t raw)
{
    if (hi <= lo)  return 0.0f;
    if (raw <= lo) return 0.0f;
    if (raw >= hi) return 1.0f;
    return (float)(raw - lo) / (float)(hi - lo);
}

/* Panel-legend anchors, measured mark-by-mark on the unit (2026-07-18): the pot
 * taper is NOT linear against the print (a linear map misreads 0.6 as ~0.57).
 * Jitter during capture was <=2 counts; medians of 30 samples per mark. */
static const struct { uint16_t raw; float mult; } k_knob_anchor[] = {
    {    0, 0.4f },   /* CCW stop  */
    {  566, 0.6f },
    { 1409, 0.8f },
    { 2054, 1.0f },   /* noon      */
    { 2635, 1.2f },
    { 3329, 1.4f },
    { 4094, 1.6f },   /* CW stop   */
};
#define KNOB_ANCHORS (sizeof k_knob_anchor / sizeof k_knob_anchor[0])

float cal_knob_panel_mult(uint16_t raw)
{
    if (raw <= k_knob_anchor[0].raw) return k_knob_anchor[0].mult;
    for (unsigned i = 1; i < KNOB_ANCHORS; i++) {
        if (raw <= k_knob_anchor[i].raw) {
            float span = (float)(k_knob_anchor[i].raw - k_knob_anchor[i-1].raw);
            float frac = (float)(raw - k_knob_anchor[i-1].raw) / span;
            return k_knob_anchor[i-1].mult
                 + frac * (k_knob_anchor[i].mult - k_knob_anchor[i-1].mult);
        }
    }
    return k_knob_anchor[KNOB_ANCHORS - 1].mult;
}

float cal_knob01(uint16_t raw)
{
    return (cal_knob_panel_mult(raw) - 0.4f) * (1.0f / 1.2f);
}
