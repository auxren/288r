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
