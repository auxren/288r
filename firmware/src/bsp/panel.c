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
    a_set(P165_CLK, 0);   /* idle LOW between reads — matches stock (live ODR) */
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

/* ---- stock-faithful boot GPIO state + DIP/preset matrix scan ---------------
 * From the stock init (sub_2508 + sub_fe0/sub_102c pin table): the 4051 mux
 * address lines are PA0/PA1/PA7/PA8/PA11 and boot at 0,1,x,1,1,0 — i.e. PA1,
 * PA7, PA8 HIGH, PA0, PA11 LOW. PB0/PB1 and PC10/PC12 are driven low. The old
 * codec bring-up parked ALL of these high (a state the stock never uses). */
void bsp_panel_mux_boot_state(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN;
    (void)RCC->AHB1ENR;
    /* RUNTIME truth from the live stock dump (GPIOA ODR = 0x996): PA1,2,4,7,8,11
     * HIGH. PA11 is set at runtime by the transport init (sub_102c(4)) — the
     * .data boot constants alone said low, which was wrong on the wire. */
    static const uint8_t pa_lo[] = {0};
    static const uint8_t pa_hi[] = {1, 7, 8, 11};
    for (unsigned i = 0; i < sizeof pa_lo; ++i) { a_out(pa_lo[i]); a_set(pa_lo[i], 0); }
    for (unsigned i = 0; i < sizeof pa_hi; ++i) { a_out(pa_hi[i]); a_set(pa_hi[i], 1); }
    /* PB0/PB1 low (stock clears them at init, never sets them) */
    GPIOB->MODER = (GPIOB->MODER & ~(3u << 0) & ~(3u << 2)) | (1u << 0) | (1u << 2);
    GPIOB->BSRR  = (1u << 16) | (1u << 17);
}

/* DSP-driven indicator/pulse outputs — PA0/1/7/8/11 (stock sub_fe0/sub_102c
 * index 0..4). NOT mux addresses: the stock drives them from signal/transport
 * state (PA0 = input>0.5FS comparator, PA11 = envelope presence, PA1/7/8 =
 * transport mode + loop-wrap pulses). Active-LOW at the LEDs. */
static const uint8_t IND_PIN[5] = { 0, 1, 7, 8, 11 };

void bsp_panel_ind(unsigned idx, int level)
{
    if (idx < 5u) a_set(IND_PIN[idx], level);
}

void bsp_panel_strobe(int level)   /* legacy name: indicator 0 (input LED) */
{
    a_set(0, level);
}

/* Match every remaining ELECTRICAL idle-state difference against the live stock
 * GPIO dump (machine diff, bench session 5). The panel's level LEDs are analog;
 * any node we load differently (pull-up leakage, a parked chip-select, a floating
 * driver) shows up as stuck LEDs. Differences fixed here:
 *   - drop our internal pull-ups on PB10/11, PC1..6, PD11/12 (stock: none — the
 *     board has external resistors), and the stray PA15 pull
 *   - PA9/PA10 -> USART1 AF7, USART1 enabled 115200 8N1 exactly like stock (a
 *     silent debug port, but TX then idles DRIVEN HIGH instead of floating)
 * Call after all other panel/codec init. */
void bsp_panel_match_stock_idle(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN |
                    RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_GPIODEN;
    (void)RCC->AHB1ENR;

    GPIOB->PUPDR &= ~((3u << (10*2)) | (3u << (11*2)));                  /* PB10/11 */
    GPIOC->PUPDR &= ~((3u << (1*2)) | (3u << (2*2)) | (3u << (3*2)) |
                      (3u << (4*2)) | (3u << (5*2)) | (3u << (6*2)));    /* PC1..6  */
    GPIOD->PUPDR &= ~((3u << (11*2)) | (3u << (12*2)));                  /* PD11/12 */
    GPIOA->PUPDR &= ~(3u << (15*2));                                     /* PA15    */

    /* USART1 on PA9/PA10, stock config (BRR=0x2D9 ~= 115200 @84 MHz, CR1=0x200C
     * = UE|TE|RE). We never send anything; this just drives the pins like stock. */
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;
    GPIOA->MODER = (GPIOA->MODER & ~((3u << (9*2)) | (3u << (10*2))))
                 | (2u << (9*2)) | (2u << (10*2));                       /* AF mode */
    GPIOA->AFR[1] = (GPIOA->AFR[1] & ~((0xFu << 4) | (0xFu << 8)))
                  | (7u << 4) | (7u << 8);                               /* AF7     */
    USART1->BRR = 0x2D9u;
    USART1->CR1 = 0x200Cu;
}

/* PC1..PC6 = the 6 matrix row inputs (pull-up, active-low), read per column. */
void bsp_panel_matrix_init(void)
{
    bsp_panel_init();                       /* 165 in + 595 out chains */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
    (void)RCC->AHB1ENR;
    for (unsigned n = 1; n <= 6; ++n) {
        GPIOC->MODER &= ~(3u << (n*2));                                  /* input   */
        GPIOC->PUPDR  = (GPIOC->PUPDR & ~(3u << (n*2))) | (1u << (n*2)); /* pull-up */
    }
}

/* One full 8-column sweep, exactly like the stock (sub_3488 outer loop):
 * per column shift col*0x111111 (six 3-bit address nibbles), latch, then read
 * rows PC1..PC6 ACTIVE-LOW into three 16-bit words with the stock's layout:
 *   w[0] (stock 0x200020c4): PC5 -> bit col, PC6 -> bit col+8
 *   w[1] (stock 0x200020c6): PC3 -> bit col, PC4 -> bit col+8
 *   w[2] (stock 0x200020c8): PC1 -> bit col, PC2 -> bit col+8   */
void bsp_panel_matrix_scan(uint16_t w[3], uint16_t trim[8])
{
    uint16_t acc[3] = {0, 0, 0};
    for (uint32_t col = 0; col < 8u; ++col) {
        bsp_panel_out(col * 0x111111u);
        dly(); dly();
        /* analog scan: one ADC3/PF8 conversion per column (stock: continuous
         * ch6 conversions while the 595 sweeps the mux addresses) */
        if (trim) trim[col] = bsp_mult_read();
        uint32_t idr = GPIOC->IDR;
        if (!(idr & (1u << 5))) acc[0] |= (uint16_t)(1u << col);
        if (!(idr & (1u << 6))) acc[0] |= (uint16_t)(1u << (col + 8));
        if (!(idr & (1u << 3))) acc[1] |= (uint16_t)(1u << col);
        if (!(idr & (1u << 4))) acc[1] |= (uint16_t)(1u << (col + 8));
        if (!(idr & (1u << 1))) acc[2] |= (uint16_t)(1u << col);
        if (!(idr & (1u << 2))) acc[2] |= (uint16_t)(1u << (col + 8));
    }
    w[0] = acc[0]; w[1] = acc[1]; w[2] = acc[2];
}
