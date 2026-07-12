/* main.c — 288r community firmware top level (clone).
 *
 * The hardware-independent engine (engine.c et al.) is complete and tested. This
 * file is the bring-up skeleton that wires it to the STM32F429 peripherals. The
 * HAL init and the exact peripheral/pin/clock config are produced by CubeMX from
 * the confirmed board pinout — those calls are declared `extern` here and marked
 * TODO(cube). See firmware/README.md "Blocked on hardware".
 *
 * Data flow on hardware: SAI2 RX/TX run full-duplex over DMA2 in double-buffer
 * (ping-pong). The half/complete IRQs hand a block of samples to audio_block(),
 * which runs engine_process() per sample. Panel (ADC/I2C/GPIO) is polled in the
 * superloop and pushes parameters into the engine.
 */
#include "engine.h"
#include <stdint.h>

/* ------------------------------------------------------------------ config */
/* CONFIRM on bench: base sample rate, block size, SDRAM buffer length, and the
 * SHORT/FULL cycle lengths in samples at this rate. */
#define SAMPLE_RATE_HZ   96000u          /* TODO(bench): chosen fixed base rate  */
#define BLOCK_SAMPLES    16u             /* matches the 0x10 block seen in RE     */
#define DELAY_LEN        (2u*1024u*1024u)/* samples in SDRAM; TODO(bench): size   */
#define BASE_DELAY_SHORT (SAMPLE_RATE_HZ/4u)
#define BASE_DELAY_FULL  (SAMPLE_RATE_HZ)

/* Delay buffer in external SDRAM (see linker .sdram section). */
static float delay_buf[DELAY_LEN] __attribute__((section(".sdram")));

static engine_t g_engine;

/* ---- TODO(cube): provided by CubeMX-generated HAL from the board pinout ---- */
extern void SystemClock_Config(void);   /* HSE/PLL + PLLSAI for the codec clock  */
extern void MX_GPIO_Init(void);
extern void MX_FMC_SDRAM_Init(void);     /* external delay RAM                    */
extern void MX_SAI2_Init(void);          /* 24-bit full-duplex codec I/O          */
extern void MX_DMA_Init(void);           /* DMA2 streams for SAI2 RX/TX           */
extern void MX_ADC_Init(void);           /* CV / pots / trimmers                  */
extern void MX_I2C_Init(void);           /* output-mixer slider bank              */
extern void MX_TIM_Init(void);           /* pulse-output timing                   */

/* ---- panel -> engine params (TODO: panel.c; needs pin/channel map) -------- */
static float panel_time_raw01(void) { return 0.5f; }   /* TODO(bench): ADC map   */
static void  panel_poll(engine_t *e) { (void)e;        /* TODO: sliders/switches */ }

/* Called from the SAI DMA half/complete IRQ with one block of interleaved I/O.
 * `in`/`out` are the codec block buffers (format conversion is TODO(bench):
 * the codec word format & channel layout come from the codec part number). */
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
    SystemClock_Config();
    MX_GPIO_Init();
    MX_FMC_SDRAM_Init();
    MX_DMA_Init();
    MX_SAI2_Init();
    MX_ADC_Init();
    MX_I2C_Init();
    MX_TIM_Init();

    engine_init(&g_engine, delay_buf, DELAY_LEN,
                (float)BASE_DELAY_FULL, /*time_lo*/ 0.25f, /*time_hi*/ 20.0f, /*slew*/ 0.15f);

    /* Superloop: audio runs in the SAI/DMA IRQ via audio_block(); here we poll
     * the panel and update engine parameters. */
    for (;;) {
        panel_poll(&g_engine);
    }
}
