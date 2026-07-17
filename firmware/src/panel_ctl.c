/* panel_ctl.c — see panel_ctl.h. Clean-room decode from the RE bit map. */
#include "panel_ctl.h"

/* Bit positions in the assembled 13-bit word (MSB-first, matching the stock read
 * in panel.c). From re/notes/panel-scan.md. [BENCH] confirm order + polarity. */
#define B_PRESET_A  0
#define B_PRESET_B  1
#define B_PRESET_C  2
#define B_TAPRAW    3
#define B_BANKB     6
#define B_WRITE     7
#define B_RECIRC    8
#define B_OCT0      9
#define B_OCT1      10

static inline unsigned bit(uint16_t b, unsigned n) { return (b >> n) & 1u; }

void panel_decode(uint16_t bits, panel_ctl_t *out)
{
    /* A/B/C preset select. The stock uses bits 0/1/2 as the lookup row; whether
     * that's one-hot (3-position selector) or binary is [BENCH]. Treat as a
     * priority selector: C, then B, else A — works for a one-hot latching switch. */
    if      (bit(bits, B_PRESET_C)) out->preset = 2;
    else if (bit(bits, B_PRESET_B)) out->preset = 1;
    else                            out->preset = 0;

    /* octave/rate ×1/×2/×4 from two bits. [BENCH] exact encoding; assume binary
     * 0->x1, 1->x2, >=2->x4. */
    unsigned oct = bit(bits, B_OCT0) | (bit(bits, B_OCT1) << 1);
    out->octave = (oct >= 2u) ? 4u : (oct == 1u ? 2u : 1u);

    out->bank_b       = (uint8_t)bit(bits, B_BANKB);
    out->write_trig   = (uint8_t)bit(bits, B_WRITE);
    out->recirc_trig  = (uint8_t)bit(bits, B_RECIRC);
    out->tap_raw_mode = (uint8_t)bit(bits, B_TAPRAW);
}

float panel_octave_factor(const panel_ctl_t *p)
{
    return (float)p->octave;
}

int panel_preset_phase(unsigned preset, float phase[NUM_TAPS])
{
    /* Row A: confirmed stock default (preset_phase_table = 20,40,..,160).
     * B/C rows are [BENCH] — not yet recovered; fall back to A so the selector is
     * wired and safe, but return 0 to flag "placeholder, not the real preset". */
    static const float A[NUM_TAPS] = { 20, 40, 60, 80, 100, 120, 140, 160 };
    for (int i = 0; i < NUM_TAPS; i++) phase[i] = A[i];
    return (preset == 0u) ? 1 : 0;
}
