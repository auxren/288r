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

/* Input envelope (ISR-updated) for the presence/auto LED comparator (PA11) and
 * the looper's auto-write trigger. */
static volatile float g_env = 0.0f;

/* Looper auto-state (community-documented stock behavior): the red AUTO CONTROL
 * switch (165 bit 4) selects DELAY ("all sounds": continuous write, out of
 * "ready") vs LOOPER (center: sit READY; signal presence auto-starts a WRITE of
 * one cycle, then auto-RECIRCs it; the momentary punches manually). */
enum { LP_READY = 0, LP_WRITE, LP_LOOP };
static uint8_t  g_lp_state = LP_READY;
static uint32_t g_lp_start = 0;

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
        /* input envelope for the AUTO-CONTROL/presence LED (PA11): stock compares
         * an envelope accumulator (0x200000bc) against 0x200000 = 0.25 FS. */
        {
            float ax = (x < 0.0f) ? -x : x;
            g_env += (ax - g_env) * 0.002f;      /* ~5 ms one-pole @96k */
        }
        float chan[NUM_TAPS];
        (void)engine_process_multi(&g_engine, x, t, chan);
        for (unsigned s = 0; s < TDM_SLOTS; ++s)
            out[f * TDM_SLOTS + s] = (s < (unsigned)NUM_TAPS)
                                     ? audio_f_to_out(chan[s]) : 0;
    }
    /* AUTO/presence LED (PA11), stock threshold: envelope > 0x200000/0x800000
     * = 0.25 FS -> LOW (LED on). Per block is plenty (stock: per main-loop). */
    bsp_panel_ind(4, (g_env > 0.25f) ? 0 : 1);
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

    /* Base window = FULL cycle (1 s @96 k). The CYCLE 3-way switch (165 bits
     * 9/10 — proven) scales it LIVE in the panel tick; no boot read needed. */
    float base = (float)SAMPLE_RATE_HZ;
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
            /* Momentaries PROVEN: bits 11/12 active-low, decode gives pressed=1.
             * No idle-capture needed. */
            (void)scan_first; (void)idle_w;
            unsigned wr_act = pc.write_trig, rc_act = pc.recirc_trig;
            g_dbg_panel.sw165 = sw;
            g_dbg_panel.preset = pc.preset;
            g_dbg_panel.octave = pc.octave;
            g_dbg_panel.bank_b = pc.automode;    /* dbg slot: red-switch position */
            g_dbg_panel.write_trig = (uint8_t)wr_act;
            g_dbg_panel.recirc_trig = (uint8_t)rc_act;
            g_dbg_panel.base = g_engine.taps.base_delay;
#if PANEL_TRANSPORT_ENABLE
            /* Stock transport (community-documented + proven switch map):
             *  - WRITE momentary (bit11): punch into WRITE (record) — write LED
             *  - RECIRC momentary (bit12): loop the buffer — ready LED
             *  - RED AUTO CONTROL center: READY/auto — signal presence auto-starts
             *    a WRITE of one cycle, then auto-RECIRCs it
             *  - RED side positions ("all sounds"): plain delay — continuous write */
            int wr_edge = (wr_act && !g_xtrig.prev_w);
            int rc_edge = (rc_act && !g_xtrig.prev_r);
            g_xtrig.prev_w = (uint8_t)wr_act;
            g_xtrig.prev_r = (uint8_t)rc_act;

            if (pc.automode == 1) {
                /* DELAY mode: continuous write; manual punches still honored */
                if (rc_edge)      { engine_recirc(&g_engine); }
                else if (wr_edge || (g_lp_state != LP_LOOP &&
                                     !transport_should_write(&g_engine.xport)))
                                  { engine_write(&g_engine); }
                if (transport_should_write(&g_engine.xport)) {
                    g_lp_state = LP_READY;
                    bsp_panel_ind(1, 0); bsp_panel_ind(3, 1);   /* write LED    */
                } else {
                    g_lp_state = LP_LOOP;
                    bsp_panel_ind(1, 1); bsp_panel_ind(3, 0);   /* ready/loop   */
                }
            } else {
                /* LOOPER/auto mode (red switch center) */
                uint32_t cyc = (uint32_t)g_engine.taps.base_delay;
                switch (g_lp_state) {
                case LP_READY:
                    if (!transport_should_write(&g_engine.xport)) engine_write(&g_engine);
                    if (g_env > 0.25f || wr_edge || pc.automode == 2) {  /* auto / punch / arm */
                        g_lp_start = g_engine.dl.wpos;
                        engine_write(&g_engine);
                        g_lp_state = LP_WRITE;
                    }
                    bsp_panel_ind(1, 1); bsp_panel_ind(3, 0);   /* ready LED */
                    break;
                case LP_WRITE: {
                    uint32_t written = (g_engine.dl.wpos >= g_lp_start)
                                     ? g_engine.dl.wpos - g_lp_start
                                     : g_engine.dl.wpos + DELAY_LEN - g_lp_start;
                    if (written >= cyc || rc_edge) {       /* cycle done / punch-out */
                        engine_recirc(&g_engine);
                        g_lp_state = LP_LOOP;
                    } else if (wr_edge) {                  /* restart the take       */
                        g_lp_start = g_engine.dl.wpos;
                        engine_write(&g_engine);
                    }
                    bsp_panel_ind(1, 0); bsp_panel_ind(3, 1);   /* write LED */
                    break; }
                default: /* LP_LOOP */
                    if (wr_edge) {                         /* punch a new take       */
                        g_lp_start = g_engine.dl.wpos;
                        engine_write(&g_engine);
                        g_lp_state = LP_WRITE;
                    }
                    bsp_panel_ind(1, 1); bsp_panel_ind(3, 0);   /* ready (looping) */
                    break;
                }
            }
            bsp_panel_ind(2, 1);   /* PA7 held high (stock run state) */
#endif
            /* base window = boot base x octave (x1/x2) x cycle (3-way, live) */
            unsigned oc_key = (unsigned)pc.octave | ((unsigned)pc.cycle << 4);
            if (oc_key != prev_octave) {
                float nb = engine_clamp_base(base_boot * panel_octave_factor(&pc)
                                             * panel_cycle_factor(&pc),
                                             DELAY_LEN, time_hi);
                taps_set_base_delay(&g_engine.taps, nb);
                prev_octave = oc_key;
            }
            /* "cal." position forces the evenly-spaced ramp (stock-exact);
             * pre-set position recalls the selected A/B/C slot. */
            unsigned sel_key = pc.cal ? 0xFEu : pc.preset;
            if (sel_key != prev_preset) {
                float ph[NUM_TAPS];
#if PRESET_ENABLE
                if (pc.cal) {
                    for (int i = 0; i < NUM_TAPS; i++) ph[i] = 20.0f * (float)(i + 1);
                } else {
                    /* Recall the selected slot (capture-live presets). Blank/
                     * invalid slot -> the A-ramp default. */
                    preset_scene_t sc;
                    (void)preset_load(bsp_preset_flash_base(), pc.preset, &sc);
                    for (int i = 0; i < NUM_TAPS; i++) ph[i] = sc.phase[i];
                }
#else
                (void)panel_preset_phase(pc.preset, ph);
#endif
                taps_set_phase(&g_engine.taps, ph);
                prev_preset = sel_key;
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
