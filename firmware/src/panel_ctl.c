/* panel_ctl.c — see panel_ctl.h. Clean-room decode from the RE bit map. */
#include "panel_ctl.h"

/* Bit positions in the assembled 13-bit word (MSB-first, matching the stock read
 * in panel.c = sub_4290). Confirmed against the decompile (re/binja): these are the
 * exact bits sub_4310(N) tests, and the decode below replicates the firmware's own
 * logic, so it needs no bench guessing (only the physical switch->bit polarity is a
 * hardware fact, and it's identical since we drive the same 165 on PA4/5/6). */
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
    /* Preset A/B/C/D — the stock tap-position lookup tests bits 0/1/2 ACTIVE-LOW in
     * priority order (sub_4310(0)>then(1)>then(2), decompile):
     *   bit0=0 -> A (linear ramp 20..160); bit1=0 -> B; bit2=0 -> C; none low -> D. */
    if      (!bit(bits, B_PRESET_A)) out->preset = 0;
    else if (!bit(bits, B_PRESET_B)) out->preset = 1;
    else if (!bit(bits, B_PRESET_C)) out->preset = 2;
    else                             out->preset = 3;

    /* Octave/rate ×1/×2/×4 — exact logic from get_mode_from_switches (sub_1110):
     *   if (!bit10) return 0(×1); if (!bit9) return 2(×4); return 1(×2).
     * bit10 is the master (bit9 only matters when bit10 is set). */
    if      (!bit(bits, B_OCT1))     out->octave = 1;   /* bit10=0        -> ×1 */
    else if (!bit(bits, B_OCT0))     out->octave = 4;   /* bit10=1,bit9=0 -> ×4 */
    else                             out->octave = 2;   /* bit10=1,bit9=1 -> ×2 */

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
    /* Preset A (0) = the stock linear ramp: the sub_4310(0) branch returns
     * (tap+1)*0x14 = (i+1)*20 -> 20,40,..,160. This is code-exact.
     * B/C/D (1/2/3) read rows of preset_phase_table (+16 / +0 / +8) that the
     * preset-DIP scan (sub_3488) fills from the physical DIP matrix -- pending the
     * matrix scan, so fall back to A and flag not-real. */
    for (int i = 0; i < NUM_TAPS; i++) phase[i] = 20.0f * (float)(i + 1);
    return (preset == 0u) ? 1 : 0;
}
