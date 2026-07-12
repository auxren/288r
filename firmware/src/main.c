/* main.c — 288r community firmware top level (clone).
 *
 * The hardware-independent engine (engine.c et al.) is complete and tested. This
 * file is the bring-up skeleton that wires it to the STM32F429 peripherals. The
 * The peripheral/pin/clock init is hand-written on the Standard Peripheral Library
 * (reuse the MARF 248r's `Libraries/`) from the confirmed board pinout — those init
 * calls are declared `extern` here and marked TODO(init). See firmware/README.md
 * "Blocked on hardware".
 *
 * Data flow on hardware: SAI2 RX/TX run full-duplex over DMA2 in double-buffer
 * (ping-pong). The half/complete IRQs hand a block of samples to audio_block(),
 * which runs engine_process() per sample. Panel (ADC/I2C/GPIO) is polled in the
 * superloop and pushes parameters into the engine.
 */
#include "engine.h"
#include <stdint.h>

/* ------------------------------------------------------------------ config */
/* Hardware brief (board photo + BOM, see re/notes/hardware.md):
 *   MCU   = STM32F429Z (confirm ZE=512K vs ZI=2M flash).
 *   Codec = Cirrus CS42888 (4 ADC-in / 8 DAC-out, 24-bit, TDM/I2S, I2C or SPI2
 *           control). The 8 taps map to the 8 DAC outputs -> the F429 drives it
 *           over SAI2 multichannel TDM; audio_io/output should be 8-channel, not
 *           stereo. Confirm TDM slot map + control bus from the codec-init code.
 *   SDRAM = ISSI IS42S16400, 8 MB (4M x 16), 16-bit -> store int16 samples
 *           (float32 40 s = 15 MB won't fit); int16<->float at the codec boundary,
 *           float delay_line stays the host reference (see firmware/DESIGN.md).
 *   Rate  = 24-bit / 96 kHz (vendor "196KHz" is a typo). */
#define SAMPLE_RATE_HZ   96000u          /* confirmed 24/96 (VERIFY codec)        */
#define BLOCK_SAMPLES    16u             /* matches the 0x10 block seen in RE     */
#define DELAY_LEN        (2u*1024u*1024u)/* samples; TODO(bench): from SDRAM size */
#define BASE_DELAY_SHORT (SAMPLE_RATE_HZ/4u)
#define BASE_DELAY_FULL  (SAMPLE_RATE_HZ)

/* Delay buffer in external SDRAM (see linker .sdram section). */
static float delay_buf[DELAY_LEN] __attribute__((section(".sdram")));

static engine_t g_engine;

/* ---- TODO(init): hand-written StdPeriph init from the board pinout (⇐ MARF) ---- */
extern void clock_init(void);   /* HSE/PLL + PLLSAI for the codec clock  */
extern void gpio_init(void);
extern void sdram_init(void);   /* FMC → external delay RAM              */
extern void sai2_init(void);    /* 24-bit multichannel TDM (CS42888)     */
extern void dma_init(void);     /* DMA2 streams for SAI2 RX/TX           */
extern void adc_init(void);     /* CV / pots / trimmers (via 4051 mux)   */
extern void i2c_init(void);     /* codec control + slider bank           */
extern void tim_init(void);     /* pulse-output timing                   */

/* ---- panel -> engine params (TODO: panel.c; needs pin/channel map) -------- */
static float panel_time_raw01(void) { return 0.5f; }   /* TODO(bench): ADC map   */
static void  panel_poll(engine_t *e) { (void)e;        /* TODO: sliders/switches */ }

/* Called from the SAI DMA half/complete IRQ with one block of I/O.
 * NOTE(codec): the CS42888 is 4-in / 8-out over TDM, so `in`/`out` are actually
 * multichannel-interleaved (4 ADC slots in, 8 DAC slots out per frame) — the loop
 * below is a single-channel placeholder. TODO(bench): confirm the TDM slot map,
 * then emit each of the 8 taps to its own DAC slot and read the input/CV slots.
 * Word format assumed left-justified 24-bit in int32 — verify from codec-init. */
void audio_block(const int32_t *in, int32_t *out, unsigned n)
{
    const float to_f   = 1.0f / 8388608.0f;   /* 24-bit -> [-1,1)  (int24 in int32) */
    const float from_f = 8388607.0f;
    float t = panel_time_raw01();
    for (unsigned i = 0; i < n; i++) {
        float x = (float)(in[i] >> 8) * to_f;          /* assumes left-justified 24b */
        float y = engine_process(&g_engine, x, t);
        int32_t s = (int32_t)(y * from_f);
        out[i] = s << 8;
    }
}

int main(void)
{
    clock_init();
    gpio_init();
    sdram_init();
    dma_init();
    sai2_init();
    adc_init();
    i2c_init();
    tim_init();

    engine_init(&g_engine, delay_buf, DELAY_LEN,
                (float)BASE_DELAY_FULL, /*time_lo*/ 0.25f, /*time_hi*/ 20.0f, /*slew*/ 0.15f);

    /* Superloop: audio runs in the SAI/DMA IRQ via audio_block(); here we poll
     * the panel and update engine parameters. */
    for (;;) {
        panel_poll(&g_engine);
    }
}
