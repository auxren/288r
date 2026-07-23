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
/* no newlib headers in this freestanding toolchain — memcpy's implementation
 * lives in bsp/freestanding.c; declare it here (__SIZE_TYPE__ = size_t). */
void *memcpy(void *dst, const void *src, __SIZE_TYPE__ n);
#include "bsp/bsp.h"
#include "bsp/board.h"
#include "engine.h"
#include "audio_io.h"
#include "led.h"
#include "panel_ctl.h"
#include "preset_store.h"
#include "chord.h"
#include "pitch_voice.h"
#include "pitch_taps.h"
#include "ks.h"
#include "fast_math.h"
#include "calibration.h"
#include "storage.h"
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

/* PITCH mode (TIME/pitch switch, 165 bit 4, 0=TIME per the owner): the Time-CV
 * drives the crossfaded-tap pitch voice at 1.2 V/oct (Buchla) instead of the
 * delay-time multiplier; the multiplier stays knob-only. Voice sums into ch0. */
#define PITCH_VOICE_ENABLE 1

/* Stock attenuverter control law — OFF until the parked ADC3 channel is proven
 * (observe g_dbg_panel.trim[0] while turning the c.v. knob). */
#define CTRL_ATTENUVERTER_LAW 1   /* PROVEN: owner's knob sweep tracked adc3 7..4085 */
#define PRESET_SAVE_HOLD_BLOCKS 6000u   /* 2 s (measured block clock ~3 kHz) */

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
/* SDRAM split: main delay line + the pitch voice's per-tap echo ring
 * (stock-faithful transposed multitap — see pitch_taps.h). 1.75M + 256k
 * samples = the full 8 MB. Ring depth 2.73 s covers every tap's pitch-mode
 * minimum through x4 octave; only the x10-extend corner clamps. */
#define PT_RING_LEN 262144u                          /* 2^18 samples, 1 MB   */
#define DELAY_LEN  (SDRAM_BYTES / sizeof(float) - PT_RING_LEN)  /* 1,835,008 */
static float delay_buf[DELAY_LEN] __attribute__((section(".sdram")));
static float pt_ring_buf[PT_RING_LEN] __attribute__((section(".sdram")));

/* Hot DSP state in CCM (0x10000000): zero-wait-state core-coupled RAM, saves
 * SRAM-bus contention with the SAI DMA. CCM is CPU-only — safe here because the
 * DMA touches only the SAI buffers (SRAM) and the SDRAM delay memory. The .ccmram
 * section is NOLOAD: main() zeroes [_sccm,_eccm) before any init runs. */
static engine_t g_engine __attribute__((section(".ccmram")));

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

extern volatile uint8_t g_spi_raw[4][3];   /* SPI2 raw: [0]=0x80 [1]=0xA0(CV) [2]=0xC0 [3]=0xE0(knob) */

/* TIME control in [0,1]; written in the superloop, read in the audio ISR. */
static volatile float g_time_raw01 = 0.5f;

/* Input envelope (ISR-updated) for the presence/auto LED comparator (PA11) and
 * the looper's auto-write trigger. */
static volatile float g_env = 0.0f;
/* sens/"signal in" candidate channels: codec ADC slots 1+2 carry knob-scaled
 * copies of the input (analog attenuators — see board.h SENS_IN_SLOT). Both are
 * envelope-followed so one owner knob-sweep over SWD identifies which is sens. */
static volatile float g_sens_env[2] = { 0.0f, 0.0f };
/* red AUTO CONTROL position snapshot for the fast control tick (decoded in the
 * slow panel tick): 1=all-sounds(delay), 0=center(looper), 2=next-sound. */
static volatile uint8_t g_auto_now = 1;
#if SENS_IN_SLOT >= 0 && (SENS_IN_SLOT < 1 || SENS_IN_SLOT > 2)
#error "SENS_IN_SLOT must be 1 or 2 (indexes g_sens_env[SLOT-1])"
#endif

/* pulse-jack edge latches (write/recirc/arm): sampled at BLOCK rate in the
 * ISR tail and latched until the panel tick consumes them — the tick alone
 * sampled tens of ms apart and MISSED millisecond triggers (field report:
 * 281e/251e pulses ignored, envelopes worked; issue #1). */
static volatile uint8_t g_pulse_latch = 0;   /* bit0=write bit1=recirc bit2=arm */
static uint8_t g_pulse_prev = 0;

/* chain-clip indicator (PA0 repurposed, LED_INPUT_CLIP_MODE): block count until
 * which the LED stays lit, + an event counter for SWD verification. */
static volatile uint32_t g_clip_until = 0;
static volatile uint8_t  g_clip_count = 0;

/* SWD-driven DAC-slot solo for slot->slider mapping at the bench: -1 = normal,
 * 0..7 = only that TDM slot carries audio (others muted). Written over SWD
 * (mwb &g_dac_solo N); strip with the rest of the g_dbg scaffolding. */
volatile int8_t g_dac_solo __attribute__((used)) = -1;

/* SWD-triggered live 4051-mux sweep with PEAK-HOLD (signal-in hunt): the boot
 * matrix scan samples each mux column ONCE — useless against audio. Set
 * g_dbg_muxscan=1 over SWD and the superloop sweeps all 8 columns, holding
 * min/max of ~400 ADC3 samples each, into g_dbg_muxpk. Reparks at 0x777777
 * (stock idle) when done. Bench scaffolding — strip with the rest. */
volatile uint8_t  g_dbg_muxscan __attribute__((used)) = 0;
volatile uint16_t g_dbg_muxpk[8][2] __attribute__((used));
static float g_att_filt = 2047.0f;   /* c.v. attenuverter (ADC3 parked ch) */

/* Audio-block clock (ISR-incremented, ~6000/s): the loop-pass "tick" rate varies
 * with superloop load, so anything timed (save-hold, blinks) counts blocks. */
static volatile uint32_t g_blocks = 0;

/* Looper auto-state (community-documented stock behavior): the red AUTO CONTROL
 * switch (165 bit 4) selects DELAY ("all sounds": continuous write, out of
 * "ready") vs LOOPER (center: sit READY; signal presence auto-starts a WRITE of
 * one cycle, then auto-RECIRCs it; the momentary punches manually). */
enum { LP_READY = 0, LP_WRITE, LP_HOLD, LP_LOOP };
static uint8_t  g_lp_state = LP_READY;
static uint32_t g_lp_start = 0;
static uint32_t g_lp_end = 0;    /* store-end: head when the window completed */
static uint8_t  g_lp_armed = 0;   /* looper: env must dip low before the next onset triggers */
static uint8_t  g_lp_prev_auto = 0xFFu; /* red-switch position at the last tick (0xFF = boot) */
static uint8_t  g_lp_prev_store = 0xFFu; /* store beg./end position at the last tick (0xFF = boot) */

#if PITCH_VOICE_ENABLE
static pitch_voice_t g_pv __attribute__((section(".ccmram")));
/* per-tap echo ring: each channel reads the voice at ITS OWN tap delay
 * (transposed multitap, the stock's pitch-mode semantics). Also provides the
 * channel decorrelation the old 0-9 ms micro-delay ring existed for. */
static ptaps_t g_pt;
#endif

/* KARPLUS-STRONG string bank (gesture-entered mode: hold next-sound 2 s;
 * twinkle confirms; READY LED breathes while in the mode; same hold exits).
 * Rings in SRAM (~77 KB). Tap PHASES are the chord; c.v. in transposes at 1.2 V/oct
 * (direct); the multiplier knob is damping/brightness. */
static ks_t g_ks;
static volatile uint8_t g_ks_mode = 0;
/* transport/looper LED writes route through this gate so STRING MODE owns the
 * indicator lamps (breathing READY PWM was being fought). Identifier-level
 * substitution only — call sites keep their exact control flow. */
static void lp_ind(unsigned idx, int level)
{
    if (!g_ks_mode) bsp_panel_ind(idx, level);
}
#if PITCH_VOICE_ENABLE
/* CCM copy of the active AA coefficient band (zero-wait D-bus reads in the
 * ISR vs ART-thrashing flash loads — adversarial-verify blocker fix). The
 * superloop copies on band change; revoke-before-overwrite protocol in
 * ps_set_aa_rows keeps the ISR on the flash tables during the memcpy. */
static float g_aa_ccm[33][16] __attribute__((section(".ccmram")));
static int   g_aa_ccm_band = -1;
/* bench-only: force the pitch ratio over SWD (0 = off) to measure worst-case
 * ISR load without patching a CV. Strip with the other g_dbg scaffolding. */
volatile float g_dbg_ratio_force __attribute__((used)) = 0.0f;
static volatile uint8_t g_pitch_mode = 0;   /* tick-written, ISR-read */
static volatile uint8_t pc_cycle_now = 1;   /* cycle pos for pitch span   */
#endif

/* preset-saved feedback: until this block count, the scan tick sparkles the
 * indicator LEDs pseudo-randomly and the ISR holds off its PA0/PA11 writes. */
static volatile uint32_t g_twinkle_until = 0;
static uint32_t g_twinkle_rng = 0x12345678u;

/* multiplier control-pinning: preset recall pins the SAVED multiplier until the
 * physical knob sweeps through it (storage.h pin_*) — no value jumps. */
static ctrl_pin_t g_mult_pin;

/* switch-pinning for recalled octave/cycle: the preset's values apply until the
 * PHYSICAL switch moves from its recall-time position (then live wins). */
static uint8_t g_sw_pin_on = 0;
static uint8_t g_pin_oct, g_pin_cyc;        /* saved values being applied   */
static uint8_t g_pin_oct_phys, g_pin_cyc_phys; /* physical pos at recall    */

/* end-of-cycle blink: loop-wrap detector state */
static uint32_t g_prev_wpos = 0;
static uint8_t  g_eoc_blink = 0;

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
    uint8_t  lp_state;    /* looper state READY/WRITE/LOOP         */
    uint8_t  xp_mode;     /* transport 1=WRITE 2=RECIRC            */
    uint8_t  env_q;       /* envelope x100                          */
    uint8_t  sens_q[2];   /* codec slot1/slot2 env x1000 (sens/"signal in" ID) */
    uint8_t  clip_q;      /* chain-clip event counter (wraps)      */
    uint8_t  pad_;
    uint16_t isr_pk;      /* max audio-ISR cycles per BLOCK >> 4 (DWT CYCCNT) */
    uint8_t  eoc;         /* eoc blink counter (nonzero = blinking) */
    float    mult;        /* smoothed multiplier [0,1]            */
    float    base;        /* current taps base_delay (samples)    */
};
volatile struct dbg_panel g_dbg_panel __attribute__((used));

/* Audio ISR bridge: TDM frame = TDM_SLOTS int32 (ADC slots 0..3 in, DAC slots 0..7
 * out). Pull the input from AUDIO_IN_SLOT, run the engine, scatter the 8 tap
 * outputs into the 8 DAC slots. audio_in_to_f / audio_f_to_out are the codec word
 * conversions from audio_io.c (host-tested, clamped). */
/* DWT cycle counter (non-intrusive ISR load measurement — the AA verify
 * panel demanded a MEASURED headroom number, not an estimate). */
#define DWT_CTRL_REG   (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYCCNT_REG (*(volatile uint32_t *)0xE0001004u)
#define DEMCR_REG      (*(volatile uint32_t *)0xE000EDFCu)
static volatile uint16_t g_isr_pk = 0;   /* max cycles/block >> 4 */

void bsp_audio_isr(const int32_t *in, int32_t *out, unsigned frames)
{
    const uint32_t cyc0 = DWT_CYCCNT_REG;
    const float t = g_time_raw01;
    int clip = 0;
    for (unsigned f = 0; f < frames; ++f) {
        float x = audio_in_to_f(in[f * TDM_SLOTS + AUDIO_IN_SLOT]);
#if LED_INPUT_CLIP_MODE
        /* INPUT LED (PA0) repurposed: whole-chain clip detector, stage 1 — the
         * input ADC at the 24-bit rail (information already lost upstream of us;
         * the fix is the mixer knob, so tell the player). */
        { float ax0 = (x < 0.0f) ? -x : x; if (ax0 >= CLIP_IN_THRESH) clip = 1; }
#else
        /* INPUT LED (PA0) — decompile-exact: the stock compares each input sample
         * against +0.5 FS in the tap service and drives PA0 LOW (LED ON) above it:
         *   if (r5 + 0x400000 > 0x800000) sub_fe0(0); else sub_102c(0);
         * A per-sample 1-bit envelope PWM: loud input -> more low-time -> brighter.
         * (PA0/1/7/8/11 are DSP-driven indicator outputs, NOT mux addresses — the
         * old panel-scan.md label was wrong.) */
        if (g_blocks >= g_twinkle_until) bsp_panel_strobe(x > 0.5f ? 0 : 1);
#endif
        /* input envelope for the AUTO-CONTROL/presence LED (PA11): stock compares
         * an envelope accumulator (0x200000bc) against 0x200000 = 0.25 FS. */
        {
            float ax = (x < 0.0f) ? -x : x;
            g_env += (ax - g_env) * 0.002f;      /* ~5 ms one-pole @96k */
        }
        /* sens / "signal in" knob channels (codec slots 1+2): same one-pole. */
        {
            float s1 = audio_in_to_f(in[f * TDM_SLOTS + 1]);
            float s2 = audio_in_to_f(in[f * TDM_SLOTS + 2]);
            if (s1 < 0.0f) s1 = -s1;
            if (s2 < 0.0f) s2 = -s2;
            g_sens_env[0] += (s1 - g_sens_env[0]) * 0.002f;
            g_sens_env[1] += (s2 - g_sens_env[1]) * 0.002f;
        }
        float chan[NUM_TAPS];
#if PITCH_VOICE_ENABLE
        /* pitch mode NEVER uses the engine's 8 tap outputs (full wet discards
         * them; the transition sliver's dry leg is a single tap-0 read below)
         * — skip the 8 SDRAM reads that pushed the ISR into DMA overrun (the
         * owner's "glitches": 104-110% of block budget, torn output blocks
         * that the external feedback patch then recirculated). Write/recirc/
         * control still run inside the engine. */
        g_engine.skip_tap_reads = g_pitch_mode ? 1 : 0;
#endif
        if (g_ks_mode) g_engine.skip_tap_reads = 1;   /* KS replaces channels */
        (void)engine_process_multi(&g_engine, x, t, chan);
        if (g_ks_mode) {
            ks_process(&g_ks, x, chan);
        } else {
#if PITCH_VOICE_ENABLE
        if (g_pitch_mode) {
            /* ALWAYS run the voice (pv_process is the only thing that slews
             * g_pv.ratio — gating the call on the ratio deadlocked the voice
             * at unity forever; adversarial-verify blocker #1). Only the MIX is
             * faded by |ratio-1| so pitch mode is transparent at zero CV. */
            float y = pv_process(&g_pv, &g_engine.dl, DL_INTERP_HERMITE);
            float dev = g_pv.ratio - 1.0f; if (dev < 0.0f) dev = -dev;
            /* steep wet slope: full wet by 0.5% ratio deviation (~9 cents).
             * The old 2% band let CV/attenuverter offsets PARK in a dry+shift
             * partial mix whose slow beating read as a broken tremolo (owner:
             * "weird stuff"); passing through the sliver during slews is
             * inaudible, living in it is not. */
            float wet = dev * 200.0f; if (wet > 1.0f) wet = 1.0f;
            /* REPLACE, don't layer (stock: pitch-mode output IS the shifted
             * signal). Dry leg for the unity-transition sliver = ONE tap-0
             * read (the taps sit at min delay in pitch mode; per-channel dry
             * diversity there isn't worth 7 more SDRAM reads of ISR budget). */
            float dry = 0.0f;
            if (wet < 0.999f) {
                uint32_t d_int; float d_frac;
                taps_delay_frac(&g_engine.taps, 0, &d_int, &d_frac);
                if (d_int < 1) { d_int = 1; d_frac = 0.0f; }
                dry = dl_read_frac(&g_engine.dl, d_int, d_frac, DL_INTERP_HERMITE);
            }
            /* TRANSPOSED MULTITAP (stock semantics, owner-requested): the
             * voice goes into its echo ring, and each channel reads it back
             * at that tap's own delay — the TIME-mode echo pattern, pitched.
             * Cycle/octave rescale the pattern exactly as in TIME mode. */
            pt_write(&g_pt, y);
            for (unsigned pi = 0; pi < (unsigned)NUM_TAPS; ++pi) {
                uint32_t d_int; float d_frac;
                taps_delay_frac(&g_engine.taps, pi, &d_int, &d_frac);
                float yi = pt_read(&g_pt, d_int, d_frac);
                chan[pi] = (1.0f - wet) * dry + (PITCH_VOICE_GAIN * wet) * yi;
            }
        }
#endif
        }   /* !g_ks_mode */
#if MASTER_DRY_MODE
        /* SLIDER 0 = DRY OUT (owner norm): slot 4 reaches only the analog
         * master sum (its own slider path is broken), so it carries a hidden
         * compensation signal — dry minus the other seven channels — and the
         * master collapses to ~pure dry. Sliders 1-8 are untouched. Works in
         * both modes (pitch mode: slider 0 = the dry anchor under the
         * transposed echoes). See board.h MASTER_DRY_MODE. */
        {
            float comp = x;
            for (unsigned s2 = 0; s2 < (unsigned)NUM_TAPS; ++s2)
                if (s2 != 4u) comp -= chan[s2];
            chan[4] = comp;
        }
#endif
#if LED_INPUT_CLIP_MODE
        /* clip stage 2 — any tap about to exceed full scale pre-limiter (covers
         * the pitch-voice sum, hot recirc content, and the master-dry comp
         * channel; the soft knee makes it inaudible-ish, which is exactly why
         * it deserves an indicator). */
        for (unsigned s = 0; s < (unsigned)NUM_TAPS; ++s)
            if (chan[s] >= 1.0f || chan[s] <= -1.0f) clip = 1;
#endif
        for (unsigned s = 0; s < TDM_SLOTS; ++s)
            out[f * TDM_SLOTS + s] = (s < (unsigned)NUM_TAPS)
                                     ? audio_f_to_out(chan[s]) : 0;
        if (g_dac_solo >= 0)                       /* bench slot->slider mapping */
            for (unsigned s = 0; s < TDM_SLOTS; ++s)
                if (s != (unsigned)g_dac_solo) out[f * TDM_SLOTS + s] = 0;
    }
    g_blocks++;
    {   /* pulse-jack edge latch at block rate (~3 kHz: catches 1 ms pulses) */
        uint8_t now = (uint8_t)((bsp_pulse_in(0) ? 1u : 0u)
                              | (bsp_pulse_in(1) ? 2u : 0u)
                              | (bsp_pulse_in(2) ? 4u : 0u));
        g_pulse_latch |= (uint8_t)(now & (uint8_t)~g_pulse_prev);
        g_pulse_prev = now;
    }
    {
        uint32_t dt = (DWT_CYCCNT_REG - cyc0) >> 4;
        if (dt > 0xFFFFu) dt = 0xFFFFu;
        if ((uint16_t)dt > g_isr_pk) g_isr_pk = (uint16_t)dt;
    }
#if LED_INPUT_CLIP_MODE
    if (clip) { g_clip_until = g_blocks + CLIP_HOLD_BLOCKS; g_clip_count++; }
    if (g_blocks >= g_twinkle_until)
        bsp_panel_strobe((g_blocks < g_clip_until) ? 0 : 1);
#endif
    /* KS mode indicator: READY LED breathes (~2 s cycle, PWM at block rate) */
    if (g_ks_mode && g_blocks >= g_twinkle_until) {
        uint32_t ph = g_blocks % 6000u;
        uint32_t tri = (ph < 3000u) ? ph : (6000u - ph);      /* 0..3000 */
        uint32_t duty = (tri * 255u) / 3000u;
        bsp_panel_ind(3, (((g_blocks * 97u) & 0xFFu) < duty) ? 0 : 1);
    }
    /* AUTO/presence LED (PA11) — owner spec: illuminate ONLY when incoming
     * audio exceeds the threshold set by the sens. knob (sens channel envelope
     * vs the fixed reference — same comparison that fires the auto-trigger).
     * Until the sens slot is wire-proven, fall back to the stock 0.25 FS law. */
#if SENS_IN_SLOT >= 0
    if (g_blocks >= g_twinkle_until)
        bsp_panel_ind(4, (g_sens_env[SENS_IN_SLOT - 1] > SENS_REF) ? 0 : 1);
#else
    if (g_blocks >= g_twinkle_until) bsp_panel_ind(4, (g_env > 0.25f) ? 0 : 1);
#endif
}

int main(void)
{
    /* zero the NOLOAD CCM section before anything touches it */
    extern uint32_t _sccm, _eccm;
    for (uint32_t *p = &_sccm; p < &_eccm; ++p) *p = 0u;

    /* DWT cycle counter on (ISR load telemetry) */
    DEMCR_REG |= (1u << 24);
    DWT_CYCCNT_REG = 0u;
    DWT_CTRL_REG |= 1u;

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
    /* extend factor is latched POST-park in the tick (matrix-connected DIP) */
    static float   g_extend_factor  = 1.0f;
    static uint8_t g_extend_latched = 0;
    /* TIME MULTIPLIER range from the panel legend (1.0 at noon). The x1/x2 octave
     * switch will scale this once wired. */
    /* slew 0.001/sample = tau ~10 ms @96k (fc ~15 Hz): buries the discrete
     * control ticks (owner: 0.15 zippered on knob/CV/signal moves — tau was
     * 67 us, so the read head SNAPPED tick to tick at ~10x read velocity)
     * while still tracking chorus/flanger-rate CV. */
    engine_init(&g_engine, delay_buf, DELAY_LEN, base, time_lo, time_hi, /*slew*/ 0.001f);

    /* Fidelity from config DIP SW1 sw3/sw4 (PD11/PD12): 24-bit = full precision
     * (clean), 12/8/4-bit = vintage bit-crush. Owner-confirmed; all-off = 24-bit. */
    /* Resolution/bandwidth DIPs are MATRIX-CONNECTED like the extend strap
     * (issue #12): boot-time reads see a disconnected pin. Safe defaults here;
     * the real values latch in the panel tick after the first 595 park. */
    g_engine.vintage_bits = 0;
    engine_set_bandwidth(&g_engine, (float)SAMPLE_RATE_HZ, 0.0f);

    bsp_spi2_adc_init();      /* control-surface ADC (multiplier knob + CV) */

    /* Start SAI/MCLK BEFORE codec config: the CS42888 control port needs a valid
     * MCLK to respond on I2C. Engine already init'd, so the ISR is safe to run. */
    bsp_audio_init();
    bsp_audio_start();
    /* CONCERT-GRADE: codec init is verified+retried inside; on total failure
     * flash all indicators rapidly ~3 s (dry still passes on the analog path,
     * but the player must KNOW) — then continue so the panel stays alive. */
    if (bsp_codec_init() != 0) {
        for (int fl = 0; fl < 30; ++fl) {
            for (unsigned li = 0; li < 5u; ++li) bsp_panel_ind(li, fl & 1);
            for (volatile int d = 0; d < 800000; ++d) { }
        }
    }

#if PANEL_LED_ENABLE
    bsp_panel_init();          /* [BENCH] enable only after the 595 bits are labelled */
    led_clear(&g_leds);
#endif

#if PANEL_SCAN_ENABLE
    bsp_panel_switches_init();          /* 165 input pins only (no 595 output) */
    unsigned prev_octave = 1, prev_preset = 0;
    /* Tick cadence is BLOCK-CLOCK driven, not pass-counted: pitch mode's
     * splice searches stretch superloop passes so far that a pass-counted
     * tick ran only every ~0.4 s — physical momentary flicks fell BETWEEN
     * panel reads (issue #3: 'write/recirc don't initiate the looper in
     * pitch mode'; measured live via pulse-latch consumption lag). Blocks
     * tick at ~3 kHz regardless of superloop load: fast tick ~1 ms, panel
     * tick ~5 ms in EVERY mode. */
    uint32_t last_fast = 0, last_slow = 0;
    int scan_first = 1;                 /* first pass captures momentary idle levels */
    unsigned idle_w = 0;                /* auto-polarity: pressed = level != idle    */
#if PANEL_TRANSPORT_ENABLE
    transport_trig_init(&g_xtrig);
#endif
#if PRESET_ENABLE
    chord_init(&g_save_chord);
    bsp_preset_flash_migrate();  /* one-time: carry sector-3 saves to sector 7 */
    prev_preset = 0xFFu;    /* force a load of the selected slot on the first scan */
#endif
#endif
#if PANEL_MATRIX_ENABLE
    /* Stock-faithful panel bring-up: mux/control lines to the stock RUNTIME state
     * (PA1/7/8/11 high — live ODR 0x996), row inputs ready, ADC3 on for the
     * per-column analog scan, and the 595 sweep runs from here on. */
    bsp_panel_mux_boot_state();
    bsp_panel_matrix_init();
    {   /* stock-exact: the DIP/preset matrix is read ONCE at boot, then the 595
         * parks at 0x777777 forever (proven from the decompile: sub_3488 has one
         * call site). The parked address routes the c.v. ATTENUVERTER to ADC3. */
        uint16_t dip[3];
        bsp_panel_matrix_scan(dip);
        for (int i = 0; i < 3; i++) g_dbg_panel.dip[i] = dip[i];
    }
    bsp_mult_init();               /* ADC3 ch6/PF8 (stock-matching config) */
    bsp_panel_match_stock_idle();  /* pull-ups off, USART1 pins driven, etc. */
#endif
#if PITCH_VOICE_ENABLE
    pv_init(&g_pv, PITCH_WINDOW_SAMPLES, PITCH_BASE_SAMPLES, PITCH_RATIO_SLEW);
    pt_init(&g_pt, pt_ring_buf, PT_RING_LEN);
    pt_clear(&g_pt);                       /* SDRAM: not zeroed by startup */
    ks_init(&g_ks, 0.995f, 0.5f);          /* string bank (gesture-entered) */
#endif

    float mult_filt = 0.5f;
    pin_free(&g_mult_pin, 0.5f);
    for (;;) {
#if PITCH_VOICE_ENABLE
        /* de-glitch service: correlation-aligned splices for the pitch voice.
         * Cheap check per pass; the actual search (~250k MACs) runs only when a
         * tap wrap is imminent (every ~0.5 s in pitch mode) — superloop-only,
         * never in the ISR. Measured: purity 0.98..1.00 vs 0.77..0.98 plain. */
        if (g_pitch_mode) ps_service(&g_pv.ps, &g_engine.dl);
#endif
#if PANEL_SCAN_ENABLE
        /* Panel + control tick (~every 64 passes, ~10 ms). The SPI2 control-ADC
         * probe lives HERE, not every pass: hammering it at loop rate parked the
         * ADC's chip-select low ~100% duty (stock probes at ~6 Hz, CS idles HIGH
         * — live dump) and that DC-loads the control-surface analog. ~100 Hz is
         * still 15x the stock rate; the one-pole + audio-rate tap slew keep the
         * multiplier smooth. */
        /* FAST control path (every 4th pass ~1.5 kHz): the knob/CV must update
         * quickly or the delay time steps (zipper — release-test 2.1). The CV is
         * BIPOLAR around mid-scale (the panel's -/+ attenuverter): signed offset. */
        uint32_t now_blocks = g_blocks;
        if ((uint32_t)(now_blocks - last_fast) >= 3u) {
            last_fast = now_blocks;
            bsp_spi2_probe();     /* all 4 MCP3204 channels; CV=idx1, knob=idx3 */
            uint32_t cv   = ((g_spi_raw[1][1] & 0x0F) << 8) | g_spi_raw[1][2];
            uint32_t knob_raw = ((g_spi_raw[3][1] & 0x0F) << 8) | g_spi_raw[3][2];
            /* panel-legend knob curve (owner mark-by-mark cal 2026-07-18): the
             * printed 0.4/0.6/0.8/1.0/1.2/1.4/1.6 marks read exactly true.
             * (Replaces the KNOB_ADC_LO/HI span stretch — the old 620 floor no
             * longer matches the hardware and ate the bottom fifth of travel.) */
            uint32_t knob = (uint32_t)(cal_knob01((uint16_t)knob_raw) * 4095.0f);
            int32_t  raw;
            /* attenuverter filter runs in BOTH modes (adversarial-verify find:
             * updating it only in the TIME branch left the Pitch-CV dead until
             * TIME mode had been visited once after boot). */
            {
                uint16_t a3 = bsp_mult_read();
                g_att_filt += ((float)a3 - g_att_filt) * 0.05f;
                g_dbg_panel.trim[0] = (uint16_t)g_att_filt;
            }
#if PITCH_VOICE_ENABLE
            if (g_pitch_mode) {
                /* STOCK pitch mode (decompile-verified, adversarially checked):
                 * delay pinned to range minimum (knob no longer scales time);
                 * knob = pitch-down depth, span -1.07 st FULL / -4.75 st SHORT;
                 * the CV adds bipolar 1.2 V/oct through the SAME attenuverter. */
                raw = 0;                                   /* taps -> range min  */
                /* depth = RAW pot travel, NOT the panel-legend curve: the
                 * legend (0.4x..1.6x) is a TIME-multiplier scale, meaningless
                 * for pitch — and its piecewise-anchor slope kinks made the
                 * pitch response audibly uneven across the rotation (owner
                 * report). The raw pot is physically smooth. */
                float d01  = (float)knob_raw * (1.0f / 4094.0f);
                if (d01 > 1.0f) d01 = 1.0f;
                /* unity SNAP: bottom ~2% of travel = exactly no shift. Measured
                 * on the owner's recording: knob "at CCW" parked the shifter at
                 * -44 cents (a few % of residual travel), which beats against
                 * the dry signal — audible AM. Snapped, ratio converges into
                 * the PS_UNITY_EPS clean-bypass window. */
                if (d01 < 0.02f) d01 = 0.0f;
                float span = (pc_cycle_now == 2) ? 0.24f : 0.06f;
                float att  = (g_att_filt - 2047.0f) * (1.0f / 2047.0f);
                if (att > -0.05f && att < 0.05f) att = 0.0f;
                float volts = (float)cv * PITCH_CV_VOLTS_PER_CODE * att;
                float ratio = (1.0f - d01 * span) * fm_exp2f(volts * (1.0f/1.2f));
                if (g_dbg_ratio_force > 0.0f) ratio = g_dbg_ratio_force;
                pv_set_ratio(&g_pv, ratio);
                /* publish the AA band's coefficients to CCM on change (the
                 * ISR uses flash rows until the copy is republished) */
                int req = g_pv.ps.aaband_req;
                if (req >= 0 && req != g_aa_ccm_band) {
                    ps_set_aa_rows(&g_pv.ps, -1, 0);
                    memcpy(g_aa_ccm, ps_aa_flash_rows(req), sizeof g_aa_ccm);
                    ps_set_aa_rows(&g_pv.ps, req, (const float (*)[16])g_aa_ccm);
                    g_aa_ccm_band = req;
                }
            } else
#endif
            /* PROVEN-STABLE law: additive knob+cv (the state the owner verified).
             * The stock attenuverter law (mult = knob + cv*att from the parked
             * ADC3 channel) is gated OFF until that channel is PROVEN live —
             * we only OBSERVE it into dbg below. */
            {
#if CTRL_ATTENUVERTER_LAW
                float att = (g_att_filt - 2047.0f) * (1.0f / 2047.0f);  /* stock-exact scale */
                if (att > -0.05f && att < 0.05f) att = 0.0f;
                raw = (int32_t)knob + (int32_t)((float)cv * att);
#else
                raw = (int32_t)knob + (int32_t)cv;
#endif
            }
            if (raw < 0) raw = 0; else if (raw > 4095) raw = 4095;
            mult_filt += ((float)raw * (1.0f / 4095.0f) - mult_filt) * 0.04f;
            float t01 = pin_update(&g_mult_pin, mult_filt);
            /* ENVELOPE -> DELAY-TIME (the dead signal-in jack's intended
             * feature, via the working sens channel): in "all sounds" mode the
             * sens knob is the analog DEPTH control — its channel's envelope
             * adds to the multiplier. Looper modes keep sens as the capture
             * threshold; pitch mode keeps the knob on depth duty. Applied
             * AFTER pin_update so preset knob-catch pinning is unaffected
             * (verify-panel lesson from the first signal-in attempt). */
#if PITCH_VOICE_ENABLE
            if (!g_pitch_mode && g_auto_now == 1)
#else
            if (g_auto_now == 1)
#endif
            {
                t01 += g_sens_env[0] * ENV_TIME_DEPTH;
                if (t01 > 1.0f) t01 = 1.0f;
            }
            g_time_raw01 = t01;
            /* KS tuning (owner-designed): the tap PHASES are the chord's
             * intervals; c.v. in transposes the whole chord at 1.2 V/oct —
             * ALWAYS, both TIME/pitch positions, DIRECT (no attenuverter: any
             * attenuation would break V/oct tracking). The multiplier knob is
             * DAMPING/BRIGHTNESS: CW = brighter / longer ring. */
            if (g_ks_mode) {
                float volts = (float)cv * PITCH_CV_VOLTS_PER_CODE;
                float trans = fm_exp2f(-volts * (1.0f / 1.2f));  /* CV up = pitch up */
                for (int ki = 0; ki < KS_STRINGS; ki++) {
                    float base = KS_BASE_PERIOD *
                                 (g_engine.taps.phase[ki] / PHASE_FULLSCALE);
                    ks_set_period(&g_ks, ki, base * trans);
                }
                float k01 = (float)knob_raw * (1.0f / 4094.0f);
                if (k01 > 1.0f) k01 = 1.0f;
                g_ks.damp = 0.05f + 0.90f * (1.0f - k01);
            }
            g_dbg_panel.spi_cv = (uint16_t)cv;
            g_dbg_panel.spi_knob = (uint16_t)knob;
            g_dbg_panel.mult = mult_filt;
        }
        if (g_dbg_muxscan) {
            g_dbg_muxscan = 0;
            for (unsigned k = 0; k < 8u; k++) {
                bsp_panel_out(0x111111u * k);
                for (volatile int w = 0; w < 20000; w++) { }   /* settle */
                uint16_t lo = 4095, hi = 0;
                for (int n = 0; n < 400; n++) {
                    uint16_t v = bsp_mult_read();
                    if (v < lo) lo = v;
                    if (v > hi) hi = v;
                }
                g_dbg_muxpk[k][0] = lo; g_dbg_muxpk[k][1] = hi;
            }
            bsp_panel_out(0x777777u);              /* repark (stock idle) */
        }
        if ((uint32_t)(now_blocks - last_slow) >= 15u) {
            last_slow = now_blocks;
            panel_ctl_t pc;
            uint16_t sw = bsp_panel_switches_read();
            panel_decode(sw, &pc);
            /* Momentaries PROVEN: bits 11/12 active-low, decode gives pressed=1.
             * No idle-capture needed. */
            (void)scan_first; (void)idle_w;
            /* consume ISR-latched pulse edges (issue #1) + live levels for
             * gate-style use; clear-after-read */
            uint8_t pl = g_pulse_latch; g_pulse_latch = 0;
            unsigned wr_act = pc.write_trig | (unsigned)bsp_pulse_in(0) | ((pl >> 0) & 1u);
            unsigned rc_act = pc.recirc_trig | (unsigned)bsp_pulse_in(1) | ((pl >> 1) & 1u);
            unsigned arm_in = (unsigned)bsp_pulse_in(2) | ((pl >> 2) & 1u);
            g_dbg_panel.sw165 = sw;
            g_dbg_panel.preset = pc.preset;
            g_dbg_panel.octave = pc.octave;
            g_dbg_panel.bank_b = pc.automode;    /* dbg slot: red-switch position */
            g_auto_now = pc.automode;            /* fast-tick snapshot (env->time gate) */

            /* KS gesture: HOLD next-sound (red momentary) ~2 s -> toggle the
             * string bank; twinkle confirms; a short flick keeps its normal
             * next-sound arm meaning. Same hold exits. */
            {
                static uint32_t ks_hold_t0 = 0;
                static uint8_t  ks_prev = 0, ks_fired = 0;
                uint8_t held = (pc.automode == 2);
                if (held && !ks_prev) { ks_hold_t0 = g_blocks; ks_fired = 0; }
                if (held && !ks_fired &&
                    (g_blocks - ks_hold_t0) >= PRESET_SAVE_HOLD_BLOCKS) {
                    g_ks_mode = !g_ks_mode;
                    ks_fired = 1;
                    g_twinkle_until = g_blocks + 3000u;   /* ~1 s confirm */
                }
                ks_prev = held;
            }
#if PITCH_VOICE_ENABLE
            g_pitch_mode = pc.time_pitch;        /* bit4: 1 = pitch mode */
            pc_cycle_now = pc.cycle;
#endif
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

            /* Red-switch MOVEMENT resets the looper state machine (#13). The
             * stock reset gesture — toggle to "all sounds" and back — depends
             * on it: all-sounds entry releases the loop back to continuous
             * write, and looper entry sits READY *armed*, so a signal already
             * above the sens threshold captures immediately instead of
             * waiting for a fresh silence->onset. Boot latch takes no action
             * (0xFF), and a recalled preset can't fake a movement: this reads
             * the physical switch only. */
            if (pc.automode != g_lp_prev_auto) {
                if (g_lp_prev_auto != 0xFFu) {
                    g_lp_state = LP_READY;
                    g_lp_armed = (pc.automode != 1u);
                }
                g_lp_prev_auto = pc.automode;
            }
            /* The store beg./end selector is the same story (#16): it used to
             * be consulted only at the instant a write pass completed, so
             * flipping it mid-loop did nothing — Mixcatonic's documented stock
             * reset gesture (store beg. -> store end) needs the movement
             * itself to reset. Same policy as the red switch: physical
             * movement -> READY, armed unless we're in plain delay. */
            if (pc.store_end_mode != g_lp_prev_store) {
                if (g_lp_prev_store != 0xFFu) {
                    g_lp_state = LP_READY;
                    g_lp_armed = (pc.automode != 1u);
                }
                g_lp_prev_store = pc.store_end_mode;
            }

            if (pc.automode == 1) {
                /* DELAY mode: continuous write; manual punches still honored */
                if (rc_edge)      { engine_recirc_window(&g_engine,
                                        (uint32_t)g_engine.taps.base_delay); }
                else if (wr_edge || (g_lp_state != LP_LOOP &&
                                     !transport_should_write(&g_engine.xport)))
                                  { engine_write(&g_engine); }
                if (transport_should_write(&g_engine.xport)) {
                    g_lp_state = LP_READY;
                    /* WRITE: write LED on, recirc + ready off (PA1/PA7/PA8 map
                     * pin-forced + owner-named) */
                    lp_ind(1, 0); lp_ind(2, 1); lp_ind(3, 1);
                } else {
                    g_lp_state = LP_LOOP;
                    /* RECIRC: recirc LED on; ready blips at each loop wrap */
                    lp_ind(1, 1); lp_ind(2, 0);
                }
            } else {
                /* LOOPER/auto mode (red switch center) */
                uint32_t cyc = (uint32_t)g_engine.taps.base_delay;
                /* Shared auto-trigger law, "next sound" semantics: arm when the
                 * input dips below the arm threshold, fire on the next onset —
                 * a signal present when a state is entered can't trigger until
                 * it stops and restarts. Used by READY (first capture) AND by
                 * LOOP/HOLD (auto re-arm, #10). sens knob = threshold by
                 * analog gain (board.h); knob at zero = auto-trigger off. */
                int lp_auto_fire;
#if SENS_IN_SLOT >= 0
                {
                    float sens = g_sens_env[SENS_IN_SLOT - 1];
                    if (sens < SENS_REF * SENS_ARM_FRAC) g_lp_armed = 1;
                    lp_auto_fire = (g_lp_armed && sens > SENS_REF) || arm_in;
                }
#else
                if (g_env < 0.10f) g_lp_armed = 1;
                lp_auto_fire = (g_lp_armed && g_env > 0.25f) || arm_in;
#endif
                switch (g_lp_state) {
                case LP_READY:
                    if (!transport_should_write(&g_engine.xport)) engine_write(&g_engine);
                    if (lp_auto_fire || wr_edge || pc.automode == 2) {
                        g_lp_start = g_engine.dl.wpos;
                        engine_write(&g_engine);
                        g_lp_state = LP_WRITE;
                        g_lp_armed = 0;
                    }
                    lp_ind(1, 1); lp_ind(2, 1); lp_ind(3, 0); /* READY LED (PA8) */
                    break;
                case LP_WRITE: {
                    uint32_t written = (g_engine.dl.wpos >= g_lp_start)
                                     ? g_engine.dl.wpos - g_lp_start
                                     : g_engine.dl.wpos + DELAY_LEN - g_lp_start;
                    if (rc_edge) {                         /* manual punch-out      */
                        engine_recirc_between(&g_engine, g_lp_start);
                        g_lp_state = LP_LOOP;
                    } else if (written >= cyc) {
                        if (!pc.store_end_mode) {          /* 'store beg.': auto-loop */
                            engine_recirc_window(&g_engine, cyc);
                            g_lp_state = LP_LOOP;
                        } else {                           /* 'store end': hold      */
                            g_lp_end = g_engine.dl.wpos;
                            g_lp_state = LP_HOLD;          /* delay keeps running    */
                        }
                    } else if (wr_edge) {                  /* restart the take       */
                        g_lp_start = g_engine.dl.wpos;
                        engine_write(&g_engine);
                    }
                    lp_ind(1, 0); lp_ind(2, 1); lp_ind(3, 1); /* write LED */
                    break; }
                case LP_HOLD:  /* stock mode 5: window stored, delay keeps running */
                    if (!transport_should_write(&g_engine.xport)) engine_write(&g_engine);
                    if (rc_edge) {                         /* recall the saved window */
                        engine_recirc_span(&g_engine, g_lp_start, g_lp_end);
                        g_lp_state = LP_LOOP;
                    } else if (wr_edge || lp_auto_fire) {  /* new take               */
                        g_lp_start = g_engine.dl.wpos;
                        engine_write(&g_engine);
                        g_lp_state = LP_WRITE;
                        g_lp_armed = 0;
                    }
                    /* stored-and-waiting: write + ready LEDs together */
                    lp_ind(1, 0); lp_ind(2, 1); lp_ind(3, 0);
                    break;
                default: /* LP_LOOP */
                    /* AUTO RE-ARM (#10/#16): stock evidence — in the batchas
                     * 288v video (via Mixcatonic) auto control cycles
                     * write/recirc continuously with the input, so a playing
                     * loop must re-trigger on the next onset, not hold
                     * forever. Same silence->onset law as READY: the sound
                     * that triggered THIS take can't retrigger until the
                     * input dips below the arm threshold. */
                    if (wr_edge || lp_auto_fire) {         /* punch a new take       */
                        g_lp_start = g_engine.dl.wpos;
                        engine_write(&g_engine);
                        g_lp_state = LP_WRITE;
                        g_lp_armed = 0;
                    }
                    lp_ind(1, 1); lp_ind(2, 0);   /* recirc LED (looping) */
                    break;
                }
            }
#endif
            /* END-OF-CYCLE indicator: in recirc the head snaps back at the loop
             * boundary — blip the end-of-cycle outputs (PA7/PA8) at each wrap,
             * the stock's loop-rate pulse/LED behavior. */
            if (!transport_should_write(&g_engine.xport)) {
                if (g_engine.dl.wpos < g_prev_wpos) g_eoc_blink = 2;
            }
            g_prev_wpos = g_engine.dl.wpos;
            if (g_eoc_blink) { lp_ind(3, 0); g_eoc_blink--; }
            else if (!transport_should_write(&g_engine.xport) && g_lp_state == LP_LOOP) {
                lp_ind(3, 1);
            }
            g_dbg_panel.lp_state = g_lp_state;
            g_dbg_panel.xp_mode = (uint8_t)g_engine.xport.mode;
            { float e100 = g_env * 100.0f; g_dbg_panel.env_q = (e100 > 255.0f) ? 255 : (uint8_t)e100; }
            g_dbg_panel.eoc = g_eoc_blink;
            for (int i = 0; i < 2; ++i) {          /* sens/"signal in" slot ID */
                float e = g_sens_env[i] * 1000.0f;
                g_dbg_panel.sens_q[i] = (e > 255.0f) ? 255 : (uint8_t)e;
            }
            g_dbg_panel.clip_q = g_clip_count;
            g_dbg_panel.isr_pk = g_isr_pk;   /* read+reset over SWD as needed */
            if (g_blocks < g_twinkle_until) {    /* saved! — random sparkle, 1 s */
                g_twinkle_rng ^= g_twinkle_rng << 13;   /* xorshift32 */
                g_twinkle_rng ^= g_twinkle_rng >> 17;
                g_twinkle_rng ^= g_twinkle_rng << 5;
                for (unsigned i = 0; i < 5u; i++)
                    bsp_panel_ind(i, (int)((g_twinkle_rng >> i) & 1u));
            }
            /* base window = boot base x octave x cycle. Recalled presets pin
             * octave/cycle until the physical switch MOVES from its recall-time
             * position. */
            if (g_sw_pin_on &&
                (pc.octave != g_pin_oct_phys || pc.cycle != g_pin_cyc_phys))
                g_sw_pin_on = 0;                        /* touched -> live wins */
            panel_ctl_t eff = pc;
            if (g_sw_pin_on) { eff.octave = g_pin_oct; eff.cycle = g_pin_cyc; }
            unsigned oc_key = (unsigned)eff.octave | ((unsigned)eff.cycle << 4);
            if (oc_key != prev_octave) {
                float nb = engine_clamp_base(base_boot * g_extend_factor
                                             * panel_octave_factor(&eff)
                                             * panel_cycle_factor(&eff),
                                             DELAY_LEN, time_hi);
                taps_set_base_delay(&g_engine.taps, nb);
                prev_octave = oc_key;
            }
            /* The rear DIPs sit BEHIND the scanned matrix: before the first
             * 595 park they are electrically disconnected from their pins, so
             * the boot-time strap read always saw "open" (x4 never engaged —
             * found by SWD pull-up probing pre/post park). Latch the extend
             * strap ONCE here, after the panel is parked, and force the base
             * rescale through the same path the octave switch uses. */
            if (!g_extend_latched) {
                g_extend_latched = 1;
                g_extend_factor = bsp_sw_delay_extend() ? DELAY_EXTEND_FACTOR : 1.0f;
                if (g_extend_factor != 1.0f) prev_octave = 0xFFu;  /* force rescale */
                /* issue #12: the other rear DIPs share the matrix topology */
                unsigned depth = bsp_resolution_bits();
                g_engine.vintage_bits = (depth >= 24u) ? 0 : (int)depth;
                engine_set_bandwidth(&g_engine, (float)SAMPLE_RATE_HZ,
                                     bsp_sw_bandwidth_limit() ? BANDWIDTH_LIMIT_HZ : 0.0f);
            }
            /* "cal." position forces the evenly-spaced ramp (stock-exact);
             * pre-set position recalls the selected A/B/C slot. */
            unsigned sel_key = pc.cal ? 0xFEu : pc.preset;
            if (sel_key != prev_preset) {
                float ph[NUM_TAPS];
#if PRESET_ENABLE
                if (pc.cal) {
                    for (int i = 0; i < NUM_TAPS; i++) ph[i] = 20.0f * (float)(i + 1);
                    /* cal. = LIVE panel control by definition: release the
                     * preset pins so the knob/switches answer immediately
                     * (field report: knob stayed pinned into cal., issue #2) */
                    pin_free(&g_mult_pin, mult_filt);
                    g_sw_pin_on = 0;
                } else {
                    /* Recall the selected slot (capture-live presets). Blank/
                     * invalid slot -> the A-ramp default. */
                    preset_scene_t sc;
                    if (preset_load(bsp_preset_flash_base(), pc.preset, &sc)) {
                        pin_recall(&g_mult_pin, sc.mult);   /* knob pinned to saved */
                        g_sw_pin_on = 1;                    /* switches pinned too  */
                        g_pin_oct = sc.octave;  g_pin_cyc = sc.cycle;
                        g_pin_oct_phys = pc.octave; g_pin_cyc_phys = pc.cycle;
                        prev_octave = 0xFFu;                /* force base recompute */
                    }
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
            static uint32_t save_press_at = 0; static uint8_t save_latched = 0;
            if (!wr_act) { save_press_at = 0; save_latched = 0; }
            else if (!save_press_at) save_press_at = g_blocks ? g_blocks : 1u;
            if (wr_act && !save_latched && save_press_at &&
                (g_blocks - save_press_at) > PRESET_SAVE_HOLD_BLOCKS) {
                save_latched = 1;
                preset_scene_t sc;
                for (int i = 0; i < NUM_TAPS; i++) sc.phase[i] = g_engine.taps.phase[i];
                sc.mult      = g_time_raw01;
                sc.octave    = pc.octave;
                sc.mute_mask = 0;
                sc.cycle     = pc.cycle;
                sc.rsvd      = 0;
                uint8_t blob[PRESET_SLOT_BYTES];
                unsigned n = (unsigned)preset_pack(&sc, blob);
                (void)bsp_preset_flash_write(pc.preset, blob, n);
                g_dbg_panel.saved_blink++;      /* SWD-visible save confirmation */
                g_twinkle_until = g_blocks + 3000u;   /* 1 s sparkle          */
                g_twinkle_rng ^= g_blocks;            /* reseed per save       */
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
