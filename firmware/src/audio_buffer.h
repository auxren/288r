/* audio_buffer.h — int16/int32 SDRAM storage layer for the delay line.
 *
 * DESIGN.md "Memory & fidelity": the stock firmware stores every sample in a full
 * int32 word regardless of fidelity, so "vintage" buys no memory. Here the fidelity
 * switch selects the STORAGE WIDTH, so vintage (<=16-bit) packs int16 and gets 2x
 * the delay time out of the same 8 MB SDRAM; hi-fi packs int32 (24-bit-clean).
 *
 * This mirrors delay_line's interface exactly (init/clear/write/read/read_at/
 * read_loop/advance_loop) so it is a drop-in for the engine's storage once wired.
 * Interpolation and mixing stay hardware float: samples are converted int<->float
 * only at the buffer boundary (a cheap VCVT), using the SAME Hermite/linear kernel
 * as delay_line. Layout is fixed at boot from the fidelity switch (DESIGN.md
 * "Boot-time layout rule") — a width change is a buffer reinit, never per-sample.
 *
 * Host-testable: point `buf` at ordinary RAM.
 */
#ifndef AUDIO_BUFFER_H
#define AUDIO_BUFFER_H

#include <stdint.h>
#include "delay_line.h"   /* dl_interp_t */

typedef enum {
    AB_FMT_I16 = 0,   /* vintage: 2 bytes/sample, 2x capacity */
    AB_FMT_I32,       /* hi-fi:   4 bytes/sample, 24-bit clean */
} ab_format_t;

typedef struct {
    void       *buf;          /* SDRAM storage (int16* or int32*)          */
    uint32_t    len;          /* capacity in SAMPLES (= bytes / width)     */
    uint32_t    wpos;         /* write head index                          */
    ab_format_t fmt;          /* storage width, fixed at init              */
    int         vintage_bits; /* 0 = full width; else crush on write (e.g. 12) */
} audio_buffer_t;

/* Bind `bytes` of storage as `fmt`. len = bytes/width; the SAME bytes give 2x the
 * samples for I16 vs I32 — that's the vintage capacity win. len ends up >= 4. */
void  ab_init(audio_buffer_t *ab, void *buf, uint32_t bytes, ab_format_t fmt);

void  ab_clear(audio_buffer_t *ab);
void  ab_set_vintage(audio_buffer_t *ab, int bits);   /* 0 = off */
uint32_t ab_capacity_samples(const audio_buffer_t *ab);

/* Push one sample (float, normalized [-1,1]; clamped) and advance the head. */
void  ab_write(audio_buffer_t *ab, float x);

/* Fractional reads — identical semantics to delay_line, returning float. */
float ab_read(const audio_buffer_t *ab, float delay, dl_interp_t interp);
float ab_read_at(const audio_buffer_t *ab, float index, dl_interp_t interp);
float ab_read_loop(const audio_buffer_t *ab, float delay,
                   uint32_t loop_start, uint32_t loop_end, dl_interp_t interp);
void  ab_advance_loop(audio_buffer_t *ab, uint32_t loop_start, uint32_t loop_end);

#endif /* AUDIO_BUFFER_H */
