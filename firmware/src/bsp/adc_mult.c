/* adc_mult.c — TIME MULTIPLIER control via the F429 internal ADC.
 *
 * Confirmed from the stock firmware (RE + live regs): the multiplier is read on
 * ADC3 channel 6 = pin PF8 (ADC3_IN6 is exclusive to PF8; stock SQR3=6, PF8 in
 * analog mode). 12-bit; stock smooths + hysteresis-filters it. Here we read it
 * raw and let the engine's time-control slew it.
 *
 * [BENCH] direction/taper: if the knob feels reversed, invert in bsp_mult_read01.
 */
#include "stm32f429xx.h"
#include "board.h"
#include "bsp.h"

void bsp_mult_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_ADC3EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOFEN;
    (void)RCC->APB2ENR;

    GPIOF->MODER |= (3u << (8u * 2u));       /* PF8 -> analog */

    ADC->CCR = (ADC->CCR & ~(3u << 16)) | (1u << 16);  /* ADCPRE = PCLK2/4 (~21 MHz) */

    ADC3->CR1  = 0;                            /* 12-bit, no scan            */
    ADC3->SQR1 = 0;                            /* L = 0 -> 1 conversion      */
    ADC3->SQR3 = 6u;                           /* SQ1 = channel 6 (PF8)      */
    ADC3->SMPR2 = (7u << (6u * 3u));           /* ch6 sample time = 480 cyc  */
    ADC3->CR2  = ADC_CR2_ADON;                 /* enable                     */
    for (volatile int d = 0; d < 10000; ++d) { }  /* Tstab                   */
}

uint16_t bsp_mult_read(void)
{
    ADC3->CR2 |= ADC_CR2_SWSTART;
    while (!(ADC3->SR & ADC_SR_EOC)) { }
    return (uint16_t)(ADC3->DR & 0x0FFFu);     /* 12-bit result */
}

float bsp_mult_read01(void)
{
    return (float)bsp_mult_read() * (1.0f / 4095.0f);
}
