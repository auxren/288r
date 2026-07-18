/* fast_math.h — single-precision, no-libm math for the freestanding firmware.
 *
 * The engine links -nostdlib, so there is no libm. pitch_shift.c (verbatim) calls
 * sinf/fabsf, and the pitch CV map needs 2^x. These are the firmware's own
 * single-precision implementations (fm_*), host-tested against libm for accuracy.
 *
 * On the ARM target we also alias the standard sinf/fabsf to fm_* (see fast_math.c)
 * so pitch_shift.c links without libm. On a hosted build the system libm provides
 * sinf/fabsf, so only the fm_* names are defined here — no clash with -lm — and the
 * tests exercise fm_* directly.
 */
#ifndef FAST_MATH_H
#define FAST_MATH_H

float fm_sinf(float x);    /* sin(x), |err| < ~1e-4 over several periods */
float fm_cosf(float x);
float fm_fabsf(float x);
float fm_exp2f(float x);   /* 2^x, single precision, rel err < ~1e-4 for |x|<=32 */
float fm_sqrtf(float x);   /* sqrt(x), x>=0 (VSQRT on the target); 0 for x<0 */

#endif /* FAST_MATH_H */
