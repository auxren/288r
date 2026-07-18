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
#include "preset_store.h"
#include "chord.h"
#include <stdint.h>

/* Live panel scan (74HC165). Input-only, so acting on a mis-decoded bit can't drop
 * audio (worst case: a wrong delay time, recoverable). Default ON. [BENCH] confirm
 * PA4/5/6 don't overlap the codec-reset-release GPIO block before trusting it. */
#define PANEL_SCAN_ENABLE 1

/* Stock-faithful 595 address sweep + DIP-matrix row read (decompile: sub_3488).
 * The stock runs this every loop; the LEDs/analog tree hang off the scanned mux
 * chain, so NOT running it leaves the panel dark. Columns are the stock's exact
 * words (0x000000..0x777777). */
#define PANEL_MATRIX_ENABLE 1

/* WRITE/RECIRC transport from the momentary switches (panel_ctl write/recirc_trig).
 * Momentary POLARITY is auto-calibrated: at boot nobody presses anything, so the
 * first scan's level IS the idle level; pressed = level != idle. No boot misfires
 * either way. */
#define PANEL_TRANSPORT_ENABLE 1

/* Savable presets (Option A: capture-live). A/B/C/D recall a tap pattern from the
 * preset store; holding both momentaries ~2 s snapshots the current pattern into
 * the selected slot. Backend is flash_preset.c — currently the RAM placeholder, so
 * saves last until power-off ([BENCH]: swap in internal-flash for reboot-proof). */
#define PRESET_ENABLE 1
#define PRESET_SAVE_HOLD_TICKS 200u   /* ~2 s at the ~10 ms scan cadence */

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

#if PANEL_SCAN_ENABLE && PANEL_TRANSPORT_ENABLE
static xport_trig_t g_xtrig;
#endif

#if PANEL_SCAN_ENABLE && PRESET_ENABLE
static chord_t g_save_chord;
#endif

extern volatile uint8_t g_spi_raw[2][3];   /* SPI2 control-ADC raw bytes (ch0,ch1) */

/* TIME control in [0,1]; written in the superloop, read in the audio ISR. */
static volatile float g_time_raw01 = 0.5f;

/* SWD observability: live panel decode + raw control reads, for labelling the
 * [BENCH] bits at the bench (gdb `p g_dbg_panel`, or `mdw &g_dbg_panel`). Populated
 * in the superloop. Strip pre-release with the other g_dbg_* scaffolding. */
struct dbg_panel {
    uint16_t sw165;       /* raw 74HC165 switch word              */
    uint16_t spi_cv;      /* raw Time-CV        (SPI2 ch0)         */
    uint16_t spi_knob;    /* raw multiplier knob (SPI2 ch1)       */
    uint16_t dip[3];      /* DIP/preset matrix words (stock c4/c6/c8 layout) */
    uint16_t trim[8];     /* ADC3/PF8 reading per 595 column (analog scan)   */
    uint8_t  preset;      /* decoded A/B/C (0/1/2)                */
    uint8_t  octave;      /* decoded x1/x2/x4                      */
    uint8_t  bank_b;
    uint8_t  write_trig;  /* polarity-corrected (pressed=1)       */
    uint8_t  recirc_trig; /* polarity-corrected (pressed=1)       */
    uint8_t  saved_blink; /* increments on each preset save        */
    float    mult;        /* smoothed multiplier [0,1]            */
    float    base;        /* current taps base_delay (samples)    */
};
volatile struct dbg_panel g_dbg_panel __attribute__((used));

/* Audio ISR bridge: TDM frame = TDM_SLOTS int32 (ADC slots 0..3 in, DAC slots 0..7
 * out). Pull the input from AUDIO_IN_SLOT, run the engine, scatter the 8 tap
 * outputs into the 8 DAC slots. audio_in_to_f / audio_f_to_out are the codec word
 * conversions from audio_io.c (host-tested, clamped). */
void bsp_audio_isr(const int32_t *in, int32_t *out, unsigned frames)
{
    const float t = g_time_raw01;
    for (unsigned f = 0; f < frames; ++f) {
        float x = audio_in_to_f(in[f * TDM_SLOTS + AUDIO_IN_SLOT]);
        /* INPUT LED (PA0) — decompile-exact: the stock compares each input sample
         * against +0.5 FS in the tap service and drives PA0 LOW (LED ON) above it:
         *   if (r5 + 0x400000 > 0x800000) sub_fe0(0); else sub_102c(0);
         * A per-sample 1-bit envelope PWM: loud input -> more low-time -> brighter.
         * (PA0/1/7/8/11 are DSP-driven indicator outputs, NOT mux addresses — the
         * old panel-scan.md label was wrong.) */
        bsp_panel_strobe(x > 0.5f ? 0 : 1);
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
    int scan_first = 1;                 /* first pass captures momentary idle levels */
    unsigned idle_w = 0;                /* auto-polarity: pressed = level != idle    */
#if PANEL_TRANSPORT_ENABLE
    transport_trig_init(&g_xtrig);
#endif
#if PRESET_ENABLE
    chord_init(&g_save_chord);
    prev_preset = 0xFFu;    /* force a load of the selected slot on the first scan */
#endif
#endif
#if PANEL_MATRIX_ENABLE
    /* Stock-faithful panel bring-up: mux/control lines to the stock RUNTIME state
     * (PA1/7/8/11 high — live ODR 0x996), row inputs ready, ADC3 on for the
     * per-column analog scan, and the 595 sweep runs from here on. */
    bsp_panel_mux_boot_state();
    bsp_panel_matrix_init();
    bsp_mult_init();               /* ADC3 ch6/PF8 (stock-matching config) */
    bsp_panel_match_stock_idle();  /* pull-ups off, USART1 pins driven, etc. */
#endif

    float mult_filt = 0.5f;
    for (;;) {
#if PANEL_SCAN_ENABLE
        /* Panel + control tick (~every 64 passes, ~10 ms). The SPI2 control-ADC
         * probe lives HERE, not every pass: hammering it at loop rate parked the
         * ADC's chip-select low ~100% duty (stock probes at ~6 Hz, CS idles HIGH
         * — live dump) and that DC-loads the control-surface analog. ~100 Hz is
         * still 15x the stock rate; the one-pole + audio-rate tap slew keep the
         * multiplier smooth. */
        if ((scan_div++ & 0x3Fu) == 0u) {
            bsp_spi2_probe();     /* -> g_spi_raw[0]=ch0(CV), [1]=ch1(knob) */
            uint32_t cv   = ((g_spi_raw[0][1] & 0x0F) << 8) | g_spi_raw[0][2];
            uint32_t knob = ((g_spi_raw[1][1] & 0x0F) << 8) | g_spi_raw[1][2];
            uint32_t raw  = knob + cv;
            if (raw > 4095u) raw = 4095u;
            mult_filt += ((float)raw * (1.0f / 4095.0f) - mult_filt) * 0.3f;
            g_time_raw01 = mult_filt;
            g_dbg_panel.spi_cv = (uint16_t)cv;
            g_dbg_panel.spi_knob = (uint16_t)knob;
            g_dbg_panel.mult = mult_filt;

            panel_ctl_t pc;
            uint16_t sw = bsp_panel_switches_read();
            panel_decode(sw, &pc);
#if PANEL_MATRIX_ENABLE
            /* Full 8-column 595 address sweep + DIP rows + per-column ADC3 trims
             * (stock: every loop). */
            uint16_t dip[3], trim[8];
            bsp_panel_matrix_scan(dip, trim);
            for (int i = 0; i < 3; i++) g_dbg_panel.dip[i] = dip[i];
            for (int i = 0; i < 8; i++) g_dbg_panel.trim[i] = trim[i];
#endif
            /* Momentary polarity: boot level = idle (nobody pressing at boot).
             * Confirmed on the unit: the momentary is bit 8, idle HIGH. */
            if (scan_first) { idle_w = pc.write_trig; scan_first = 0; }
            unsigned trig_act = (pc.write_trig != idle_w);
            unsigned wr_act = trig_act, rc_act = trig_act;   /* single trigger */
            g_dbg_panel.sw165 = sw;
            g_dbg_panel.preset = pc.preset;
            g_dbg_panel.octave = pc.octave;
            g_dbg_panel.bank_b = pc.time_pitch;   /* dbg slot reused: TIME/pitch */
            g_dbg_panel.write_trig = (uint8_t)trig_act;
            g_dbg_panel.recirc_trig = (uint8_t)!transport_should_write(&g_engine.xport);
            g_dbg_panel.base = g_engine.taps.base_delay;
#if PANEL_TRANSPORT_ENABLE
            /* One confirmed momentary -> TAP toggles WRITE <-> RECIRC (loop capture
             * on entry to RECIRC), HOLD ~2 s -> preset save (below). */
            if (trig_act && !g_xtrig.prev_w) {
                if (transport_should_write(&g_engine.xport)) engine_recirc(&g_engine);
                else                                          engine_write(&g_engine);
            }
            g_xtrig.prev_w = (uint8_t)trig_act;
#endif
            if (pc.octave != prev_octave) {
                float nb = engine_clamp_base(base_boot * panel_octave_factor(&pc),
                                             DELAY_LEN, time_hi);
                taps_set_base_delay(&g_engine.taps, nb);
                prev_octave = pc.octave;
            }
            if (pc.preset != prev_preset) {
                float ph[NUM_TAPS];
#if PRESET_ENABLE
                /* Recall the selected slot from flash (capture-live presets). Blank/
                 * invalid slot -> the A-ramp default. mult recall would pin the knob
                 * (control-pinning) once the multiplier reads through the panel too. */
                preset_scene_t sc;
                (void)preset_load(bsp_preset_flash_base(), pc.preset, &sc);
                for (int i = 0; i < NUM_TAPS; i++) ph[i] = sc.phase[i];
#else
                (void)panel_preset_phase(pc.preset, ph);
#endif
                taps_set_phase(&g_engine.taps, ph);
                prev_preset = pc.preset;
            }
#if PRESET_ENABLE
            /* Save chord: hold BOTH momentaries ~2 s -> snapshot the current tap
             * pattern + mult into the selected slot. Distinct from the momentary
             * WRITE/RECIRC taps, so it won't fire by accident. */
            if (chord_update(&g_save_chord, wr_act && rc_act,
                             PRESET_SAVE_HOLD_TICKS)) {
                preset_scene_t sc;
                for (int i = 0; i < NUM_TAPS; i++) sc.phase[i] = g_engine.taps.phase[i];
                sc.mult      = g_time_raw01;
                sc.octave    = (uint8_t)prev_octave;
                sc.mute_mask = 0;
                sc.rsvd[0] = sc.rsvd[1] = 0;
                uint8_t blob[PRESET_SLOT_BYTES];
                unsigned n = (unsigned)preset_pack(&sc, blob);
                (void)bsp_preset_flash_write(pc.preset, blob, n);
                g_dbg_panel.saved_blink++;      /* SWD-visible save confirmation */
            }
#endif
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
