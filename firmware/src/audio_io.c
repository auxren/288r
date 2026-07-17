/* audio_io.c — see audio_io.h. */
#include "audio_io.h"

/* 24-bit full scale. TODO(bench): confirm the codec word alignment from the init capture. */
#define FS24    8388608.0f   /* 2^23  */
#define FS24_M1 8388607.0f   /* 2^23 - 1 */

float audio_in_to_f(int32_t codec_word)
{
    /* SAI DR presents 24-bit data RIGHT-aligned in bits [23:0], zero-extended.
     * Sign-extend from bit 23, then scale. (Confirmed on hardware from the CS42888
     * ADC stream: e.g. 0x00C522F3 -> -0.46, not a tiny value.) */
    int32_t s = (codec_word << 8) >> 8;                /* sign-extend 24-bit */
    return (float)s * (1.0f / FS24);
}

int32_t audio_f_to_out(float x)
{
    if (x >  1.0f) x =  1.0f;          /* clamp: never wrap to the opposite rail */
    if (x < -1.0f) x = -1.0f;
    int32_t s = (int32_t)(x * FS24_M1);
    return s & 0x00FFFFFF;             /* 24-bit data, right-aligned in the SAI slot */
}

void audio_io_block(engine_t *e, const int32_t *in, int32_t *out,
                    unsigned frames, unsigned in_slot, float time_raw01)
{
    if (in_slot >= ADC_SLOTS) in_slot = 0;
    for (unsigned f = 0; f < frames; f++) {
        float x = audio_in_to_f(in[f * ADC_SLOTS + in_slot]);

        float chan[NUM_TAPS];
        (void)engine_process_multi(e, x, time_raw01, chan);   /* mix -> analog sum jacks */

        int32_t *o = &out[f * DAC_SLOTS];
        for (unsigned c = 0; c < DAC_SLOTS; c++)
            o[c] = audio_f_to_out(chan[c]);                   /* 8 taps -> 8 DAC slots */
    }
}
