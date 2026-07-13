/* audio_io.c — see audio_io.h. */
#include "audio_io.h"

/* 24-bit full scale. TODO(bench): confirm the codec word alignment from the init capture. */
#define FS24    8388608.0f   /* 2^23  */
#define FS24_M1 8388607.0f   /* 2^23 - 1 */

float audio_in_to_f(int32_t codec_word)
{
    return (float)(codec_word >> 8) * (1.0f / FS24);   /* left-justified 24-bit -> [-1,1) */
}

int32_t audio_f_to_out(float x)
{
    if (x >  1.0f) x =  1.0f;          /* clamp: never wrap to the opposite rail */
    if (x < -1.0f) x = -1.0f;
    int32_t s = (int32_t)(x * FS24_M1);
    return s << 8;                     /* back to left-justified 24-bit in int32 */
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
