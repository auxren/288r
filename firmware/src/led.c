/* led.c — see led.h. Logical LED state -> 24-bit 74HC595 word. Clean-room. */
#include "led.h"

/* [BENCH] logical LED index -> 595 output bit (0..23). UNKNOWN until a walking-1
 * sweep (led_diag_walk) labels the bits on the unit. This placeholder identity map
 * MUST NOT be trusted for real display until confirmed -- a wrong bit may drive a
 * column-select, the 4051 enable, or codec reset. */
static const uint8_t LED_BIT[LED_COUNT] = { 0, 1, 2, 3, 4, 5, 6, 7 };

#define LED_WORD_MASK 0x00FFFFFFu   /* 24 output bits */

void led_clear(led_state_t *s) { s->on = 0u; }

void led_set(led_state_t *s, unsigned i, int on)
{
    if (i >= LED_COUNT) return;
    if (on) s->on |=  (1u << i);
    else    s->on &= ~(1u << i);
}

int led_get(const led_state_t *s, unsigned i)
{
    return (i < LED_COUNT) ? (int)((s->on >> i) & 1u) : 0;
}

uint32_t led_word(const led_state_t *s, uint32_t extra)
{
    uint32_t w = extra & LED_WORD_MASK;
    for (unsigned i = 0; i < LED_COUNT; i++)
        if ((s->on >> i) & 1u) w |= (1u << LED_BIT[i]);
    return w & LED_WORD_MASK;
}

uint32_t led_diag_walk(unsigned step)
{
    return 1u << (step % 24u);
}
