/* fast_math.c — see fast_math.h. Range-reduced minimax sinf + poly exp2f, no libm. */
#include "fast_math.h"

float fm_fabsf(float x) { return (x < 0.0f) ? -x : x; }

float fm_sinf(float x)
{
    const float PI         = 3.14159265358979f;
    const float HALF_PI    = 1.57079632679490f;
    const float TWO_PI     = 6.28318530717959f;
    const float INV_TWO_PI = 0.159154943091895f;

    /* range-reduce to [-pi, pi] by subtracting the nearest multiple of 2*pi */
    float q = x * INV_TWO_PI;
    int   n = (int)(q + (q >= 0.0f ? 0.5f : -0.5f));
    x -= (float)n * TWO_PI;

    /* fold into [-pi/2, pi/2] via sin(pi - x) = sin(x), where the Taylor series
     * below is accurate to ~5e-6 (worst case at +-pi/2) */
    if      (x >  HALF_PI) x =  PI - x;
    else if (x < -HALF_PI) x = -PI - x;

    /* odd Taylor series, 5 terms: x - x^3/6 + x^5/120 - x^7/5040 + x^9/362880 */
    float x2 = x * x;
    return x * (1.0f + x2 * (-0.16666667f + x2 * (0.00833333f
                    + x2 * (-0.00019841f + x2 * 0.0000027557f))));
}

float fm_cosf(float x)
{
    return fm_sinf(x + 1.57079632679490f);   /* cos(x) = sin(x + pi/2) */
}

float fm_exp2f(float x)
{
    /* clamp to the representable exponent range */
    if (x <= -126.0f) return 0.0f;
    if (x >=  127.0f) x = 127.0f;

    /* split x = k + f with k = floor(x), f in [0,1) */
    int   k = (int)x;
    float f = x - (float)k;
    if (f < 0.0f) { f += 1.0f; k -= 1; }

    /* 2^f = e^(f*ln2): Taylor/minimax on [0,1] (coeffs = (ln2)^n / n!) */
    float p = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f
                    + f * (0.0096181f + f * 0.0013333f))));

    /* 2^k by writing the exponent field directly (k in [-126,127]) */
    union { float fl; unsigned u; } v;
    v.u = (unsigned)((k + 127) << 23);
    return p * v.fl;
}

float fm_sqrtf(float x)
{
    if (x <= 0.0f) return 0.0f;
#if defined(__arm__) || defined(__ARM_EABI__)
    float r;
    __asm ("vsqrt.f32 %0, %1" : "=t"(r) : "t"(x));
    return r;
#else
    /* hosted: one Newton step off the bit-trick seed is plenty for fade gains,
     * but just defer to the compiler builtin (links against libm's sqrtf). */
    return __builtin_sqrtf(x);
#endif
}

/* On the bare-metal target, satisfy pitch_shift.c's standard-name calls with no
 * libm. On a hosted build these names come from libm (linked with -lm), so do NOT
 * define them here or the link would see duplicates. */
#if defined(__arm__) || defined(__ARM_EABI__)
float sinf(float x)  { return fm_sinf(x); }
float fabsf(float x) { return fm_fabsf(x); }
float cosf(float x)  { return fm_cosf(x); }
float sqrtf(float x) { return fm_sqrtf(x); }
#endif
