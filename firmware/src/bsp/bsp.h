/* bsp.h — board-support init API for the 288r community firmware.
 *
 * Bare-metal StdPeriph-free bring-up (direct CMSIS register access) for the F429
 * peripherals MARF's F40x StdPeriph copy can't reach (FMC/SDRAM + SAI). GPIO/I2C
 * are done the same way here for consistency. All board constants live in board.h.
 *
 * Init order (see main.c):
 *   bsp_clock_init();     168 MHz + PLLSAI (audio kernel clock)
 *   bsp_sdram_init();     FMC -> IS42S16400 usable at SDRAM_BASE
 *   bsp_panel_gpio_init();  read-only switch inputs
 *   bsp_codec_init();     I2C1 -> CS42888 registers (TDM, 24-bit)
 *   bsp_audio_init(...);  SAI1 TDM + DMA2 double-buffer, then bsp_audio_start()
 *
 * When a DMA half/complete IRQ fires, the driver calls bsp_audio_isr() (weak,
 * overridden in main.c) with one AUDIO_BLOCK_FRAMES block of TDM frames.
 */
#ifndef BSP_H
#define BSP_H

#include <stdint.h>

void bsp_clock_init(void);
void bsp_sdram_init(void);
void bsp_panel_gpio_init(void);
int  bsp_codec_init(void);       /* returns 0 on I2C ack, <0 on failure */
void bsp_audio_init(void);       /* SAI1 + DMA2, buffers armed, not yet running */
void bsp_audio_start(void);      /* enable SAI + DMA -> IRQs start firing        */

/* Raw panel reads (1 = switch asserted; polarity [BENCH]). */
int  bsp_sw_calibrate(void);     /* cal./pre-set   */
int  bsp_sw_full_cycle(void);    /* SHORT/FULL     */
unsigned bsp_resolution_bits(void); /* 12/16/20 from the 2-bit selector */

/* Control-surface ADC over SPI2 (sliders/pots). */
void     bsp_spi2_adc_init(void);
uint16_t bsp_pot_read(unsigned ch);  /* raw 12-bit, MCP320x-style [BENCH map] */
void     bsp_spi2_probe(void);       /* diagnostic: raw bytes -> g_spi_raw[2][3] */

/* TIME MULTIPLIER via internal ADC3 ch6 (PF8). */
void     bsp_mult_init(void);
uint16_t bsp_mult_read(void);        /* raw 12-bit */
float    bsp_mult_read01(void);      /* 0..1 */

/* Called from the SAI DMA ISR with one block of interleaved TDM frames:
 *   in  : frames * TDM_SLOTS int32 (24-bit left-justified), ADC slots 0..3 valid
 *   out : frames * TDM_SLOTS int32, fill DAC slots 0..7 with the 8 tap outputs
 * Default is a weak silence stub; main.c overrides it to run the engine. */
void bsp_audio_isr(const int32_t *in, int32_t *out, unsigned frames);

#endif /* BSP_H */
