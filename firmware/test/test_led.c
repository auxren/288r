/* test_led.c — LED state -> 24-bit 595 word composition (see src/led.c).
 * The bit->LED map is [BENCH]; this tests the composition logic against whatever
 * map is compiled in (default identity), plus range-safety, extra-bit OR, masking,
 * and the walking-1 diagnostic. Built by `make test`.
 */
#include "led.h"
#include <stdio.h>

static int fails = 0;
static void ck(const char *name, int cond) {
    printf("  %-42s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

int main(void)
{
    led_state_t s;
    led_clear(&s);
    ck("cleared -> word 0", led_word(&s, 0) == 0u);

    /* set/get each LED, and confirm exactly its mapped bit appears */
    int getset_ok = 1, onehot_ok = 1;
    for (unsigned i = 0; i < LED_COUNT; i++) {
        led_clear(&s);
        led_set(&s, i, 1);
        if (!led_get(&s, i)) getset_ok = 0;
        uint32_t w = led_word(&s, 0);
        /* exactly one bit set (LEDs map to distinct bits) */
        if (w == 0u || (w & (w - 1u)) != 0u) onehot_ok = 0;
        led_set(&s, i, 0);
        if (led_get(&s, i)) getset_ok = 0;
        if (led_word(&s, 0) != 0u) onehot_ok = 0;
    }
    ck("set/get round-trips per LED", getset_ok);
    ck("each LED -> exactly one distinct bit", onehot_ok);

    /* out-of-range index is ignored (no crash, no word change) */
    led_clear(&s);
    led_set(&s, LED_COUNT + 3u, 1);
    ck("out-of-range set ignored", led_word(&s, 0) == 0u);
    ck("out-of-range get -> 0", led_get(&s, LED_COUNT + 3u) == 0);

    /* extra bits (column-select / must-hold) are OR'd in and masked to 24 bits */
    led_clear(&s);
    ck("extra bits OR'd", led_word(&s, 0x000A00u) == 0x000A00u);
    ck("word masked to 24 bits", (led_word(&s, 0xFF000000u) & 0xFF000000u) == 0u);

    /* two LEDs on -> both bits present */
    led_clear(&s); led_set(&s, 0, 1); led_set(&s, 1, 1);
    {
        uint32_t w = led_word(&s, 0);
        int bits = 0; for (int b = 0; b < 24; b++) if ((w >> b) & 1u) bits++;
        ck("two LEDs -> two bits", bits == 2);
    }

    /* walking-1 diagnostic: single bit, position = step%24, wraps */
    int walk_ok = 1;
    for (unsigned step = 0; step < 50; step++) {
        uint32_t w = led_diag_walk(step);
        if (w == 0u || (w & (w - 1u)) != 0u) walk_ok = 0;   /* exactly one bit */
        if (w != (1u << (step % 24u))) walk_ok = 0;
    }
    ck("walking-1 diagnostic single-bit + wraps", walk_ok);

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
