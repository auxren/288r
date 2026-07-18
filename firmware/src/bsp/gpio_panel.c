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
#if SW_EXTEND_MAPPED
    in_pullup(SW_EXTEND_PORT,    SW_EXTEND_PIN);
#endif
#if SW_BANDWIDTH_MAPPED
    in_pullup(SW_BANDWIDTH_PORT, SW_BANDWIDTH_PIN);
#endif
}

static int rd(GPIO_TypeDef *port, uint32_t n) { return (port->IDR >> n) & 1u; }

/* With pull-ups, a closed-to-ground switch reads 0 = asserted. [BENCH] confirm. */
int bsp_sw_calibrate(void)  { return rd(SW_CALPRESET_PORT, SW_CALPRESET_PIN) == 0; }
int bsp_sw_full_cycle(void) { return rd(SW_CYCLE_PORT,     SW_CYCLE_PIN)     == 0; }

unsigned bsp_resolution_bits(void)
{
    /* Config DIP SW1 sw3=PD11, sw4=PD12, ACTIVE-LOW (off = high via pull-up).
     * Owner-confirmed table: both off = 24-bit, sw3 = 12, sw4 = 8, both on = 4-bit.
     * [BENCH] confirm sw3/sw4 vs PD11/PD12 order (12 vs 8 could be swapped). */
    unsigned sw3 = (rd(SW_RES0_PORT, SW_RES0_PIN) == 0);   /* PD11, active-low */
    unsigned sw4 = (rd(SW_RES1_PORT, SW_RES1_PIN) == 0);   /* PD12, active-low */
    switch (sw3 | (sw4 << 1)) {
        case 0:  return 24u;   /* both off */
        case 1:  return 12u;   /* sw3 on   */
        case 2:  return 8u;    /* sw4 on   */
        default: return 4u;    /* both on  */
    }
}

/* Config DIP sw1 (x10 delay/looper extend) and sw2 (11025 Hz bandwidth limit).
 * Pins are [BENCH]-pending: until SW_*_MAPPED is set in board.h these return 0
 * (feature off) rather than sampling an unrelated pin. Active-low when mapped. */
int bsp_sw_delay_extend(void)
{
#if SW_EXTEND_MAPPED
    return rd(SW_EXTEND_PORT, SW_EXTEND_PIN) == 0;
#else
    return 0;
#endif
}

/* Pulse-input jacks (stock sub_3ccc, decompile-verified): PG10 = WRITE pulse in,
 * PG11 = RECIRC pulse in, PG12 = arm/"next sound" pulse in. ACTIVE-HIGH (the
 * stock acts when the jack reads 1). GPIOG clock is enabled by sdram_init. */
int bsp_pulse_in(unsigned which)   /* 0=write 1=recirc 2=arm */
{
    static const uint8_t pin[3] = { 10u, 11u, 12u };
    if (which > 2u) return 0;
    return (GPIOG->IDR >> pin[which]) & 1u;
}

int bsp_sw_bandwidth_limit(void)
{
#if SW_BANDWIDTH_MAPPED
    return rd(SW_BANDWIDTH_PORT, SW_BANDWIDTH_PIN) == 0;
#else
    return 0;
#endif
}
