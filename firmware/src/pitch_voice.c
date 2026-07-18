/* pitch_voice.c — see pitch_voice.h. CV->ratio map + ratio slew over pitch_shift. */
#include "pitch_voice.h"
#include "fast_math.h"

/* Buchla is 1.2 V/oct (NOT 1 V/oct): one octave = 1.2 V => ratio 2. */
#define PV_OCT_PER_VOLT (1.0f / 1.2f)

void pv_init(pitch_voice_t *v, float window, float base, float slew)
{
    ps_init(&v->ps, window, base);
    v->ratio  = 1.0f;
    v->target = 1.0f;
    v->slew   = slew;
}

void pv_set_ratio(pitch_voice_t *v, float ratio)
{
    /* clamp at the ENTRY, not just inside ps: an unbounded target (seen live:
     * railed CV x attenuverter -> ratio 16.6) poisons the wet-mix |ratio-1|
     * math and makes the slew traverse garbage territory between updates. */
    if (ratio < 0.25f) ratio = 0.25f;      /* ps core contract: +/-2 oct */
    if (ratio > 4.0f)  ratio = 4.0f;
    v->target = ratio;
}

void pv_set_cv(pitch_voice_t *v, float volts)
{
    pv_set_ratio(v, fm_exp2f(volts * PV_OCT_PER_VOLT));
}

float pv_process(pitch_voice_t *v, const delay_line_t *d, dl_interp_t interp)
{
    v->ratio += (v->target - v->ratio) * v->slew;   /* glide the ratio */
    ps_set_ratio(&v->ps, v->ratio);                  /* clamps to +/-2 oct */
    return ps_process(&v->ps, d, interp);
}
