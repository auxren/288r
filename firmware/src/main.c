/* main.c — 288r community firmware top level (clone, bring-up image).
 *
 * Wires the host-tested engine (engine.c et al.) to the F429 via the bare-metal
 * BSP (in src/bsp). This is the FIRST flashable bring-up: it establishes clock ->
 * SDRAM -> codec -> SAI/DMA and runs the smooth fractional-delay engine, 8 taps
 * out the 8 DAC channels. TIME-mode fractional delay is the headline fix.
 *
 * TIME MULTIPLIER is live: read from the SPI2 control ADC (channel 0 = Time-CV,
 * confirmed on hardware) -> smooth delay-time modulation (chorus/flanger). Still
 * being resolved (bench session 3): the coarse multiplier KNOB is a combined
 * ADC3(4051-mux) + SPI2 read; the 74HC595/4051 DIP+trimmer scan; momentary-switch
 * transport; LEDs; settings/calibration. See re/notes/bench-session-3.md.
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

/* --- SDRAM self-test (results read over SWD; boot self-check) --- */
volatile uint32_t g_mt_errors, g_mt_first_i, g_mt_first_exp, g_mt_first_got, g_mt_done;
static void sdram_memtest(void)
{
    volatile uint32_t *p = (volatile uint32_t *)SDRAM_BASE;
    const uint32_t n = SDRAM_BYTES / 4u;
    for (uint32_t i = 0; i < n; ++i) p[i] = i * 2654435761u;   /* distinct per word */
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t exp = i * 2654435761u, got = p[i];
        if (got != exp) {
            if (!g_mt_errors) { g_mt_first_i = i; g_mt_first_exp = exp; g_mt_first_got = got; }
            g_mt_errors++;
        }
    }
    g_mt_done = 0xD09E;
}

/* --- live engine telemetry (read over SWD) --- */
volatile float    g_dbg_in;            /* last input sample                 */
volatile float    g_dbg_chan[NUM_TAPS];/* last 8 per-tap outputs            */
volatile float    g_dbg_tapdelay[NUM_TAPS]; /* taps.cur[] in samples        */
volatile float    g_dbg_mult;          /* current time multiplier           */
volatile uint32_t g_dbg_wpos;          /* delay write pointer               */
volatile float    g_dbg_base;          /* base delay (cycle length)         */

/* TIME control in [0,1]. Updated by the (not-yet-wired) panel/CV layer; fixed for
 * now. volatile: written in the superloop, read in the audio ISR. */
static volatile float g_time_raw01 = 0.5f;

/* panel diagnostics (SWD): switch bits in; 595 pattern out (write via SWD to probe). */
volatile uint16_t g_switches;
volatile uint32_t g_led_out;      /* 0 = don't drive the 595 (safe default) */
volatile uint8_t  g_led_enable;   /* set via SWD to start driving g_led_out */

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
        g_dbg_in = x;
        for (int i = 0; i < NUM_TAPS; ++i) {
            g_dbg_chan[i]     = chan[i];
            g_dbg_tapdelay[i] = g_engine.taps.cur[i];
        }
        g_dbg_mult = g_engine.time.mult;
        g_dbg_wpos = g_engine.dl.wpos;
    }
}

int main(void)
{
    bsp_clock_init();
    bsp_sdram_init();
    sdram_memtest();
    bsp_panel_gpio_init();

    /* Cycle length (SHORT/FULL) sets the base delay window. FULL = 1 s @96 k. */
    float base = bsp_sw_full_cycle() ? (float)SAMPLE_RATE_HZ
                                     : (float)SAMPLE_RATE_HZ / 4.0f;
    engine_init(&g_engine, delay_buf, DELAY_LEN, base,
                /*time_lo*/ 0.25f, /*time_hi*/ 4.0f, /*slew*/ 0.15f);
    g_dbg_base = base;

    unsigned bits = bsp_resolution_bits();
    g_engine.vintage_bits = (bits < 16u) ? (int)bits : 0;  /* 12-bit -> vintage */

    bsp_spi2_adc_init();      /* control-surface ADC (multiplier is here) */

    /* Start SAI/MCLK BEFORE codec config: the CS42888 control port needs a valid
     * MCLK to respond on I2C. Engine already init'd, so the ISR is safe to run. */
    bsp_audio_init();
    bsp_audio_start();

    /* Codec: if it NAKs (wrong address/regs at the bench), keep running so the
     * clock/SDRAM/SAI can still be probed — audio just won't pass. */
    (void)bsp_codec_init();
    bsp_panel_init();         /* 74HC165 switches + 74HC595 out (bit-banged) */

    for (;;) {
        /* TIME MULTIPLIER = SPI2 control ADC, read ch0 then ch1 in one probe
         * (stock sub_ecc order). ch0 = Time-CV, ch1 = KNOB. Knob sets it, CV adds. */
        bsp_spi2_probe();   /* -> g_spi_raw[0]=ch0, [1]=ch1 */
        uint32_t cv   = ((g_spi_raw[0][1] & 0x0F) << 8) | g_spi_raw[0][2];
        uint32_t knob = ((g_spi_raw[1][1] & 0x0F) << 8) | g_spi_raw[1][2];
        uint32_t raw  = knob + cv;
        if (raw > 4095u) raw = 4095u;
        g_time_raw01 = (float)raw * (1.0f / 4095.0f);
        /* DIAGNOSTIC: read the switch chain (safe, input-only). */
        g_switches = bsp_panel_switches_read();
        /* Only drive the 595 outputs once armed over SWD (avoids disturbing an
         * unknown output state — e.g. a 4051-enable or codec-reset bit). */
        if (g_led_enable) bsp_panel_out(g_led_out);
        __asm volatile ("wfi");
    }
}
