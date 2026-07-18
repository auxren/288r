/* test_fast_math.c — accuracy of the no-libm fm_sinf/fm_cosf/fm_exp2f vs libm.
 * These back the firmware's freestanding pitch path (pitch_shift's sinf + the 1.2
 * V/oct CV map's 2^x). Built by `make test` (links -lm for the reference).
 */
#include "fast_math.h"
#include <stdio.h>
#include <math.h>

#ifndef M_PI            /* POSIX, not C11: glibc hides it under -std=c11 (CI) */
#define M_PI 3.14159265358979323846
#endif

static int fails = 0;
static void ck(const char *name, int cond, double err) {
    printf("  %-34s %s  (max err %.2e)\n", name, cond ? "ok" : "FAIL", err);
    if (!cond) fails++;
}

int main(void)
{
    /* sinf over several periods */
    double smax = 0.0;
    for (double x = -4.0 * M_PI; x <= 4.0 * M_PI; x += 0.001) {
        double e = fabs((double)fm_sinf((float)x) - sin(x));
        if (e > smax) smax = e;
    }
    ck("fm_sinf vs sin, |x|<=4pi", smax < 1e-4, smax);

    /* cosf */
    double cmax = 0.0;
    for (double x = -4.0 * M_PI; x <= 4.0 * M_PI; x += 0.001) {
        double e = fabs((double)fm_cosf((float)x) - cos(x));
        if (e > cmax) cmax = e;
    }
    ck("fm_cosf vs cos, |x|<=4pi", cmax < 1e-4, cmax);

    /* exp2f over the musical pitch range (+/- ~26 semitones and beyond) */
    double emax = 0.0;
    for (double x = -6.0; x <= 6.0; x += 0.005) {
        double ref = exp2(x);
        double e = fabs((double)fm_exp2f((float)x) - ref) / ref;   /* relative */
        if (e > emax) emax = e;
    }
    ck("fm_exp2f vs exp2, |x|<=6 (rel)", emax < 1e-4, emax);

    /* exact musical anchors: octaves are powers of two */
    ck("fm_exp2f(0) == 1",   fm_fabsf(fm_exp2f(0.0f)  - 1.0f) < 1e-5f, fabs(fm_exp2f(0.0f) - 1.0));
    ck("fm_exp2f(1) == 2",   fm_fabsf(fm_exp2f(1.0f)  - 2.0f) < 1e-4f, fabs(fm_exp2f(1.0f) - 2.0));
    ck("fm_exp2f(-1) == 0.5", fm_fabsf(fm_exp2f(-1.0f) - 0.5f) < 1e-4f, fabs(fm_exp2f(-1.0f) - 0.5));
    ck("fm_exp2f(2) == 4",   fm_fabsf(fm_exp2f(2.0f)  - 4.0f) < 1e-4f, fabs(fm_exp2f(2.0f) - 4.0));

    /* fabsf */
    ck("fm_fabsf basic", fm_fabsf(-3.5f) == 3.5f && fm_fabsf(2.0f) == 2.0f, 0.0);

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
