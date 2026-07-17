/* sdram.c — FMC controller + IS42S16400 (8 MB, 16-bit) bring-up, bank 1.
 *
 * The IS42S16400 is the same SDRAM ST ships on the F429I-DISCO, so this init is
 * well-trodden; timings are the -7 datasheet values at SDCLK = HCLK/2 = 84 MHz
 * (docs/bench-runbook.md).  [BENCH]: confirm the FMC pin list and that the SDRAM
 * CS is wired to bank 1 (0xC0000000) not bank 2, and try CAS 2 vs 3.
 *
 * Geometry: 4M x 16 = 4 banks x 4096 rows (12 bits) x 256 cols (8 bits) x 16-bit.
 */
#include "stm32f429xx.h"
#include "board.h"
#include "bsp.h"

/* FMC SDRAM signal pins (AF12).  Standard F429 mapping; bank-1 control on port C.
 * [BENCH] verify against the schematic — data/address are fixed, but SDCLK/SDNE0/
 * SDCKE0/SDNWE have alternate pins on some boards. */
struct pin { GPIO_TypeDef *port; uint8_t pin; };

static const struct pin fmc_pins[] = {
    /* data D0..D15 */
    {GPIOD,14},{GPIOD,15},{GPIOD,0},{GPIOD,1},{GPIOE,7},{GPIOE,8},{GPIOE,9},{GPIOE,10},
    {GPIOE,11},{GPIOE,12},{GPIOE,13},{GPIOE,14},{GPIOE,15},{GPIOD,8},{GPIOD,9},{GPIOD,10},
    /* address A0..A11 */
    {GPIOF,0},{GPIOF,1},{GPIOF,2},{GPIOF,3},{GPIOF,4},{GPIOF,5},
    {GPIOF,12},{GPIOF,13},{GPIOF,14},{GPIOF,15},{GPIOG,0},{GPIOG,1},
    /* bank address BA0/BA1 */
    {GPIOG,4},{GPIOG,5},
    /* byte-lane mask NBL0/NBL1 */
    {GPIOE,0},{GPIOE,1},
    /* control: RAS PF11, CAS PG15, WE PC0, CLK PG8, and BANK-2 CKE1/NE1 on PB5/PB6
     * (confirmed from stock: the SDRAM chip-select is FMC bank 2 -> 0xD0000000). */
    {GPIOF,11},{GPIOG,15},{GPIOC,0},{GPIOG,8},{GPIOB,5},{GPIOB,6},
};

static void fmc_gpio_init(void)
{
    /* clock the GPIO ports used by the FMC pins */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_GPIODEN
                  | RCC_AHB1ENR_GPIOEEN | RCC_AHB1ENR_GPIOFEN | RCC_AHB1ENR_GPIOGEN
                  | RCC_AHB1ENR_GPIOHEN;
    (void)RCC->AHB1ENR;

    for (unsigned i = 0; i < sizeof(fmc_pins)/sizeof(fmc_pins[0]); ++i) {
        GPIO_TypeDef *p = fmc_pins[i].port;
        uint32_t n = fmc_pins[i].pin;
        p->MODER   = (p->MODER   & ~(3u << (n*2))) | (2u << (n*2));   /* alternate  */
        p->OTYPER &= ~(1u << n);                                      /* push-pull  */
        p->OSPEEDR |=  (3u << (n*2));                                 /* very high  */
        p->PUPDR   = (p->PUPDR   & ~(3u << (n*2)));                   /* no pull    */
        uint32_t af = (n < 8) ? 0u : 1u;
        p->AFR[af] = (p->AFR[af] & ~(0xFu << ((n & 7u)*4))) | (12u << ((n & 7u)*4)); /* AF12 */
    }
}

static void short_delay(volatile uint32_t n) { while (n--) { __asm volatile("nop"); } }

static void sdram_cmd(uint32_t mode, uint32_t autorefresh, uint32_t mrd)
{
    while (FMC_Bank5_6->SDSR & FMC_SDSR_BUSY) { }
    FMC_Bank5_6->SDCMR = mode
        | FMC_SDCMR_CTB2                       /* target bank 2 (0xD0000000) */
        | ((autorefresh - 1u) << 5)            /* NRFS */
        | (mrd << 9);                          /* MRD  */
}

void bsp_sdram_init(void)
{
    fmc_gpio_init();
    RCC->AHB3ENR |= RCC_AHB3ENR_FMCEN;
    (void)RCC->AHB3ENR;

    /* Bank 2 config. The shared bits (SDCLK/RBURST/RPIPE) MUST live in SDCR1 even
     * when using bank 2; the geometry (NC/NR/MWID/NB/CAS) goes in SDCR2. */
    FMC_Bank5_6->SDCR[0] = (2u << 10)                     /* SDCLK = HCLK/2 (10)      */
                         | (1u << 12)                     /* RBURST                   */
                         | ((SDRAM_RPIPE & 3u) << 13);    /* RPIPE                    */
    FMC_Bank5_6->SDCR[1] = (0u << 0)                       /* NC:  8 column bits (00)  */
                         | (1u << 2)                       /* NR:  12 row bits   (01)  */
                         | (1u << 4)                       /* MWID:16-bit        (01)  */
                         | (1u << 6)                       /* NB:  4 banks       (1)   */
                         | ((SDRAM_CAS_LATENCY & 3u) << 7);/* CAS latency              */

    /* Shared TRP/TRC in SDTR1; the bank-2 timings in SDTR2 (cycles-1 @84 MHz). */
    FMC_Bank5_6->SDTR[0] = (1u << 20)   /* TRP  = 2 */
                         | (6u << 12);  /* TRC  = 7 */
    FMC_Bank5_6->SDTR[1] = (1u << 0)    /* TMRD = 2 */
                         | (6u << 4)    /* TXSR = 7 */
                         | (3u << 8)    /* TRAS = 4 */
                         | (1u << 16)   /* TWR  = 2 */
                         | (1u << 24);  /* TRCD = 2 */

    /* JEDEC power-up sequence */
    sdram_cmd(1u, 1u, 0u);          /* clock config enable */
    short_delay(20000u);            /* >= 100 us at 168 MHz */
    sdram_cmd(2u, 1u, 0u);          /* precharge all (PALL) */
    sdram_cmd(3u, 8u, 0u);          /* 8 auto-refresh cycles */
    /* Load mode register: burst length 1, sequential, CAS latency, single-write */
    {
        uint32_t mrd = (0u << 0)                       /* burst length = 1 */
                     | (0u << 3)                       /* sequential */
                     | ((SDRAM_CAS_LATENCY & 7u) << 4) /* CAS latency */
                     | (0u << 7)                       /* normal op mode */
                     | (1u << 9);                      /* write burst single */
        sdram_cmd(4u, 1u, mrd);
    }
    /* Refresh timer */
    FMC_Bank5_6->SDRTR = (SDRAM_REFRESH << 1);
    while (FMC_Bank5_6->SDSR & FMC_SDSR_BUSY) { }
}
