/* engine.c — see engine.h. */
#include "engine.h"

void engine_init(engine_t *e, float *buf, uint32_t len,
                 float base_delay, float time_lo, float time_hi, float slew)
{
    dl_init(&e->dl, buf, len);
    dl_clear(&e->dl);
    taps_init(&e->taps, base_delay, slew);
    tc_init(&e->time, 1.0f, slew, time_lo, time_hi);
    transport_init(&e->xport, /*auto_cycle*/ 0);
    mixer_init(&e->mix);
    bw_init(&e->bw, 1.0f, 0.0f);   /* bandwidth limit off (bypass = identity) */
    e->interp = DL_INTERP_HERMITE;
    e->in_gain = 1.0f;
    e->auto_correction = 0.0f;
    e->vintage_bits = 0;
    e->skip_tap_reads = 0;
    e->dith = 0x1234567u;                 /* dither PRNG seed */
    transport_begin_write(&e->xport, e->dl.wpos);
}

void engine_set_bandwidth(engine_t *e, float fs, float cutoff_hz)
{
    bw_init(&e->bw, fs, cutoff_hz);
}

float engine_clamp_base(float base, uint32_t len, float time_hi)
{
    /* deepest tap = base * (PHASE_FULLSCALE/PHASE_FULLSCALE) * time_hi = base*time_hi.
     * keep it + a guard (write-head + Hermite's 4-sample stencil) inside the buffer. */
    float denom   = (time_hi > 0.0f) ? time_hi : 1.0f;
    float max_base = ((float)len - 64.0f) / denom;
    return (base < max_base) ? base : max_base;
}

float engine_process_multi(engine_t *e, float input, float time_raw01, float chan[NUM_TAPS])
{
    /* 1. delay-time control -> tap positions (continuous, slewed) */
    float mult = tc_update(&e->time, time_raw01);
    taps_update(&e->taps, mult);

    const int recirc = !transport_should_write(&e->xport);
    const uint32_t ls = e->xport.loop_start, le = e->xport.loop_end;

    /* 2. record or recirculate */
    if (!recirc) {
        float x = mixer_input(input, e->in_gain);
        x = bw_process(&e->bw, x);            /* sw2 bandwidth limit (bypass=identity) */
        if (e->vintage_bits > 0) {
            /* TPDF dither (two xorshift uniforms, triangular in [-1,1] quantum
             * units): decorrelates the bit-crush distortion into benign noise —
             * proper lo-fi instead of gritty correlated error. */
            e->dith ^= e->dith << 13; e->dith ^= e->dith >> 17; e->dith ^= e->dith << 5;
            uint32_t r1 = e->dith;
            e->dith ^= e->dith << 13; e->dith ^= e->dith >> 17; e->dith ^= e->dith << 5;
            float tp = ((float)(r1 >> 8) + (float)(e->dith >> 8)) * (1.0f / 16777216.0f) - 1.0f;
            x = dl_vintage_quantize(x, e->vintage_bits, tp);
        }
        dl_write(&e->dl, x);
    } else {
        dl_advance_loop(&e->dl, ls, le);
    }

    /* pitch mode at full wet: the crossfade discards the tap outputs, so skip
     * the 8 SDRAM reads entirely (control, write, recirc all ran above). */
    if (e->skip_tap_reads) {
        for (int i = 0; i < NUM_TAPS; i++) chan[i] = 0.0f;
        return 0.0f;
    }

    /* 3. read the 8 taps (loop-aware in RECIRC) — exact int+frac path, so the
     * fraction survives at SDRAM buffer sizes (see dl_read_frac) */
    float taps[NUM_TAPS];
    for (int i = 0; i < NUM_TAPS; i++) {
        uint32_t d_int; float d_frac;
        taps_delay_frac(&e->taps, i, &d_int, &d_frac);
        if (d_int < 1) { d_int = 1; d_frac = 0.0f; }   /* keep off the write head */
        taps[i] = recirc ? dl_read_loop_frac(&e->dl, d_int, d_frac, ls, le, e->interp)
                         : dl_read_frac(&e->dl, d_int, d_frac, e->interp);
    }

    /* 4. mix: 8 per-tap DAC channels + the summed ("mixed") output */
    mixer_channels(&e->mix, taps, chan);
    return mixer_sum(&e->mix, taps, e->auto_correction);
}

float engine_process(engine_t *e, float input, float time_raw01)
{
    float chan[NUM_TAPS];
    return engine_process_multi(e, input, time_raw01, chan);
}

void engine_write(engine_t *e)  { transport_begin_write(&e->xport, e->dl.wpos); }
void engine_recirc(engine_t *e) { transport_begin_recirc(&e->xport, e->dl.wpos); }

void engine_recirc_window(engine_t *e, uint32_t window)
{
    if (window < 2u) window = 2u;
    if (window > e->dl.len - 4u) window = e->dl.len - 4u;
    uint32_t head = e->dl.wpos;
    uint32_t start = (head >= window) ? head - window : head + e->dl.len - window;
    e->xport.mode = XP_RECIRC;
    e->xport.loop_start = start;
    e->xport.loop_end = head;
    /* snap the head INTO the window: leaving it on loop_end lets dl_advance_loop
     * increment past the boundary and free-run the whole buffer (loop AUDIO was
     * right — reads are window-mapped — but the head never wrapped, so no
     * end-of-cycle events fired). */
    e->dl.wpos = start;
}

void engine_recirc_span(engine_t *e, uint32_t start, uint32_t end)
{
    if (start >= e->dl.len) start %= e->dl.len;
    if (end   >= e->dl.len) end   %= e->dl.len;
    if (start == end) return;
    e->xport.mode = XP_RECIRC;
    e->xport.loop_start = start;
    e->xport.loop_end = end;
    e->dl.wpos = start;
}

void engine_recirc_between(engine_t *e, uint32_t start)
{
    uint32_t head = e->dl.wpos;
    if (start >= e->dl.len) start %= e->dl.len;
    if (start == head) return;                /* zero-length: ignore */
    e->xport.mode = XP_RECIRC;
    e->xport.loop_start = start;
    e->xport.loop_end = head;
    e->dl.wpos = start;
}
