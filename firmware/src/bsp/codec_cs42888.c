/* codec_cs42888.c — CS42888 (4-in/8-out, 24-bit TDM) over I2C1.
 *
 * Bus confirmed = I2C1 (bench 1). The register VALUES here are datasheet-derived
 * defaults for TDM slave operation and are [BENCH]: confirm the device address
 * (AD0/AD1 straps), the RESET pin, and the exact interface/mode bytes by sniffing
 * the stock firmware's power-up I2C (docs/bench-bringup.md).
 *
 * All waits are bounded so an absent/NAKing codec returns an error instead of
 * hanging boot — important the first time this runs on the bench.
 */
#include "stm32f429xx.h"
#include "board.h"
#include "bsp.h"

/* CS42888 register map (subset) */
#define CS_REG_POWER      0x02u   /* PDN (bit0), FREEZE (bit7)            */
#define CS_REG_FUNCMODE   0x03u   /* DAC/ADC functional mode, MCLK freq  */
#define CS_REG_INTERFACE  0x04u   /* DAC_DIF[6:4], ADC_DIF[2:0]          */
#define CS_REG_ADCCTL     0x05u
#define CS_REG_TRANS      0x06u
#define CS_REG_DACMUTE    0x07u

#define CS_PDN            0x01u
#define CS_FREEZE         0x80u
#define CS_DIF_TDM        0x06u    /* TDM interface format */

#define I2C_TIMEOUT       200000u

static int wait_flag(volatile uint32_t *sr, uint32_t mask, int want_set)
{
    uint32_t t = I2C_TIMEOUT;
    while (t--) {
        uint32_t v = *sr & mask;
        if (want_set ? (v != 0u) : (v == 0u)) return 0;
    }
    return -1;
}

static void i2c1_gpio_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    (void)RCC->AHB1ENR;
    /* PB6 = I2C1_SCL, PB7 = I2C1_SDA, AF4, open-drain, pull-up. [BENCH] pins */
    for (uint32_t n = 6; n <= 7; ++n) {
        GPIOB->MODER   = (GPIOB->MODER   & ~(3u << (n*2))) | (2u << (n*2));  /* AF   */
        GPIOB->OTYPER |= (1u << n);                                          /* OD   */
        GPIOB->OSPEEDR |= (1u << (n*2));
        GPIOB->PUPDR   = (GPIOB->PUPDR   & ~(3u << (n*2))) | (1u << (n*2));  /* PU   */
        GPIOB->AFR[0]  = (GPIOB->AFR[0]  & ~(0xFu << (n*4))) | (4u << (n*4));/* AF4  */
    }
}

static void i2c1_periph_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;
    (void)RCC->APB1ENR;
    I2C1->CR1 = I2C_CR1_SWRST;
    I2C1->CR1 = 0;
    I2C1->CR2  = (APB1_HZ / 1000000u);                 /* FREQ = 42 MHz            */
    I2C1->CCR  = (APB1_HZ / (2u * I2C1_SPEED_HZ));      /* Sm 100 kHz -> 210        */
    I2C1->TRISE = (APB1_HZ / 1000000u) + 1u;           /* 43                       */
    I2C1->CR1  = I2C_CR1_PE;
}

static int cs_write(uint8_t reg, uint8_t val)
{
    I2C1->CR1 |= I2C_CR1_START;
    if (wait_flag(&I2C1->SR1, I2C_SR1_SB, 1)) return -1;
    I2C1->DR = (uint8_t)(CS42888_I2C_ADDR7 << 1);       /* write */
    if (wait_flag(&I2C1->SR1, I2C_SR1_ADDR, 1)) { I2C1->CR1 |= I2C_CR1_STOP; return -2; }
    (void)I2C1->SR2;                                     /* clear ADDR */
    if (wait_flag(&I2C1->SR1, I2C_SR1_TXE, 1)) return -3;
    I2C1->DR = reg;
    if (wait_flag(&I2C1->SR1, I2C_SR1_TXE, 1)) return -4;
    I2C1->DR = val;
    if (wait_flag(&I2C1->SR1, I2C_SR1_BTF, 1)) return -5;
    I2C1->CR1 |= I2C_CR1_STOP;
    return 0;
}

int bsp_codec_init(void)
{
    i2c1_gpio_init();
    i2c1_periph_init();

    /* TODO(bench): drive the codec RESET pin high here before configuring. */

    int rc = 0;
    rc |= cs_write(CS_REG_POWER,     CS_PDN | CS_FREEZE);        /* config while down */
    rc |= cs_write(CS_REG_FUNCMODE,  0x00u);   /* [BENCH] slave, auto speed         */
    rc |= cs_write(CS_REG_INTERFACE, (uint8_t)((CS_DIF_TDM << 4) | CS_DIF_TDM)); /* DAC+ADC TDM */
    rc |= cs_write(CS_REG_ADCCTL,    0x00u);
    rc |= cs_write(CS_REG_TRANS,     0x00u);
    rc |= cs_write(CS_REG_DACMUTE,   0x00u);   /* unmute all 8 DAC channels         */
    rc |= cs_write(CS_REG_POWER,     0x00u);   /* PDN=0, FREEZE=0 -> run            */
    return rc ? -1 : 0;
}
