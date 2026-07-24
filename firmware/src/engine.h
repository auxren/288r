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
/* Enter RECIRC looping exactly the last `window` samples (the stock semantics:
 * "recirc loops the buffer at one of three cycle lengths"). */
void  engine_recirc_window(engine_t *e, uint32_t window);
/* Enter RECIRC looping [start, current head] (store-beg/store-end marking). */
void  engine_recirc_between(engine_t *e, uint32_t start);
/* Enter RECIRC over an explicit SAVED window [start,end] (store-end hold: the
 * head has moved past end, so recirc_between can't express it). */
void  engine_recirc_span(engine_t *e, uint32_t start, uint32_t end);

#endif /* ENGINE_H */
