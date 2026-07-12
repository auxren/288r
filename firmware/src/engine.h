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

typedef struct {
    delay_line_t dl;
    taps_t       taps;
    time_ctrl_t  time;
    transport_t  xport;
    mixer_t      mix;
    dl_interp_t  interp;
    float        in_gain;
    float        auto_correction;  /* AUTO CONTROL term (placeholder; calibrate) */
    int          vintage_bits;     /* 0 = full precision, else e.g. 12 (vintage)  */
} engine_t;

/* buf/len: delay memory. base_delay: cycle length in samples (SHORT/FULL).
 * time_lo/hi: TIME MULTIPLIER range. slew: one-pole coeff for taps + time. */
void  engine_init(engine_t *e, float *buf, uint32_t len,
                  float base_delay, float time_lo, float time_hi, float slew);

/* Process one input sample; time_raw01 is the TIME control in [0,1]. */
float engine_process(engine_t *e, float input, float time_raw01);

/* Transport control (driven by panel/pulse layer). */
void  engine_write(engine_t *e);    /* enter WRITE at current head          */
void  engine_recirc(engine_t *e);   /* enter RECIRC, capture loop window     */

#endif /* ENGINE_H */
