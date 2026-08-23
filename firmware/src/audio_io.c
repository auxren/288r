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
    /* sign-extend 24-bit; shift in unsigned (a signed << that overflows is UB —
     * see audio_word_overrange) */
    int32_t s = (int32_t)((uint32_t)codec_word << 8) >> 8;
    return (float)s * (1.0f / FS24);
}

/* Saturating float -> 24-bit signed. On the target VCVT saturates to the int32
 * rails and SSAT does the 24-bit clamp in one cycle, which is why the float
 * clamps below could go. The host has no saturating cast (out-of-range is UB),
 * so it clamps in float first — same result for every value the target can
 * produce, and the host path is not in anybody's cycle budget. */
static inline int32_t f_to_i24(float y)
{
#if defined(__ARM_FEATURE_SAT)
    int32_t s = (int32_t)(y * FS24_M1);
    __asm volatile ("ssat %0, #24, %1" : "=r"(s) : "r"(s));
    return s;
#else
    if (!(y >= -1.0f)) y = (y != y) ? 0.0f : -1.0f;   /* NaN -> 0, like VCVT */
    if (y > 1.0f) y = 1.0f;
    return (int32_t)(y * FS24_M1);
#endif
}

int32_t audio_f_to_out(float x)
{
    /* SOFT-KNEE output limiter (patched-feedback quality): transparent below
     * 0.75 FS (zero cost, bit-exact passthrough — normal audio never touches
     * it), rational knee above it asymptotic to full scale. A patched feedback
     * loop pushed past unity gain then blooms like tape compression instead of
     * shattering on a hard digital clip. Knee: y = t + e/(1 + e/(1-t)),
     * e = |x|-t — C1-continuous at the knee, asymptote exactly 1.0.
     *
     * The CURVE is unchanged (it is field-calibrated: a 1.15x external feedback
     * loop settles at 0.877 FS). What changed is how it is evaluated, because
     * this runs 8 times per frame and the old form spent most of its cycles in
     * the FPU status register rather than on arithmetic:
     *
     *  - |x| and the sign restore were float compares. On an M4F every float
     *    compare is VCMPE + VMRS APSR_nzcv, and that FPSCR->core transfer
     *    stalls the pipeline. Both are now bit operations on the sign bit, in
     *    core registers, where the value has to be anyway for the next step.
     *  - the knee test compares INTEGER bit patterns. For a non-negative float
     *    the IEEE-754 encoding is monotone in the value, so |x| > 0.75f is
     *    exactly (bits & 0x7fffffff) > 0x3f400000 — one cycle, no FPU status.
     *  - the +/-1.0 float clamps are gone. They were unreachable: the knee's
     *    asymptote already bounds |y| < 1.0 for every |x| > 0.75, and below the
     *    knee |x| <= 0.75. The never-wrap guarantee they existed for is now
     *    made by the saturating conversion in f_to_i24(), which also covers the
     *    two cases the float clamps did NOT (inf and NaN both used to reach the
     *    cast as garbage).
     *
     * ONE BEHAVIOUR CHANGE CAME WITH THAT REWRITE, and it belongs in the
     * release notes rather than only in a diff: audio_f_to_out(inf) now returns
     * 0 (silence) where the old float clamps returned full scale. An infinity
     * reaches the knee, e/(1 + e*4) evaluates to NaN, and f_to_i24 maps NaN to
     * 0 exactly as the target's VCVT does. Neither answer is "right" — nothing
     * upstream can produce an infinity without a fault already having happened —
     * but "a fault becomes silence rather than a rail" is a decision about the
     * output stage, and test_softknee asserts it so it stays a decision.
     *
     * The single VDIV stays. A Newton-Raphson reciprocal (bit-trick seed + 3
     * iterations) is ~6 dependent FPU ops at 3-cycle latency each and measures
     * SLOWER than the 14-cycle VDIV on this core, and every division-free
     * approximation accurate enough not to add audible distortion above the
     * knee costs more than it saves. The divide is also skipped entirely for
     * normal programme material, which is the reason the threshold exists. */
    union { float f; uint32_t u; } v;
    v.f = x;
    const uint32_t sign = v.u & 0x80000000u;
    v.u &= 0x7FFFFFFFu;                          /* |x|, integer-comparable */
    if (v.u > 0x3F400000u) {                     /* |x| > 0.75f  */
        const float t = 0.75f;
        float e = v.f - t;
        v.f = t + e / (1.0f + e * (1.0f / (1.0f - t)));
    }
    v.u |= sign;                                 /* restore polarity */
    return f_to_i24(v.f) & 0x00FFFFFF;   /* 24-bit, right-aligned in the slot */
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
