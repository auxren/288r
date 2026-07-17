/* calibration.h — persisted calibration record on top of storage.{c,h}.
 *
 * Concrete use of the persistence layer: the power-up cal routine measures the
 * usable ADC span of the Time-CV and the multiplier knob and stores it; at runtime
 * cal_map01() stretches that span to the full [0,1] range. That is the fix for the
 * reported Time-CV narrow-usable-range bug (re/notes/hardware.md) — the full CV/knob
 * travel maps to the full multiplier range instead of a slice of it.
 *
 * The record is versioned + CRC'd via storage; the flash backend (F429 internal-
 * flash EEPROM emulation) is a separate [BENCH] hardware layer that just reads/writes
 * the blob. Bump CAL_VERSION to invalidate old formats.
 */
#ifndef CALIBRATION_H
#define CALIBRATION_H

#include <stdint.h>
#include <stddef.h>

#define CAL_VERSION 1

typedef struct {
    uint16_t time_cv_lo, time_cv_hi;   /* Time-CV usable ADC range   */
    uint16_t knob_lo,    knob_hi;      /* multiplier knob ADC range  */
} calibration_t;

_Static_assert(sizeof(calibration_t) == 8, "cal record layout is frozen (no padding)");

void   cal_defaults(calibration_t *c);                 /* full-scale, identity ranges */
size_t cal_save(const calibration_t *c, uint8_t *blob, size_t cap);  /* -> bytes to flash, 0 if too small */
int    cal_load(calibration_t *c, const uint8_t *blob, size_t len);  /* 1 = loaded, 0 = fell back to defaults */

/* Map a raw ADC reading through a [lo,hi] span to [0,1], clamped. hi<=lo -> 0. */
float  cal_map01(uint16_t lo, uint16_t hi, uint16_t raw);

#endif /* CALIBRATION_H */
