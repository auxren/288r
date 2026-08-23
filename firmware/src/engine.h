/* engine.h — 288r signal engine: wires delay_line + taps + time_control +
 * transport + mixer into the per-sample flow (faithful clone, fixed-rate).
 *
 * Per sample:
 *   mult   = time control (slewed)            -> taps_update
 *   WRITE  : dl_write(vintage?(quantize):in)   RECIRC: dl_advance_loop(window)
 *   tap i  = interpolated read at taps_delay(i) (loop-aware in RECIRC)
 *   out    = mixer sum(taps, gains, phase) + auto correction
 *
 * Host-testable: pass ordinary RAM as the delay buffer. On hardware the buffer
 * lives in external SDRAM and engine_process() runs from the SAI/DMA block loop.
 */
#ifndef ENGINE_H
#define ENGINE_H

#include "delay_line.h"
#include "taps.h"
#include "time_control.h"
#include "transport.h"
#include "mixer.h"
#include "bwlimit.h"

/* Loop-seam crossfade length in samples (~10 ms @96 k): applied once at every
 * loop capture (see dl_loop_splice). */
#define LOOP_SPLICE_FADE 960u

/* TRANSPORT DECLICK (field #27/#28/#29/#30/#31 - five reports, one family).
 *
 * Every transport change moves the read positions discontinuously: entering
 * RECIRC teleports the head (dl.wpos = loop_start) and window-maps the reads,
 * leaving it drops that mapping. All 8 taps therefore jump to unrelated content
 * in a single sample - "a click that propagates through the taps".
 *
 * DECLICK_FADE crossfades the tap outputs across the jump; the outgoing side is
 * the held pre-switch value rather than a second set of reads, so the cost is
 * two multiplies per tap for the duration and the ISR budget is untouched (a
 * real dual-read crossfade would double the tap SDRAM loads exactly when the
 * looper is already busy).
 *
 * WR_SEAM_FADE covers the other half: when writing RESUMES it lands mid-buffer
 * on old content, so without a blend the step is recorded into the material and
 * heard on every later pass (field #28: "clicks that are recorded into the next
 * loop"). It reads as a punch-in crossfade, not as a fade.
 *
 * Both lengths were swept against the measured discontinuity (test_declick):
 * the transition residual falls as 1/DECLICK_FADE until it reaches the test
 * signal's own slope, and the write-resume residual bottoms out at ~576. Longer
 * buys nothing; shorter is audibly an edge. */
/* 10 ms @96k: output crossfade across a transport transition.
 *
 * ADAPTIVE. The fade is clamped to half the interval since the PREVIOUS
 * transition, because the pulse jacks (and, soon, clocked mode) can retrigger
 * the transport faster than a fixed fade can finish. With a fixed 10 ms fade a
 * 100 Hz pulse train leaves the engine permanently mid-crossfade: the outgoing
 * hold never decays, and the tap outputs degrade into a rolling smear instead
 * of clean transitions. Shortening keeps the step covered and stays responsive;
 * DECLICK_MIN is the floor below which a ramp stops being worth anything. */
#ifndef DECLICK_FADE
#define DECLICK_FADE   960u
#endif
#ifndef DECLICK_MIN
#define DECLICK_MIN     64u    /* ~0.67 ms @96k */
#endif
/* 6 ms @96k: buffer blend where writing resumes. */
#ifndef WR_SEAM_FADE
#define WR_SEAM_FADE   576u
#endif

typedef struct {
    delay_line_t dl;
    taps_t       taps;
    time_ctrl_t  time;
    transport_t  xport;
    mixer_t      mix;
    bwlimit_t    bw;               /* config DIP sw2 bandwidth limit (bypass = off) */
    dl_interp_t  interp;
    float        in_gain;
    float        auto_correction;  /* AUTO CONTROL term (placeholder; calibrate) */
    int          vintage_bits;     /* 0 = full precision, else e.g. 12 (vintage)  */
    int          skip_tap_reads;   /* 1 = run control+write/recirc but skip the 8
                                      tap reads (chan[]=0, return 0). Set by the
                                      pitch mode at full wet, where the crossfade
                                      multiplies the taps by zero anyway — the 8
                                      SDRAM reads were ~30% of the ISR budget and
                                      pushed pitch mode into DMA overrun (the
                                      owner's "glitches"). */
    uint32_t     dith;             /* TPDF dither PRNG state (vintage modes)      */
    int          varispeed;        /* looper tape-motor (#9): in RECIRC the head
                                      advances at mult_ref/mult per sample, so a
                                      playing loop repitches with the multiplier
                                      like the stock's moving sample clock. Off
                                      (0) = classic fixed-rate loop playback.   */
    float        lp_mult_ref;      /* multiplier at loop capture (rate = ref/mult) */
    float        lp_phase;         /* fractional part of the recirc head, [0,1)    */
    float        lp_rate;          /* last applied head rate (telemetry/debug)     */
    uint32_t     declick_n;        /* transport declick: samples remaining      */
    uint32_t     declick_len;      /* length of the fade in flight (adaptive)   */
    uint32_t     declick_since;    /* samples since the last transition         */
    float        declick_hold[NUM_TAPS];  /* fade START value, captured at the switch */
    float        declick_last[NUM_TAPS];  /* last value actually EMITTED, every sample */
    uint32_t     wr_seam_n;        /* write-resume blend: samples remaining      */
    uint32_t     wr_seam_len;      /* length of that blend (adaptive)            */
    float        time_fm;          /* per-sample delay-time FM term (signal-in slot
                                      2 x depth, ISR-written): tap distances scale
                                      by (1 + time_fm) AFTER the control slews —
                                      injecting before them would low-pass the
                                      modulation to nothing (~16 Hz). Clamped
                                      +/-0.25 at application. 0 = off.          */
    float        fm_off[NUM_TAPS]; /* per-tap APPLIED FM offset (samples): slew-
                                      limited to +/-FM_MAX_STEP per sample so
                                      deep taps at audio-rate depth glide
                                      (Doppler) instead of teleporting (alias
                                      shred — heard on first bench listen).    */
    int          od_active;        /* OVERDUB (sound-on-sound): while set in
                                      RECIRC, the head writes old*od_decay +
                                      conditioned input at every position it
                                      visits (ZOH across varispeed skips) with
                                      a soft ceiling — layers accumulate in
                                      float with zero generational loss.      */
    float        od_decay;         /* per-pass fade of existing material while
                                      overdubbing (~0.95); no effect when idle */
    float        od_gain;          /* engage/release ramp (~10 ms) on the
                                      layered input — no click fronts in the
                                      buffer at the hold edges               */
    float        od_env;           /* write-limiter peak envelope (instant
                                      attack, slow decay)                    */
    float        od_lim;           /* write-limiter GAIN, slewed ~5 ms toward
                                      0.75/env: audio-rate gain ripple on the
                                      old fast-attack follower was harmonic
                                      distortion baked into the loop (field:
                                      second overdub = grit)                 */
    float        od_xprev;         /* previous layered-input sample: linear
                                      interp for multi-writes (rate > 1)     */
    float        od_xsum;          /* input accumulator: box-decimation for
                                      dropped samples (rate < 1) — ZOH here
                                      was audible zipper when the multiplier
                                      moved during an overdub               */
    uint32_t     od_xn;            /* samples accumulated                    */
    /* CHUNKED SEAM SPLICE (field 2026-07-29): the inline 960-RMW splice
     * burst at punch-out starved the ISR (~115% of block budget) while the
     * write head sat AT the seam — the torn block was RECORDED at loop
     * start and replayed every wrap. The splice is now a job the engine
     * services 8 samples per process() call: same result in ~1.3 ms,
     * bounded ~2% ISR cost, no burst. */
    uint8_t      spl_active;
    uint32_t     spl_start, spl_end, spl_fade, spl_idx;
    float        od_lp1, od_lp2;   /* 2-pole ~10 kHz lowpass on the LAYERED
                                      INPUT only: breaks ultrasonic feedback
                                      modes through the sound-on-sound loop
                                      (field forensics: an 18.9 kHz tone at
                                      13%% of loop energy after od sessions —
                                      codec round-trip resonance, held at the
                                      edge of stability by the write limiter).
                                      Inaudible on layers: tape sound-on-sound
                                      loses top end by nature.               */
} engine_t;

/* buf/len: delay memory. base_delay: cycle length in samples (SHORT/FULL).
 * time_lo/hi: TIME MULTIPLIER range. slew: one-pole coeff for taps + time. */
void  engine_init(engine_t *e, float *buf, uint32_t len,
                  float base_delay, float time_lo, float time_hi, float slew);

/* config DIP sw2: set the record-path bandwidth limit. cutoff_hz <= 0 => off. */
void  engine_set_bandwidth(engine_t *e, float fs, float cutoff_hz);

/* config DIP sw1 (x10 extend): largest base_delay whose deepest tap (base*time_hi)
 * still fits `len` with interpolation margin. Clamp the extended base with this. */
float engine_clamp_base(float base, uint32_t len, float time_hi);

/* Process one input sample; time_raw01 is the TIME control in [0,1]. Returns the
 * summed ("mixed") output. */
float engine_process(engine_t *e, float input, float time_raw01);

/* Same, but also fill `chan[NUM_TAPS]` with the 8 per-tap channel outputs (→ the
 * CS42888's 8 DAC channels). Returns the summed output (→ the "mixed" jacks). */
float engine_process_multi(engine_t *e, float input, float time_raw01, float chan[NUM_TAPS]);

/* Transport control (driven by panel/pulse layer). */
void  engine_write(engine_t *e);    /* enter WRITE at current head          */
void  engine_recirc(engine_t *e);   /* enter RECIRC, capture loop window     */
void  engine_resplice(engine_t *e); /* re-run the seam splice, chunked (od)  */
/* Enter RECIRC looping exactly the last `window` samples (the stock semantics:
 * "recirc loops the buffer at one of three cycle lengths"). */
void  engine_recirc_window(engine_t *e, uint32_t window);
/* Enter RECIRC looping [start, current head] (store-beg/store-end marking). */
void  engine_recirc_between(engine_t *e, uint32_t start);
/* Enter RECIRC over an explicit SAVED window [start,end] (store-end hold: the
 * head has moved past end, so recirc_between can't express it). */
void  engine_recirc_span(engine_t *e, uint32_t start, uint32_t end);

#endif /* ENGINE_H */
