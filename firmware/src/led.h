/* led.h — 288r front-panel LEDs over the 74HC595 chain (bsp_panel_out, PC12/PC10/PA15).
 *
 * The single 24-bit 595 word drives the LEDs AND the DIP-matrix column-select lines,
 * and *possibly* the 4051 mux-enable and the codec reset (re/notes/panel-scan.md §2/§4).
 * So the bit->LED map is [BENCH]: it must be labelled with a walking-1 sweep before we
 * dare drive the chain live (a wrong bit could deassert codec reset and kill audio).
 *
 * This module keeps a logical LED state and composes the 24-bit word from a bit-map
 * table filled at the bench. led_diag_walk() is the discovery tool. Nothing here
 * touches hardware; main shifts led_word() out via bsp_panel_out (gated until mapped).
 */
#ifndef LED_H
#define LED_H

#include <stdint.h>

/* [BENCH] actual count/positions unknown; notes estimate ~5. 8 is a safe upper API
 * bound (fits in the 24-bit word alongside column-select). */
#define LED_COUNT 8

typedef struct {
    uint32_t on;    /* logical LED state, bit i = LED i (0..LED_COUNT-1) */
} led_state_t;

void     led_clear(led_state_t *s);
void     led_set(led_state_t *s, unsigned i, int on);
int      led_get(const led_state_t *s, unsigned i);

/* Compose the 24-bit 595 word: LED bits (via the [BENCH] map) OR'd with `extra`
 * (the scan's current column-select one-hot, or any bits that must stay asserted
 * such as a mux-enable / codec-reset-release; 0 if unused). Masked to 24 bits. */
uint32_t led_word(const led_state_t *s, uint32_t extra);

/* Diagnostic: a walking single '1' across the 24 output bits (step advances each
 * call). Shift this out and watch the panel to label which bits are LEDs (and,
 * crucially, which bits must NOT be disturbed). */
uint32_t led_diag_walk(unsigned step);

#endif /* LED_H */
