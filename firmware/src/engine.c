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
        if (e->vintage_bits > 0)
            x = dl_vintage_quantize(x, e->vintage_bits, 0.0f);
        dl_write(&e->dl, x);
    } else {
        dl_advance_loop(&e->dl, ls, le);
    }

    /* 3. read the 8 taps (loop-aware in RECIRC) */
    float taps[NUM_TAPS];
    for (int i = 0; i < NUM_TAPS; i++) {
        float delay = e->taps.cur[i];
        if (delay < 1.0f) delay = 1.0f;   /* keep off the write head */
        taps[i] = recirc ? dl_read_loop(&e->dl, delay, ls, le, e->interp)
                         : dl_read(&e->dl, delay, e->interp);
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
