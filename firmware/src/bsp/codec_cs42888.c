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
    /* CONFIRMED from stock GPIOB dump: I2C1 on PB8 = SCL, PB9 = SDA, AF4,
     * open-drain, internal pull-ups (stock sets PUPDR=pull-up on both). */
    for (uint32_t n = 8; n <= 9; ++n) {
        GPIOB->MODER   = (GPIOB->MODER   & ~(3u << (n*2))) | (2u << (n*2));      /* AF   */
        GPIOB->OTYPER |= (1u << n);                                              /* OD   */
        GPIOB->OSPEEDR |= (1u << (n*2));
        GPIOB->PUPDR   = (GPIOB->PUPDR   & ~(3u << (n*2))) | (1u << (n*2));      /* PU   */
        GPIOB->AFR[1]  = (GPIOB->AFR[1]  & ~(0xFu << ((n-8)*4))) | (4u << ((n-8)*4)); /* AF4 (AFRH) */
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

/* Single-register read (CS42888: write MAP, repeated-start, read one byte). */
static int cs_read(uint8_t reg, uint8_t *out)
{
    I2C1->CR1 |= I2C_CR1_START;
    if (wait_flag(&I2C1->SR1, I2C_SR1_SB, 1)) return -1;
    I2C1->DR = (uint8_t)(CS42888_I2C_ADDR7 << 1);              /* write */
    if (wait_flag(&I2C1->SR1, I2C_SR1_ADDR, 1)) { I2C1->CR1 |= I2C_CR1_STOP; return -2; }
    (void)I2C1->SR2;
    if (wait_flag(&I2C1->SR1, I2C_SR1_TXE, 1)) return -3;
    I2C1->DR = reg;
    if (wait_flag(&I2C1->SR1, I2C_SR1_BTF, 1)) return -4;
    I2C1->CR1 |= I2C_CR1_START;                               /* repeated start */
    if (wait_flag(&I2C1->SR1, I2C_SR1_SB, 1)) return -5;
    I2C1->DR = (uint8_t)((CS42888_I2C_ADDR7 << 1) | 1u);      /* read */
    I2C1->CR1 &= ~I2C_CR1_ACK;                                /* NACK single byte */
    if (wait_flag(&I2C1->SR1, I2C_SR1_ADDR, 1)) { I2C1->CR1 |= I2C_CR1_STOP; return -6; }
    (void)I2C1->SR2;
    I2C1->CR1 |= I2C_CR1_STOP;
    if (wait_flag(&I2C1->SR1, I2C_SR1_RXNE, 1)) return -7;
    *out = (uint8_t)I2C1->DR;
    I2C1->CR1 |= I2C_CR1_ACK;                                 /* restore for next */
    return 0;
}

/* Diagnostic: chip ID + key regs read back after config (inspect over SWD). */
volatile uint8_t  g_codec_regs[10] = {0};
volatile uint32_t g_codec_rc = 0xEEEEEEEE;

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

/* ---- I2C bus scan (diagnostic; read the globals over SWD) --------------- */
/* g_i2c_scan = 128-bit ACK bitmap of 7-bit addresses; g_i2c_sb_fail counts
 * probes where the peripheral never generated a START (=> I2C/pins broken, not
 * just "no device"); g_i2c_scan_done = 0xC0FFEE when finished. */
volatile uint32_t g_i2c_scan[4]   = {0,0,0,0};
volatile uint32_t g_i2c_sb_fail   = 0;
volatile uint32_t g_i2c_scan_done = 0;

static int i2c_probe(uint8_t addr7)
{
    uint32_t t = I2C_TIMEOUT;
    while ((I2C1->SR2 & I2C_SR2_BUSY) && t--) { }
    I2C1->CR1 |= I2C_CR1_START;
    t = I2C_TIMEOUT;
    while (!(I2C1->SR1 & I2C_SR1_SB) && t--) { }
    if (!(I2C1->SR1 & I2C_SR1_SB)) { I2C1->CR1 |= I2C_CR1_STOP; return -1; }  /* START never formed */
    I2C1->DR = (uint8_t)(addr7 << 1);
    int ack = 0; t = I2C_TIMEOUT;
    while (t--) {
        uint32_t s = I2C1->SR1;
        if (s & I2C_SR1_ADDR) { (void)I2C1->SR2; ack = 1; break; }   /* device ACK'd  */
        if (s & I2C_SR1_AF)   { I2C1->SR1 = (uint16_t)~I2C_SR1_AF; break; } /* NAK      */
    }
    I2C1->CR1 |= I2C_CR1_STOP;
    return ack;
}

void bsp_codec_scan(void)
{
    for (unsigned a = 0; a < 128; ++a) {
        int r = i2c_probe((uint8_t)a);
        if (r == 1)      g_i2c_scan[a >> 5] |= (1u << (a & 31u));
        else if (r < 0)  g_i2c_sb_fail++;
        for (volatile int d = 0; d < 2000; ++d) { }   /* inter-probe recovery */
    }
    g_i2c_scan_done = 0xC0FFEEu;
}

/* Release the codec from reset by replicating stock's driven-HIGH GPIO outputs
 * (one of them enables/releases the CS42888). From the stock GPIO dump: PC12 high,
 * and PA{0,1,2,4,7,8,11} high. We leave SWD (PA13/14) and AF pins untouched.
 * [BENCH] narrow down which single pin is the codec RESET once it ACKs. */
static void drive_out_high(GPIO_TypeDef *p, uint32_t n)
{
    p->MODER = (p->MODER & ~(3u << (n*2))) | (1u << (n*2));  /* output */
    p->OTYPER &= ~(1u << n);                                 /* push-pull */
    p->BSRR = (1u << n);                                     /* high */
}

static void codec_reset_release(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOCEN;
    (void)RCC->AHB1ENR;
    drive_out_high(GPIOC, 12);
    static const uint8_t pa[] = {0, 1, 2, 4, 7, 8, 11};
    for (unsigned i = 0; i < sizeof(pa); ++i) drive_out_high(GPIOA, pa[i]);
    for (volatile int d = 0; d < 300000; ++d) { }           /* reset settle */
}

int bsp_codec_init(void)
{
    codec_reset_release();
    i2c1_gpio_init();
    i2c1_periph_init();

    bsp_codec_scan();   /* DIAGNOSTIC: find the real codec address over SWD */

    /* CS42888 TDM/slave init (values from the CS42888 datasheet + NXP fsl driver).
     * F429 SAI is master -> codec is slave (FM=11 both). Interface format 0x36 =
     * DAC_DIF=ADC_DIF=110 (TDM). [BENCH] MFREQ in reg 0x03 may need tuning to the
     * real MCLK. */
    int rc = 0;
    rc |= cs_write(0x02u, 0x7Fu);   /* power down all modules during config       */
    rc |= cs_write(0x03u, 0xF0u);   /* functional mode: DAC_FM=ADC_FM=11 (slave)  */
    rc |= cs_write(0x04u, 0x36u);   /* interface formats: TDM (both DIF = 110)    */
    rc |= cs_write(0x07u, 0xFFu);   /* mute all DAC channels during config        */
    rc |= cs_write(0x02u, 0x00u);   /* power up all                               */
    rc |= cs_write(0x06u, 0x10u);   /* transition control                         */
    rc |= cs_write(0x07u, 0x00u);   /* unmute                                     */
    g_codec_rc = (uint32_t)rc;

    /* Read back key registers (chip ID + config) for SWD inspection. */
    static const uint8_t addrs[10] = {0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x11,0x18};
    for (unsigned i = 0; i < 10; ++i) {
        uint8_t v = 0xFF;
        if (cs_read(addrs[i], &v) == 0) g_codec_regs[i] = v;
        else g_codec_regs[i] = 0xEE;
    }
    return rc ? -1 : 0;
}
