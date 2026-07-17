/* spi2_adc.c — control-surface ADC over SPI2 (sliders/pots).
 *
 * Confirmed (RE + bench 1): SPI2 is in SPI master mode reading an external ADC,
 * 3 bytes/transfer, chip-select on GPIOB12. Three bytes/transfer matches an
 * MCP320x-style transaction, so that protocol is implemented here — [BENCH] to
 * confirm the exact ADC part, the pin orientation (MISO/MOSI), and the channel map
 * (which ADC channel is the multiplier pot, which are the 9 sliders + 7 pots).
 *
 * Pins (AF5): PB13 SCK, PB14 MISO, PB15 MOSI, PB12 = GPIO chip-select.
 */
#include "stm32f429xx.h"
#include "board.h"
#include "bsp.h"

#define CS_PIN 12u

static uint8_t spi_xfer(uint8_t tx)
{
    while (!(SPI2->SR & SPI_SR_TXE)) { }
    *(volatile uint8_t *)&SPI2->DR = tx;
    while (!(SPI2->SR & SPI_SR_RXNE)) { }
    return *(volatile uint8_t *)&SPI2->DR;
}

void bsp_spi2_adc_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;
    (void)RCC->APB1ENR;

    /* PB13/14/15 -> AF5; PB12 -> output (CS), idle high. */
    for (uint32_t n = 13; n <= 15; ++n) {
        GPIOB->MODER   = (GPIOB->MODER & ~(3u << (n*2))) | (2u << (n*2));
        GPIOB->OSPEEDR |= (2u << (n*2));
        GPIOB->AFR[1]  = (GPIOB->AFR[1] & ~(0xFu << ((n-8)*4))) | (5u << ((n-8)*4));
    }
    GPIOB->MODER = (GPIOB->MODER & ~(3u << (CS_PIN*2))) | (1u << (CS_PIN*2)); /* output */
    GPIOB->BSRR  = (1u << CS_PIN);                                            /* CS high */

    /* Master, mode 0, software NSS, 8-bit, MSB-first, /32 -> ~1.3 MHz @ APB1 42 MHz. */
    SPI2->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | (4u << 3);
    SPI2->CR2 = 0;
    SPI2->CR1 |= SPI_CR1_SPE;
}

/* MCP320x single-ended read of `ch` (0..7): raw 12-bit. [BENCH] confirm part/map. */
uint16_t bsp_pot_read(unsigned ch)
{
    GPIOB->BSRR = (1u << (CS_PIN + 16));                 /* CS low  */
    (void)spi_xfer((uint8_t)(0x06u | ((ch >> 2) & 1u))); /* start + single + D2 */
    uint8_t hi = spi_xfer((uint8_t)((ch & 3u) << 6));    /* D1 D0 ... -> top 4 bits */
    uint8_t lo = spi_xfer(0x00u);
    GPIOB->BSRR = (1u << CS_PIN);                        /* CS high */
    return (uint16_t)(((hi & 0x0Fu) << 8) | lo);
}
