/* delay_engine.c — 288r delay core, reconstructed in readable C.
 *
 * Independent reconstruction from behavioral analysis of B288-REV1.0.hex
 * (see re/notes/delay-engine.md). NOT vendor source. The point of this file is
 * to be edited: the smooth-modulation fix (interpolated read) is expressed here
 * as ordinary C — compare delay_read_tap() below with the binary code-cave
 * detour in re/patches/patch1_interp.s.
 *
 * Sample format: 32-bit words in the SDRAM circular buffer holding signed audio
 * that the write path has bit-reduced by an arithmetic right shift (12/16/20)
 * per the "vintage" resolution mode; the read path shifts left to restore.
 */
#include <stdint.h>

/* Resolution / "vintage" bit-depth mode (get_mode_2bit, *0x2000208a).      */
typedef enum { RES_12BIT = 2, RES_16BIT = 1, RES_20BIT = 0 } res_mode_t;

/* Transport state machine (*0x200000d0). Values observed in the binary.    */
typedef enum { XPORT_WRITE = 1, XPORT_RECIRC = 2 /* + transitional 3,5,6 */ } xport_t;

typedef struct {
    int32_t  *bank_a;        /* @0x20000004 — primary delay memory (SDRAM)   */
    int32_t  *bank_b;        /* @0x20000008 — secondary / recirc path        */
    uint32_t  length;        /* @0x20000000 — buffer length in samples       */
    uint32_t  write_ptr;     /* @0x200000c4 — record head                    */
    uint32_t  loop_start;    /* @0x200000c8                                  */
    uint32_t  loop_end;      /* @0x200013c0                                  */
    res_mode_t res;          /* current output word width                    */
    xport_t   mode;          /* transport state                             */
} delay_state_t;

static inline int res_shift(res_mode_t r) { return r == RES_12BIT ? 12 : r == RES_16BIT ? 16 : 20; }

/* ---- WRITE path  (⇦ delay_tap_service_A/B, sub_1250/sub_15dc) ------------
 * Record one input sample into the circular buffer, bit-reduced, and advance
 * the write head. (Envelope-follower + full transport handling omitted here
 * for clarity — this is the storage kernel.)                                */
void delay_write_sample(delay_state_t *d, int32_t sample_24b)
{
    int sh = res_shift(d->res);
    d->bank_a[d->write_ptr] = sample_24b >> sh;      /* arithmetic (signed)   */
    if (++d->write_ptr >= d->length) d->write_ptr = 0;
}

/* ---- READ path  (⇦ tap read+mixer, sub_1968/sub_1c98) --------------------
 * THE FIX. `dist` is the fractional delay in samples for one tap
 * (dist = tap_pos + correction + tap_pos*scale, computed by the caller).
 *
 * Stock firmware did: out = bank[write_ptr - (int)dist]   — integer, zippered.
 * Here we keep the fraction and linearly interpolate between the two straddling
 * samples, which is what makes delay-time sweeps glide (chorus/flanger).      */
int32_t delay_read_tap(const delay_state_t *d, float dist)
{
    uint32_t len = d->length;

    /* floor for dist >= 0 (matches Cortex-M4 vcvt.s32.f32, no VRINT on M4).  */
    int32_t  i0    = (int32_t)dist;
    float    frac  = dist - (float)i0;               /* in [0,1)              */

    /* Two straddling read positions behind the write head, wrapped.         */
    int32_t  r0 = (int32_t)d->write_ptr - i0;
    while (r0 < 0)        r0 += len;
    int32_t  r1 = r0 - 1;                             /* one sample older     */
    if (r1 < 0)          r1 += len;

    float s0 = (float)d->bank_a[r0];
    float s1 = (float)d->bank_a[r1];
    float out = s0 + (s1 - s0) * frac;               /* linear interpolation  */

    return ((int32_t)out) << res_shift(d->res);      /* restore output width  */
}

/* Follow-ups (see re/patches/README.md): apply the same interpolation to the
 * path-B function and the mode==6 bank_b fetch; optionally swap linear for an
 * all-pass/Lagrange interpolator; add a one-pole slew on `dist` upstream so a
 * modulating CV produces a smooth swept distance. */
