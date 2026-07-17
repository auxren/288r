/* saturation.c — see saturation.h. Rational odd soft-clip, no libm. */
#include "saturation.h"

void sat_set(saturation_t *s, float drive, float mix)
{
    if (drive < 0.1f)  drive = 0.1f;
    if (drive > 32.0f) drive = 32.0f;
    if (mix < 0.0f) mix = 0.0f;
    else if (mix > 1.0f) mix = 1.0f;
    s->drive = drive;
    s->mix   = mix;
}

void sat_init(saturation_t *s, float drive, float mix) { sat_set(s, drive, mix); }

float sat_process(const saturation_t *s, float x)
{
    const float u = s->drive * x;
    const float a = (u < 0.0f) ? -u : u;
    const float w = u / (1.0f + a);          /* odd soft-clip, unity slope at 0 */
    return x + s->mix * (w - x);             /* (1-mix)*x + mix*w */
}
