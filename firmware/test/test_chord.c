/* test_chord.c — hold-both gesture detector (see src/chord.c). Built by `make test`. */
#include "chord.h"
#include <stdio.h>

static int fails = 0;
static void ck(const char *name, int cond) {
    printf("  %-46s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

/* count fires over `n` polls with `both` held constant */
static int fires(chord_t *c, int both, int n, uint16_t hold) {
    int f = 0; for (int i = 0; i < n; i++) f += chord_update(c, both, hold);
    return f;
}

int main(void)
{
    chord_t c; chord_init(&c);
    const uint16_t HOLD = 20;

    /* not held -> never fires */
    ck("not held: no fire", fires(&c, 0, 100, HOLD) == 0);

    /* held long enough -> fires exactly once */
    chord_init(&c);
    ck("held past threshold: fires once", fires(&c, 1, 100, HOLD) == 1);

    /* fires exactly on the HOLD-th poll, not before */
    chord_init(&c);
    ck("no fire before threshold", fires(&c, 1, HOLD - 1, HOLD) == 0);
    ck("fires on the threshold poll", chord_update(&c, 1, HOLD) == 1);

    /* brief hold then release -> no fire, and re-arms */
    chord_init(&c);
    fires(&c, 1, HOLD - 1, HOLD);           /* not long enough */
    ck("released early: no fire", chord_update(&c, 0, HOLD) == 0);

    /* release then re-hold -> fires again (re-armed) */
    chord_init(&c);
    fires(&c, 1, 100, HOLD);                 /* first fire consumed */
    chord_update(&c, 0, HOLD);               /* release */
    ck("re-hold after release fires again", fires(&c, 1, 100, HOLD) == 1);

    /* a momentary tap (1 poll) never fires -> won't clash with WRITE/RECIRC taps */
    chord_init(&c);
    int tapfires = 0;
    for (int i = 0; i < 50; i++) { tapfires += chord_update(&c, 1, HOLD); chord_update(&c, 0, HOLD); }
    ck("momentary taps never fire", tapfires == 0);

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
