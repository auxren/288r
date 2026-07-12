/* test_interp_quality.c — quantify fractional-delay interpolation fidelity.
 *
 * Writes a pure sine into the delay line, reads it back at fractional delays, and
 * compares against the analytically-delayed sine. Reports RMS error for LINEAR vs
 * HERMITE and asserts Hermite is better at high frequency (its whole point).
 *
 * cc -std=c11 -Wall -Wextra -I../src test_interp_quality.c ../src/delay_line.c -o /tmp/t -lm && /tmp/t
 */
#include "delay_line.h"
#include <stdio.h>
#include <math.h>

static float buf[8192];

/* RMS error of an interpolator reading a sine of normalized freq w (rad/sample). */
static float rms_error(dl_interp_t interp, float w)
{
    delay_line_t d;
    dl_init(&d, buf, 8192);
    dl_clear(&d);

    const int N = 6000;
    for (int n = 0; n < N; n++) dl_write(&d, sinf(w * (float)n));
    /* head is at N; reading delay `dd` targets absolute index (N - dd). */

    double sq = 0.0; int cnt = 0;
    for (float dd = 100.0f; dd < 200.0f; dd += 0.13f) {
        float got   = dl_read(&d, dd, interp);
        float ideal = sinf(w * ((float)N - dd));
        double e = (double)got - (double)ideal;
        sq += e * e; cnt++;
    }
    return (float)sqrt(sq / cnt);
}

int main(void)
{
    int fails = 0;
    const float PI = 3.14159265f;
    /* low, mid, high fractions of Nyquist */
    float freqs[] = { 0.05f * PI, 0.25f * PI, 0.5f * PI };
    const char *names[] = { "0.05*Nyq", "0.25*Nyq", "0.50*Nyq" };

    printf("  %-10s  %-12s  %-12s\n", "freq", "linear RMS", "hermite RMS");
    for (int i = 0; i < 3; i++) {
        float el = rms_error(DL_INTERP_LINEAR,  freqs[i]);
        float eh = rms_error(DL_INTERP_HERMITE, freqs[i]);
        printf("  %-10s  %-12.6f  %-12.6f\n", names[i], el, eh);
        if (i == 2) {  /* at high freq Hermite must clearly beat linear */
            if (!(eh < el * 0.5f)) { printf("  FAIL hermite not >2x better at HF\n"); fails++; }
            else                     printf("  ok   hermite >2x better at high freq\n");
        }
    }
    /* both interpolators are near-exact at low frequency */
    if (rms_error(DL_INTERP_LINEAR, 0.05f * PI) > 0.01f) { printf("  FAIL linear poor at LF\n"); fails++; }
    else printf("  ok   linear accurate at low freq\n");

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
