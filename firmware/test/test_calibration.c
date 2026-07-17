/* test_calibration.c — storage-backed calibration record + range mapping
 * (see src/calibration.c). Covers defaults, save/load round-trip, safe fallback on
 * invalid/blank/wrong-version blobs, and the CV/knob range-stretch (the narrow-range
 * bug fix). Built by `make test`.
 */
#include "calibration.h"
#include "storage.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int fails = 0;
static void ck(const char *name, int cond) {
    printf("  %-48s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

int main(void)
{
    calibration_t c, d;
    uint8_t blob[64];

    /* defaults are full-scale */
    cal_defaults(&c);
    ck("defaults: Time-CV full span", c.time_cv_lo == 0 && c.time_cv_hi == 4095);

    /* save then load round-trips the measured ranges */
    c.time_cv_lo = 300; c.time_cv_hi = 3600; c.knob_lo = 120; c.knob_hi = 3900;
    size_t nbytes = cal_save(&c, blob, sizeof blob);
    ck("save returns header+payload bytes", nbytes == STORAGE_HEADER_BYTES + sizeof(calibration_t));
    int loaded = cal_load(&d, blob, nbytes);
    ck("load reports success", loaded == 1);
    ck("cal fields round-trip", memcmp(&c, &d, sizeof c) == 0);

    /* blank/erased flash -> defaults, flagged */
    { uint8_t blank[64]; memset(blank, 0xFF, sizeof blank);
      int r = cal_load(&d, blank, sizeof blank);
      ck("blank blob -> defaults (returns 0)", r == 0 && d.time_cv_hi == 4095); }

    /* corrupt CRC -> defaults */
    { uint8_t bad[64]; memcpy(bad, blob, nbytes); bad[STORAGE_HEADER_BYTES + 1] ^= 0x20;
      int r = cal_load(&d, bad, nbytes);
      ck("corrupt blob -> defaults", r == 0 && d.time_cv_lo == 0); }

    /* a record written with a different (future) version is refused -> defaults */
    { uint8_t future[64];
      size_t fn = storage_pack(future, CAL_VERSION + 1, &c, (uint16_t)sizeof c);
      int r = cal_load(&d, future, fn);
      ck("wrong-version blob -> defaults", r == 0 && d.knob_hi == 4095); }

    /* range mapping: endpoints, midpoint, clamping */
    ck("map: raw==lo -> 0",      cal_map01(300, 3600, 300) == 0.0f);
    ck("map: raw==hi -> 1",      cal_map01(300, 3600, 3600) == 1.0f);
    ck("map: below lo clamps 0", cal_map01(300, 3600, 100) == 0.0f);
    ck("map: above hi clamps 1", cal_map01(300, 3600, 4000) == 1.0f);
    ck("map: midpoint ~0.5",     fabsf(cal_map01(300, 3600, 1950) - 0.5f) < 1e-3f);
    ck("map: degenerate hi<=lo -> 0", cal_map01(3000, 3000, 3000) == 0.0f);

    /* the bug fix: a NARROW measured span still reaches full 0..1 travel */
    float atLo  = cal_map01(1000, 3000, 1000);
    float atHi  = cal_map01(1000, 3000, 3000);
    ck("narrow span still spans full range", atLo == 0.0f && atHi == 1.0f);

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
