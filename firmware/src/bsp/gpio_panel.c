/* gpio_panel.c — read-only panel switch inputs (confirmed pins, RE bench 1).
 *
 * This is the minimal panel read for bring-up: the mode/cycle/resolution toggles
 * that gate the audio path. The full control surface (74HC595 DIP scan + 74HC4051
 * trimmer mux + SPI2 ADC for sliders/pots) is the app-layer panel_scan, [BENCH].
 * Polarity of each switch is [BENCH] — flip in one place here once measured.
 */
#include "stm32f429xx.h"
#include "board.h"
#include "bsp.h"

static void in_pullup(GPIO_TypeDef *port, uint32_t n)
{
    port->MODER &= ~(3u << (n*2));                 /* input */
    port->PUPDR  = (port->PUPDR & ~(3u << (n*2))) | (1u << (n*2)); /* pull-up */
}

void bsp_panel_gpio_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIODEN;
    (void)RCC->AHB1ENR;
    in_pullup(SW_CALPRESET_PORT, SW_CALPRESET_PIN);
    in_pullup(SW_CYCLE_PORT,     SW_CYCLE_PIN);
    in_pullup(SW_RES0_PORT,      SW_RES0_PIN);
    in_pullup(SW_RES1_PORT,      SW_RES1_PIN);
}

static int rd(GPIO_TypeDef *port, uint32_t n) { return (port->IDR >> n) & 1u; }

/* With pull-ups, a closed-to-ground switch reads 0 = asserted. [BENCH] confirm. */
int bsp_sw_calibrate(void)  { return rd(SW_CALPRESET_PORT, SW_CALPRESET_PIN) == 0; }
int bsp_sw_full_cycle(void) { return rd(SW_CYCLE_PORT,     SW_CYCLE_PIN)     == 0; }

unsigned bsp_resolution_bits(void)
{
    /* 2-bit selector -> 0:20-bit, 1:16-bit, 2:12-bit (RE). Map to bit depth. */
    unsigned code = (unsigned)(rd(SW_RES0_PORT, SW_RES0_PIN)) |
                    ((unsigned)(rd(SW_RES1_PORT, SW_RES1_PIN)) << 1);
    switch (code) {
        case 0:  return 20u;
        case 1:  return 16u;
        default: return 12u;
    }
}
