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

/* Boot-strap reads must be SELF-CONTAINED: bsp_panel_matrix_init() clears the
 * PB10/PB11 pull-ups to match the stock's electrical idle, and boot ordering
 * put that BEFORE the strap reads — the extend DIP was being read on a
 * FLOATING pin (x4 never engaged; found via disassembly + live PUPDR dump).
 * Assert the pull-up, settle, read, then restore the idle state. */
static int read_strap(GPIO_TypeDef *port, uint32_t pin)
{
    uint32_t save = port->PUPDR;
    port->PUPDR = (save & ~(3u << (pin*2))) | (1u << (pin*2));   /* pull-up */
    for (volatile int d = 0; d < 500; ++d) { }                   /* settle  */
    int level = (port->IDR >> pin) & 1u;
    port->PUPDR = save;                                          /* restore */
    return level;
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
    unsigned sw3 = (read_strap(SW_RES0_PORT, SW_RES0_PIN) == 0);   /* PD11, active-low */
    unsigned sw4 = (read_strap(SW_RES1_PORT, SW_RES1_PIN) == 0);   /* PD12, active-low */
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
    return read_strap(SW_EXTEND_PORT, SW_EXTEND_PIN) == 0;
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

/* HARDWARE EDGE CAPTURE on the three pulse jacks (bench session 9, clocked
 * mode): the audio ISR polls the jacks once per 16-frame block (~3 kHz), which
 * catches a 1 ms pulse every time but a ~30 us trigger — what the owner's
 * clock module puts out — only when it happens to straddle a poll. Measured:
 * 2 of ~46 pulses seen in 10 s. EXTI lines 10..12 latch every rising edge in
 * hardware; the handler just accumulates the bits and the block ISR drains
 * them with bsp_pulse_take_rises(). Priority sits BELOW the audio DMA IRQ so
 * it never preempts the audio path (the stamp is block-granular anyway). */
static volatile uint32_t g_pulse_exti_rise = 0u;   /* bit0 write bit1 recirc bit2 arm */

void bsp_pulse_exti_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    (void)RCC->APB2ENR;
    /* EXTICR[2] holds lines 8..11 (4 bits each), EXTICR[3] lines 12..15; 0x6 = port G */
    SYSCFG->EXTICR[2] = (SYSCFG->EXTICR[2] & ~((0xFu << 8) | (0xFu << 12)))
                      | (0x6u << 8) | (0x6u << 12);           /* PG10, PG11 */
    SYSCFG->EXTICR[3] = (SYSCFG->EXTICR[3] & ~0xFu) | 0x6u;   /* PG12       */
    const uint32_t m = (1u << 10) | (1u << 11) | (1u << 12);
    EXTI->RTSR |=  m;          /* rising edges only (jacks are active-high)  */
    EXTI->FTSR &= ~m;
    EXTI->PR    =  m;          /* clear anything pending from before         */
    EXTI->IMR  |=  m;
    NVIC_SetPriority(EXTI15_10_IRQn, 2);   /* audio DMA is 1: never preempt it */
    NVIC_EnableIRQ(EXTI15_10_IRQn);
}

void EXTI15_10_IRQHandler(void)
{
    uint32_t pr = EXTI->PR & ((1u << 10) | (1u << 11) | (1u << 12));
    EXTI->PR = pr;                           /* write-1-to-clear              */
    g_pulse_exti_rise |= pr >> 10;
}

unsigned bsp_pulse_take_rises(void)
{
    /* read-and-clear; the EXTI handler cannot run between the two statements
     * while we are inside the (higher-priority) audio ISR, and from the
     * superloop a lost race only delays an edge by one block */
    unsigned r = g_pulse_exti_rise;
    g_pulse_exti_rise = 0u;
    return r;
}

int bsp_sw_bandwidth_limit(void)
{
#if SW_BANDWIDTH_MAPPED
    return read_strap(SW_BANDWIDTH_PORT, SW_BANDWIDTH_PIN) == 0;
#else
    return 0;
#endif
}
