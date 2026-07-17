/* clock.c — 168 MHz system clock + PLLSAI audio kernel clock (bare-metal).
 *
 * Confirmed constants (bench 1): HSE 8 MHz, SYSCLK 168, HCLK 168, APB1 42, APB2 84.
 * Main PLL: 8/8*336/2 = 168 MHz.  PLLSAI feeds SAI1 (board.h, [BENCH] audio rate).
 */
#include "stm32f429xx.h"
#include "board.h"
#include "bsp.h"

static void spin_until(volatile uint32_t *reg, uint32_t mask)
{
    while ((*reg & mask) == 0u) { /* wait */ }
}

void bsp_clock_init(void)
{
    /* 1. HSE on, wait ready. */
    RCC->CR |= RCC_CR_HSEON;
    spin_until(&RCC->CR, RCC_CR_HSERDY);

    /* 2. Power interface: voltage scale 1 for 168 MHz. */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;
    PWR->CR |= PWR_CR_VOS;               /* VOS = scale 1 (0b11) */

    /* 3. Flash: prefetch + I/D cache + 5 wait states (168 MHz, 3.3 V). */
    FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_LATENCY_5WS;

    /* 4. Bus prescalers: AHB /1, APB1 /4, APB2 /2. */
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2))
              | RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV4 | RCC_CFGR_PPRE2_DIV2;

    /* 5. Main PLL from HSE. */
    RCC->PLLCFGR =
          (PLL_M <<  0)
        | (PLL_N <<  6)
        | (((PLL_P >> 1) - 1u) << 16)    /* PLLP field = P/2 - 1 */
        | RCC_PLLCFGR_PLLSRC_HSE
        | (PLL_Q << 24);
    RCC->CR |= RCC_CR_PLLON;
    spin_until(&RCC->CR, RCC_CR_PLLRDY);

    /* 6. Switch SYSCLK to the PLL. */
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) { /* wait */ }

    /* 7. PLLSAI -> SAI1 kernel clock (audio). [BENCH] retune to hit 24.576 MHz. */
    RCC->PLLSAICFGR = (PLLSAI_N << 6) | (PLLSAI_Q << 24);
    /* SAI1-A/B clock source = PLLSAI (00), PLLSAIDIVQ divides PLLSAI_Q output. */
    RCC->DCKCFGR = (RCC->DCKCFGR & ~(RCC_DCKCFGR_PLLSAIDIVQ | RCC_DCKCFGR_SAI1ASRC | RCC_DCKCFGR_SAI1BSRC))
                 | ((PLLSAI_DIVQ - 1u) << 8);
    RCC->CR |= RCC_CR_PLLSAION;
    spin_until(&RCC->CR, RCC_CR_PLLSAIRDY);

    SystemCoreClockUpdate();
}
