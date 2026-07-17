/* panel.c — bit-banged panel I/O, decoded from the stock firmware (bench session 3).
 *
 * The 288r panel is NOT on SPI1 (SPI1 is disabled in stock). It's two bit-banged
 * shift-register chains on GPIO, exactly reproduced here:
 *
 *   INPUT  74HC165  (stock sub_4290 = read_panel_switches):
 *     PA4 = LATCH/LOAD (0x10), PA5 = CLOCK (0x20), PA6 = DATA-IN (0x40)
 *     -> 13 bits MSB-first into panel_switch_bits (A/B/C select, SHORT/FULL,
 *        bank_B, octave/rate, phase-invert/mute DIP bits, ...).
 *
 *   OUTPUT 74HC595  (stock sub_3408, 24 bits/column):
 *     PC12 = DATA (0x1000), PC10 = CLOCK (0x400), PA15 = LATCH (0x8000)
 *     -> drives the LEDs + DIP column-select lines.
 *
 * Pin ROLES are confirmed from the disassembly. The bit->LED and bit->switch
 * MAPPINGS are [BENCH] (need a live sweep on the unit to label each bit). The
 * separate 4051 analog mux (trimmers/sliders -> ADC3/PF8, address on GPIOA
 * {PA0,1,7,8,11}) is a third path, still being resolved.
 */
#include "stm32f429xx.h"
#include "bsp.h"

/* pin numbers */
#define P165_LATCH 4u    /* PA4  */
#define P165_CLK   5u    /* PA5  */
#define P165_DATA  6u    /* PA6 (input) */
#define P595_LATCH 15u   /* PA15 */
#define P595_CLK   10u   /* PC10 */
#define P595_DATA  12u   /* PC12 */

static inline void a_out(unsigned n){ GPIOA->MODER = (GPIOA->MODER & ~(3u<<(n*2))) | (1u<<(n*2)); }
static inline void c_out(unsigned n){ GPIOC->MODER = (GPIOC->MODER & ~(3u<<(n*2))) | (1u<<(n*2)); }
static inline void a_set(unsigned n, int v){ GPIOA->BSRR = v ? (1u<<n) : (1u<<(n+16)); }
static inline void c_set(unsigned n, int v){ GPIOC->BSRR = v ? (1u<<n) : (1u<<(n+16)); }
static inline void dly(void){ for (volatile int d = 0; d < 12; ++d) { } }

void bsp_panel_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOCEN;
    (void)RCC->AHB1ENR;
    a_out(P165_LATCH); a_out(P165_CLK); a_out(P595_LATCH);
    GPIOA->MODER &= ~(3u << (P165_DATA*2));            /* PA6 input */
    c_out(P595_CLK); c_out(P595_DATA);
    a_set(P165_LATCH, 1); a_set(P165_CLK, 1);
    c_set(P595_CLK, 0);   a_set(P595_LATCH, 0);
}

/* Init ONLY the 74HC165 switch-input pins (PA4 latch, PA5 clk, PA6 data-in),
 * leaving the 74HC595 output pins untouched. Lets us scan the switches live
 * without any output-side risk (the 595 chain may carry codec reset / mux enable). */
void bsp_panel_switches_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    (void)RCC->AHB1ENR;
    a_out(P165_LATCH); a_out(P165_CLK);
    GPIOA->MODER &= ~(3u << (P165_DATA*2));            /* PA6 input */
    a_set(P165_LATCH, 1); a_set(P165_CLK, 1);          /* idle high */
}

/* Read the 74HC165: pulse latch, then clock in 13 bits MSB-first. */
uint16_t bsp_panel_switches_read(void)
{
    a_set(P165_LATCH, 0); dly(); a_set(P165_LATCH, 1); dly();   /* load parallel inputs */
    uint16_t r = 0;
    for (int i = 0; i < 13; ++i) {
        a_set(P165_CLK, 0); dly();
        r <<= 1;
        if ((GPIOA->IDR >> P165_DATA) & 1u) r |= 1u;
        a_set(P165_CLK, 1); dly();
    }
    return r;
}

/* Shift 24 bits MSB-first out the 74HC595 (PC12 data, PC10 clock), then latch. */
void bsp_panel_out(uint32_t bits24)
{
    for (int i = 23; i >= 0; --i) {
        c_set(P595_DATA, (bits24 >> i) & 1u); dly();
        c_set(P595_CLK, 1); dly();                 /* rising edge shifts */
        c_set(P595_CLK, 0); dly();
    }
    a_set(P595_LATCH, 1); dly(); a_set(P595_LATCH, 0);   /* latch to outputs */
}
