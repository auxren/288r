/* test_envelope.c — host unit test for the one-pole envelope follower.
 * cc -std=c11 -Wall -Wextra -I../src test_envelope.c ../src/envelope.c -o /tmp/t -lm && /tmp/t
 */
#include "envelope.h"
#include <stdio.h>
#include <math.h>

static int fails = 0;
static void ok(const char *what, int cond) {
    printf(cond ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!cond) fails++;
}

int main(void)
{
    env_follower_t e;
    env_init(&e, /*atk*/ 0.02f, /*rel*/ 0.001f);

    /* Rectifies: negative input raises the envelope just like positive. */
    env_init(&e, 0.5f, 0.5f);
    float up = env_process(&e, -1.0f);
    ok("rectifies negative input", up > 0.0f);

    /* Attack rises toward a steady level under constant drive. */
    env_init(&e, 0.02f, 0.001f);
    float v = 0.0f;
    for (int i = 0; i < 2000; i++) v = env_process(&e, 1.0f);
    ok("attack converges to level ~1", fabsf(v - 1.0f) < 0.05f);

    /* Release decays toward zero on silence, slower than attack rose. */
    int rel_steps = 0;
    while (env_value(&e) > 0.5f && rel_steps < 100000) { env_process(&e, 0.0f); rel_steps++; }
    ok("release decays on silence", env_value(&e) <= 0.5f);
    ok("release slower than attack", rel_steps > 50);   /* rel=0.001 << atk=0.02 */

    /* Envelope tracks amplitude, not instantaneous sign (smooths a sine). */
    env_init(&e, 0.05f, 0.05f);
    float mn = 1e9f, mx = -1e9f;
    for (int i = 0; i < 5000; i++) {
        float y = env_process(&e, 0.8f * sinf((float)i * 0.2f));
        if (i > 1000) { if (y < mn) mn = y; if (y > mx) mx = y; }
    }
    ok("sine envelope is positive & bounded", mn > 0.0f && mx < 0.8f);

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
