/* panel_ctl.c — see panel_ctl.h. Clean-room decode from the RE bit map. */
#include "panel_ctl.h"

/* Bit positions in the assembled 13-bit word (MSB-first, matching the stock read
 * in panel.c = sub_4290). Confirmed against the decompile (re/binja): these are the
 * exact bits sub_4310(N) tests, and the decode below replicates the firmware's own
 * logic, so it needs no bench guessing (only the physical switch->bit polarity is a
 * hardware fact, and it's identical since we drive the same 165 on PA4/5/6). */
/* Bit map CONFIRMED LIVE on the unit (2026-07-17 mapping session, g_dbg_panel
 * capture while the owner flipped each control):
 *   bits 0/1/2 : A/B/C preset selector (active-low priority — tracked the knob)
 *   bit 3      : ×1/×2 switch — measured ×1 = 1, ×2 = 0 (was wrongly bits 9/10)
 *   bit 4      : TIME/pitch mode switch
 *   bit 8      : momentary (active-LOW, idle=1) — transport/save trigger
 *   bits 9/10  : NOT the octave switch (old sub_1110 guess) — unassigned, logged
 *   bits 6/7/11/12 : unmapped (bank_b guess on 6 retained, unused)              */
#define B_PRESET_A  0
#define B_PRESET_B  1
#define B_PRESET_C  2
#define B_X2        3
#define B_TIMEPITCH 4
#define B_BANKB     6
#define B_TRIG      8

static inline unsigned bit(uint16_t b, unsigned n) { return (b >> n) & 1u; }

void panel_decode(uint16_t bits, panel_ctl_t *out)
{
    /* Preset A/B/C/D — active-low priority (stock sub_4310(0..2)); confirmed live. */
    if      (!bit(bits, B_PRESET_A)) out->preset = 0;
    else if (!bit(bits, B_PRESET_B)) out->preset = 1;
    else if (!bit(bits, B_PRESET_C)) out->preset = 2;
    else                             out->preset = 3;

    /* ×1/×2 — single switch on bit 3, measured polarity: 1 = ×1, 0 = ×2. */
    out->octave = bit(bits, B_X2) ? 1u : 2u;

    out->time_pitch   = (uint8_t)bit(bits, B_TIMEPITCH);
    out->bank_b       = (uint8_t)bit(bits, B_BANKB);
    /* Momentary trigger: RAW level here; main applies idle-capture polarity. */
    out->write_trig   = (uint8_t)bit(bits, B_TRIG);
    out->recirc_trig  = (uint8_t)bit(bits, B_TRIG);
    out->tap_raw_mode = 0;
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
