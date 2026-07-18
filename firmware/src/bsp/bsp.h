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
int  bsp_sw_delay_extend(void);     /* config DIP sw1: x10 delay/looper ([BENCH] pin) */
int  bsp_sw_bandwidth_limit(void);  /* config DIP sw2: 11025 Hz limit ([BENCH] pin)   */

/* Control-surface ADC over SPI2 (sliders/pots). */
void     bsp_spi2_adc_init(void);
uint16_t bsp_pot_read(unsigned ch);  /* raw 12-bit, MCP320x-style [BENCH map] */
void     bsp_spi2_probe(void);       /* diagnostic: raw bytes -> g_spi_raw[2][3] */

/* TIME MULTIPLIER via internal ADC3 ch6 (PF8). */
void     bsp_mult_init(void);
uint16_t bsp_mult_read(void);        /* raw 12-bit */
float    bsp_mult_read01(void);      /* 0..1 */

/* Preset persistence backend (F429 internal-flash EEPROM emulation). [BENCH]: the
 * default flash_preset.c is a RAM placeholder -- works within a power cycle so the
 * save-chord + recall UX validates on hardware, but is NOT reboot-persistent. Swap
 * for real sector erase+program (reads stay memory-mapped). */
const uint8_t *bsp_preset_flash_base(void);                          /* store base  */
int  bsp_preset_flash_write(unsigned slot, const uint8_t *blob, unsigned len);

/* Bit-banged panel I/O (74HC165 switches in, 74HC595 LEDs/columns out). */
void     bsp_panel_init(void);            /* both chains (165 in + 595 out)      */
void     bsp_panel_switches_init(void);   /* 165 input pins ONLY (no 595 output) */
uint16_t bsp_panel_switches_read(void);   /* 13-bit panel_switch_bits */
void     bsp_panel_out(uint32_t bits24);  /* shift+latch 24 output bits */

/* Stock-faithful panel bring-up (decompile-derived, sub_2508/sub_3488):
 * mux address lines to the stock boot state, PC1..PC6 matrix rows as inputs,
 * and the continuous 8-column 595 address sweep the stock runs every loop. */
void     bsp_panel_mux_boot_state(void);  /* PA1/7/8/11 high (stock runtime ODR) */
void     bsp_panel_matrix_init(void);     /* chains + PC1..PC6 row inputs        */
void     bsp_panel_matrix_scan(uint16_t w[3], uint16_t trim[8]); /* sweep -> DIP words + per-column ADC3 trims (trim may be NULL) */
void     bsp_panel_strobe(int level);     /* PA0 analog block strobe (audio ISR) */
void     bsp_panel_match_stock_idle(void); /* match stock's electrical idle state */

/* Called from the SAI DMA ISR with one block of interleaved TDM frames:
 *   in  : frames * TDM_SLOTS int32 (24-bit left-justified), ADC slots 0..3 valid
 *   out : frames * TDM_SLOTS int32, fill DAC slots 0..7 with the 8 tap outputs
 * Default is a weak silence stub; main.c overrides it to run the engine. */
void bsp_audio_isr(const int32_t *in, int32_t *out, unsigned frames);

#endif /* BSP_H */
