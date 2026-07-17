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

static void cs_lo(void){ GPIOB->BSRR = (1u << (CS_PIN + 16)); for (volatile int d=0;d<40;d++){} }
static void cs_hi(void){ while (SPI2->SR & SPI_SR_BSY) { } GPIOB->BSRR = (1u << CS_PIN); for (volatile int d=0;d<40;d++){} }
static void spi_drain(void){ (void)SPI2->DR; (void)SPI2->SR; }   /* clear stale RX/OVR */

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

    /* Master, mode 0, software NSS, 8-bit, MSB-first, /128 (~328 kHz) — matches
     * stock SPI2 CR1=0x374. */
    SPI2->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | (6u << 3);
    SPI2->CR2 = 0;
    SPI2->CR1 |= SPI_CR1_SPE;
}

/* Control-surface ADC read, matching stock read_panel_i2c_sliders (sub_ecc):
 * CS(PB12) low -> send {0x01, cmd, 0x00} -> read 3 bytes -> 12-bit result.
 * cmd 0xA0 = channel index 0, 0xE0 = channel index 1 (the multiplier pot + CV). */
uint16_t bsp_pot_read(unsigned ch)
{
    uint8_t cmd = ch ? 0xE0u : 0xA0u;
    spi_drain(); cs_lo();
    (void)spi_xfer(0x01u);
    uint8_t b1 = spi_xfer(cmd);
    uint8_t b2 = spi_xfer(0x00u);
    cs_hi();
    return (uint16_t)(((b1 & 0x0Fu) << 8) | b2);   /* [BENCH] exact bit layout */
}

/* Diagnostic: capture the raw 3 response bytes for both channels (read over SWD). */
volatile uint8_t g_spi_raw[2][3];
void bsp_spi2_probe(void)
{
    for (int ch = 0; ch < 2; ++ch) {
        uint8_t cmd = ch ? 0xE0u : 0xA0u;
        spi_drain(); cs_lo();
        g_spi_raw[ch][0] = spi_xfer(0x01u);
        g_spi_raw[ch][1] = spi_xfer(cmd);
        g_spi_raw[ch][2] = spi_xfer(0x00u);
        cs_hi();
    }
}
