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
#include "led.h"
#include "panel_ctl.h"
#include <stdint.h>

/* Live panel scan (74HC165). Input-only, so acting on a mis-decoded bit can't drop
 * audio (worst case: a wrong delay time, recoverable). Default ON. [BENCH] confirm
 * PA4/5/6 don't overlap the codec-reset-release GPIO block before trusting it. */
#define PANEL_SCAN_ENABLE 1

/* Front-panel LED drive over the 74HC595. GATED OFF by default: the same 24-bit
 * chain carries the DIP column-select and (likely) the 4051 mux-enable / codec
 * reset (re/notes/panel-scan.md), so shifting an unlabelled word can drop audio.
 * [BENCH] set PANEL_LED_WALK=1, flip PANEL_LED_ENABLE=1, and watch the panel to
 * label the LED bits (and the must-not-touch bits); then drive led_word() for real. */
#define PANEL_LED_ENABLE 0
#define PANEL_LED_WALK   0   /* 1 = shift led_diag_walk() ~1 step/s to find LED bits */

/* Delay memory in external SDRAM. float (~21.8 s @96 k, fills the 8 MB bank). The
 * int16/int32 SDRAM layer (2x capacity, vintage banks) is a staged rewrite module;
 * bring-up uses the validated float delay_line directly. */
#define DELAY_LEN  (SDRAM_BYTES / sizeof(float))     /* 2,097,152 samples */
static float delay_buf[DELAY_LEN] __attribute__((section(".sdram")));

static engine_t g_engine;

#if PANEL_LED_ENABLE
static led_state_t g_leds;
static unsigned    g_led_step;
#endif

extern volatile uint8_t g_spi_raw[2][3];   /* SPI2 control-ADC raw bytes (ch0,ch1) */

/* TIME control in [0,1]; written in the superloop, read in the audio ISR. */
static volatile float g_time_raw01 = 0.5f;

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

    /* --- Boot-config straps: read ONCE here, deliberately NOT polled ----------
     * The back-panel config DIP (resolution, and later sw1 x10-extend / sw2
     * bandwidth) and the SHORT/FULL cycle switch are latched at boot. This is a
     * correctness rule, not a cycle optimization -- a direct GPIO read is ~free.
     * These pick the SDRAM buffer LAYOUT/format and delay range, which cannot
     * change per sample: a live flip would reinterpret already-written buffer
     * samples and pop. Strap semantics = power-cycle to apply (matches stock). A
     * mid-run fidelity change, if ever wanted, is a deliberate buffer reinit, not
     * a passive read. See DESIGN.md "Boot-time layout rule". Do NOT move these into
     * the superloop; wire sw1/sw2 alongside them here. (The front-panel *scanned*
     * DIPs -- tap times, phase/mute -- are the opposite: live controls, scanned
     * out of the audio hot loop.) */

    /* Cycle length (SHORT/FULL) sets the base delay window. FULL = 1 s @96 k. */
    float base = bsp_sw_full_cycle() ? (float)SAMPLE_RATE_HZ
                                     : (float)SAMPLE_RATE_HZ / 4.0f;
    /* config DIP sw1: x10 delay/looper extend. Scale the base window, then clamp so
     * the deepest tap (base*time_hi) still fits the SDRAM buffer. */
    const float time_lo = 0.4f, time_hi = 1.6f;   /* panel legend: 0.4x..1.6x, noon=1.0 */
    if (bsp_sw_delay_extend()) base *= DELAY_EXTEND_FACTOR;
    base = engine_clamp_base(base, DELAY_LEN, time_hi);
    const float base_boot = base;     /* pre-octave base; the scan rescales from here */
    /* TIME MULTIPLIER range from the panel legend (1.0 at noon). The x1/x2 octave
     * switch will scale this once wired. */
    engine_init(&g_engine, delay_buf, DELAY_LEN, base, time_lo, time_hi, /*slew*/ 0.15f);

    /* Fidelity from config DIP SW1 sw3/sw4 (PD11/PD12): 24-bit = full precision
     * (clean), 12/8/4-bit = vintage bit-crush. Owner-confirmed; all-off = 24-bit. */
    unsigned depth = bsp_resolution_bits();
    g_engine.vintage_bits = (depth >= 24u) ? 0 : (int)depth;

    /* config DIP sw2: 11025 Hz record-path bandwidth limit (off = full 24/96). */
    engine_set_bandwidth(&g_engine, (float)SAMPLE_RATE_HZ,
                         bsp_sw_bandwidth_limit() ? BANDWIDTH_LIMIT_HZ : 0.0f);

    bsp_spi2_adc_init();      /* control-surface ADC (multiplier knob + CV) */

    /* Start SAI/MCLK BEFORE codec config: the CS42888 control port needs a valid
     * MCLK to respond on I2C. Engine already init'd, so the ISR is safe to run. */
    bsp_audio_init();
    bsp_audio_start();
    (void)bsp_codec_init();

#if PANEL_LED_ENABLE
    bsp_panel_init();          /* [BENCH] enable only after the 595 bits are labelled */
    led_clear(&g_leds);
#endif

#if PANEL_SCAN_ENABLE
    bsp_panel_switches_init();          /* 165 input pins only (no 595 output) */
    unsigned scan_div = 0, prev_octave = 1, prev_preset = 0;
#endif

    float mult_filt = 0.5f;
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

#if PANEL_SCAN_ENABLE
        /* Panel scan (74HC165) — throttled (~every 64 passes, ~10 ms), input-only,
         * applied ON CHANGE. Octave rescales the base delay (fixed-rate: the taps
         * slew to the new positions, no clock glitch); A/B/C selects the preset
         * phase row. Both glide smoothly, so this never zippers the audio. */
        if ((scan_div++ & 0x3Fu) == 0u) {
            panel_ctl_t pc;
            panel_decode(bsp_panel_switches_read(), &pc);
            if (pc.octave != prev_octave) {
                float nb = engine_clamp_base(base_boot * panel_octave_factor(&pc),
                                             DELAY_LEN, time_hi);
                taps_set_base_delay(&g_engine.taps, nb);
                prev_octave = pc.octave;
            }
            if (pc.preset != prev_preset) {
                float ph[NUM_TAPS];
                (void)panel_preset_phase(pc.preset, ph);
                taps_set_phase(&g_engine.taps, ph);
                prev_preset = pc.preset;
            }
        }
#endif

#if PANEL_LED_ENABLE
        /* LED refresh out of the audio ISR (bit-banged 595, ~few us). */
#if PANEL_LED_WALK
        if ((g_led_step % 6000u) == 0u)          /* ~1 step/s @ 6000 blocks/s */
            bsp_panel_out(led_diag_walk(g_led_step / 6000u));
        g_led_step++;
#else
        bsp_panel_out(led_word(&g_leds, 0));
#endif
#endif
        __asm volatile ("wfi");
    }
}
