/* main.c — 288r community firmware top level (clone, bring-up image).
 *
 * Wires the host-tested engine (engine.c et al.) to the F429 via the bare-metal
 * BSP (in src/bsp). This is the FIRST flashable bring-up: it establishes clock ->
 * SDRAM -> codec -> SAI/DMA and runs the smooth fractional-delay engine, 8 taps
 * out the 8 DAC channels. TIME-mode fractional delay is the headline fix.
 *
 * NOT yet wired (next bench layer, see docs/bench-bringup.md): the SPI2 control-
 * surface ADC (sliders/pots/Time-CV), the 74HC595/4051 DIP+trimmer scan, the
 * momentary-switch transport gestures, and settings/calibration persistence. Until
 * those land, the delay time sits at a fixed default and the module is a fixed
 * multi-tap delay — enough to validate the audio path end to end.
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

/* TIME control in [0,1]. Updated by the (not-yet-wired) panel/CV layer; fixed for
 * now. volatile: written in the superloop, read in the audio ISR. */
static volatile float g_time_raw01 = 0.5f;

/* Audio ISR bridge: TDM frame = TDM_SLOTS int32 (ADC slots 0..3 in, DAC slots 0..7
 * out). Pull the input from AUDIO_IN_SLOT, run the engine, scatter the 8 tap
 * outputs into the 8 DAC slots. audio_in_to_f / audio_f_to_out are the codec word
 * conversions from audio_io.c (host-tested, clamped). */
void bsp_audio_isr(const int32_t *in, int32_t *out, unsigned frames)
{
    for (unsigned f = 0; f < frames; ++f) {
        float x = audio_in_to_f(in[f * TDM_SLOTS + AUDIO_IN_SLOT]);
#if USE_CV_MULT
        /* Time-CV from a codec ADC slot -> multiplier, per sample (smooth). */
        float cv = audio_in_to_f(in[f * TDM_SLOTS + MULT_CV_SLOT]);
        float t = (cv - CV_MULT_OFFSET) * CV_MULT_SCALE + 0.5f;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
#else
        float t = g_time_raw01;
#endif
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

    unsigned bits = bsp_resolution_bits();
    g_engine.vintage_bits = (bits < 16u) ? (int)bits : 0;  /* 12-bit -> vintage */

    /* Codec: if it NAKs (wrong address/regs at the bench), keep running so the
     * clock/SDRAM/SAI can still be probed — audio just won't pass. */
    (void)bsp_codec_init();

#if USE_SPI_MULT
    bsp_spi2_adc_init();
#endif

    bsp_audio_init();
    bsp_audio_start();

    for (;;) {
#if USE_SPI_MULT
        /* Multiplier POT from the SPI2 control-surface ADC (knob path). The engine
         * slews it, so a per-loop update rate is plenty. [BENCH] channel + taper. */
        uint16_t raw = bsp_pot_read(MULT_POT_CH);
        g_time_raw01 = (float)raw / POT_MULT_FULLSCALE;
#endif
        /* TODO(bench): scan DIP/trimmer panel; handle momentary-switch transport.
         * With both multiplier sources off (default), the delay runs at a fixed
         * time so the audio path can be validated first. */
        __asm volatile ("wfi");
    }
}
