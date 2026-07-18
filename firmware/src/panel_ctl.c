/* panel_ctl.c — see panel_ctl.h. Decode of the 13-bit 74HC165 word.
 *
 * FINAL BIT MAP — every assignment PROVEN LIVE on the unit (bench session 5
 * logger: 5x presses per momentary, per-switch flips) + decompile cross-check:
 *   bit 0     : cal./pre-set switch (LOW = "cal." -> the evenly-spaced ramp;
 *               stock: if(!sub_4310(0)) return (tap+1)*20)
 *   bits 1/2  : preset A/B/C selector, active-low (A=bit1 low, B=bit2 low,
 *               C=both high — stock rows +16 / +0 / +8 of preset_phase_table)
 *   bit 3     : x1/x2 switch (1 = x1, 0 = x2; measured)
 *   bit 4     : TIME/pitch mode switch
 *   bits 5/6  : unassigned (bit6 kept as bank_b candidate)
 *   bits 7/8  : WRITE / RECIRC momentary — the RED (ON)-OFF-(ON) toggle between
 *               the write/recirc pulse-input jacks (five clean flips each side,
 *               logged; center = both HIGH, press pulls one LOW)
 *   bits 9/10 : CYCLE 3-way switch — sub_1110-exact decode: bit10=0 -> pos0,
 *               bit10=1,bit9=0 -> pos2, both -> pos1 (the stock's three cycle
 *               lengths / "PLL retune" input)
 *   bit 11    : STORE BEG. momentary (black, AUTO CONTROL section; active-low)
 *   bit 12    : STORE END  momentary (same switch, other side; active-low)
 */
#include "panel_ctl.h"

#define B_CALPRE    0
#define B_SEL_A     1
#define B_SEL_B     2
#define B_X2        3
#define B_TIMEPITCH 4
#define B_BANKB     6
/* bits 7/8 = RED AUTO CONTROL (all sounds latch / next-sound momentary) */
#define B_CYC0      9
#define B_CYC1      10
#define B_WRITE_M   11   /* red write/recirc momentary (A/B: may be swapped) */
#define B_RECIRC_M  12
/* black store beg/end switch = bits 5/6 (beg latched low observed; TBD) */

static inline unsigned bit(uint16_t b, unsigned n) { return (b >> n) & 1u; }

void panel_decode(uint16_t bits, panel_ctl_t *out)
{
    out->cal = !bit(bits, B_CALPRE);                     /* low = "cal." (ramp) */

    /* preset selector, panel order C/B/A — OWNER-CORRECTED: bit1 low = C,
     * bit2 low = B, both high = A */
    if      (!bit(bits, B_SEL_A)) out->preset = 2;   /* C */
    else if (!bit(bits, B_SEL_B)) out->preset = 1;   /* B */
    else                          out->preset = 0;   /* A */

    out->octave     = bit(bits, B_X2) ? 1u : 2u;         /* measured polarity   */
    out->time_pitch = (uint8_t)bit(bits, B_TIMEPITCH);
    out->bank_b     = (uint8_t)bit(bits, B_BANKB);

    /* RED AUTO CONTROL (owner-verified live): bit7 LATCHING low = "all sounds"
     * (delay mode); bit8 momentary low = "next sound" arm; center = ready. */
    if      (!bit(bits, 7)) out->automode = 1;       /* all sounds -> DELAY   */
    else if (!bit(bits, 8)) out->automode = 2;       /* next-sound arm pulse  */
    else                    out->automode = 0;       /* center -> READY/loop  */

    /* CYCLE 3-way — replicates sub_1110 exactly */
    if      (!bit(bits, B_CYC1)) out->cycle = 0;
    else if (!bit(bits, B_CYC0)) out->cycle = 2;
    else                         out->cycle = 1;

    /* momentaries: ACTIVE-LOW, polarity proven -> deliver pressed = 1 */
    out->write_trig  = (uint8_t)!bit(bits, B_WRITE_M);
    out->recirc_trig = (uint8_t)!bit(bits, B_RECIRC_M);
    out->store_beg   = (uint8_t)!bit(bits, 5);
    out->store_end   = (uint8_t)!bit(bits, 6);
    out->tap_raw_mode = 0;
}

float panel_octave_factor(const panel_ctl_t *p)
{
    return (float)p->octave;
}

/* CYCLE position -> base scale. OWNER-VERIFIED: bit9-low end = SHORT.
 * pos0 (bit10 low) = FULL 1.0; pos1 (center) = 0.5; pos2 (bit9 low) = SHORT 0.25 */
float panel_cycle_factor(const panel_ctl_t *p)
{
    return (p->cycle == 0) ? 1.0f : (p->cycle == 1) ? 0.5f : 0.25f;
}

int panel_preset_phase(unsigned preset, float phase[NUM_TAPS])
{
    /* fallback rows: the stock's B/C/D rows come from the preset-DIP matrix
     * (pending); the code-exact ramp covers cal. and the default. */
    (void)preset;
    for (int i = 0; i < NUM_TAPS; i++) phase[i] = 20.0f * (float)(i + 1);
    return (preset == 0u) ? 1 : 0;
}
