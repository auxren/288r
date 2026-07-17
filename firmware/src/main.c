/* main.c — 288r community firmware top level (clone).
 *
 * Wires the host-tested engine (engine.c et al.) to the F429 via the bare-metal
 * BSP (in src/bsp): clock -> SDRAM -> codec -> SAI/DMA -> the smooth fractional
 * delay engine, 8 taps out the 8 DAC channels.
 *
 * WORKING on hardware (bench session 3): multitap delay + smooth TIME-MULTIPLIER
 * modulation from the panel knob AND the Time-CV (both on the SPI2 control ADC).
 *
 * Not yet wired (mechanisms decoded, see re/notes/panel-scan.md): the 74HC595
 * LED/column output, the scanned DIP/trimmer matrix, momentary-switch transport,
 * and settings/calibration. The 74HC165 switch reader is validated (g_switches).
 *
 * Recovery is always SWD: reflash Compiled FW/B288-REV1.0.hex.
 */
#include "bsp/bsp.h"
#include "bsp/board.h"
#include "engine.h"
#include "audio_io.h"
#include <stdint.h>

/* Delay memory in external SDRAM. float (~21.8 s @96 k, fills the 8 MB bank). The
 * int16/int32 SDRAM layer (2x capacity, vintage banks) is a staged rewrite module;
 * bring-up uses the validated float delay_line directly. */
#define DELAY_LEN  (SDRAM_BYTES / sizeof(float))     /* 2,097,152 samples */
static float delay_buf[DELAY_LEN] __attribute__((section(".sdram")));

static engine_t g_engine;

extern volatile uint8_t g_spi_raw[2][3];   /* SPI2 control-ADC raw bytes (ch0,ch1) */

/* --- 595-driven panel scan diagnostic: drive stock's per-address 595 pattern
 * (addr*0x111111), then read ADC3/PF8 (analog) + PC1-6 (DIP rows) per address. --- */
#include "stm32f429xx.h"
volatile uint16_t g_analog[8];    /* ADC3/PF8 per mux address 0..7 */
volatile uint8_t  g_diprows[8];   /* PC1-6 DIP rows per column 0..7 */
static void panel_scan595(void)
{
    for (uint32_t addr = 0; addr < 8; ++addr) {
        bsp_panel_out(addr * 0x111111u);           /* stock col pattern -> mux addr + col */
        for (volatile int d = 0; d < 4000; ++d) { }/* settle */
        g_analog[addr]  = bsp_mult_read();          /* ADC3 ch6 / PF8 */
        g_diprows[addr] = (uint8_t)((GPIOC->IDR >> 1) & 0x3Fu);  /* PC1..PC6 */
    }
}

/* TIME control in [0,1]; written in the superloop, read in the audio ISR. */
static volatile float g_time_raw01 = 0.5f;

/* Latest panel switch word (74HC165); read-only until the switch decode lands. */
volatile uint16_t g_switches;

/* Audio ISR bridge: TDM frame = TDM_SLOTS int32 (ADC slots 0..3 in, DAC slots 0..7
 * out). Pull the input from AUDIO_IN_SLOT, run the engine, scatter the 8 tap
 * outputs into the 8 DAC slots. audio_in_to_f / audio_f_to_out are the codec word
 * conversions from audio_io.c (host-tested, clamped). */
void bsp_audio_isr(const int32_t *in, int32_t *out, unsigned frames)
{
    const float t = g_time_raw01;
    for (unsigned f = 0; f < frames; ++f) {
        float x = audio_in_to_f(in[f * TDM_SLOTS + AUDIO_IN_SLOT]);
        float chan[NUM_TAPS];
        (void)engine_process_multi(&g_engine, x, t, chan);
        for (unsigned s = 0; s < TDM_SLOTS; ++s)
            out[f * TDM_SLOTS + s] = (s < (unsigned)NUM_TAPS)
                                     ? audio_f_to_out(chan[s]) : 0;
    }
}

int main(void)
{
    bsp_clock_init();
    bsp_sdram_init();
    bsp_panel_gpio_init();

    /* Cycle length (SHORT/FULL) sets the base delay window. FULL = 1 s @96 k. */
    float base = bsp_sw_full_cycle() ? (float)SAMPLE_RATE_HZ
                                     : (float)SAMPLE_RATE_HZ / 4.0f;
    engine_init(&g_engine, delay_buf, DELAY_LEN, base,
                /*time_lo*/ 0.25f, /*time_hi*/ 4.0f, /*slew*/ 0.15f);

    /* Fidelity from config DIP SW1 sw3/sw4 (PD11/PD12): 24-bit = full precision
     * (clean), 12/8/4-bit = vintage bit-crush. Owner-confirmed; all-off = 24-bit. */
    unsigned depth = bsp_resolution_bits();
    g_engine.vintage_bits = (depth >= 24u) ? 0 : (int)depth;

    bsp_spi2_adc_init();      /* control-surface ADC (multiplier knob + CV) */

    /* Start SAI/MCLK BEFORE codec config: the CS42888 control port needs a valid
     * MCLK to respond on I2C. Engine already init'd, so the ISR is safe to run. */
    bsp_audio_init();
    bsp_audio_start();
    (void)bsp_codec_init();
    bsp_panel_init();         /* 74HC165 switch reader (bit-banged) */
    bsp_mult_init();          /* ADC3 ch6 (PF8) — analog-mux input, for the scan */

    float mult_filt = 0.5f;
    uint32_t tick = 0;
    for (;;) {
        /* FAST (every loop) — TIME MULTIPLIER = SPI2 ch0(CV)+ch1(knob), stock order.
         * Must update quickly or the delay time steps -> zipper. One-pole smoothing
         * kills the ADC ±1-LSB jitter (stock uses a 128-avg + hysteresis). */
        bsp_spi2_probe();     /* -> g_spi_raw[0]=ch0, [1]=ch1 */
        uint32_t cv   = ((g_spi_raw[0][1] & 0x0F) << 8) | g_spi_raw[0][2];
        uint32_t knob = ((g_spi_raw[1][1] & 0x0F) << 8) | g_spi_raw[1][2];
        uint32_t raw  = knob + cv;
        if (raw > 4095u) raw = 4095u;
        mult_filt += ((float)raw * (1.0f / 4095.0f) - mult_filt) * 0.03f;
        g_time_raw01 = mult_filt;
        (void)tick; (void)panel_scan595;   /* panel scan is out of the hot loop */
        __asm volatile ("wfi");
    }
}
