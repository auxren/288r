/* test_panel_ctl.c — 74HC165 word decode (see src/panel_ctl.c). Bit ROLES are from
 * the RE; polarity/encoding are [BENCH], so this pins the decode to the documented
 * assumption (set bit = asserted). Built by `make test`.
 */
#include "panel_ctl.h"
#include <stdio.h>

static int fails = 0;
static void ck(const char *name, int cond) {
    printf("  %-40s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

#define BIT(n) (1u << (n))

int main(void)
{
    panel_ctl_t p;

    /* FINAL PROVEN MAP (bench session 5 logger). cal = bit0 active-low */
    panel_decode(0x1FFF, &p);                ck("bit0=1 -> pre-set mode", p.cal == 0);
    panel_decode(0x1FFF & ~BIT(0), &p);      ck("bit0=0 -> cal mode", p.cal == 1);

    /* preset A/B/C selector: bits 1/2 active-low, C = both high */
    panel_decode(0x1FFF & ~BIT(1), &p);      ck("bit1=0 -> C", p.preset == 2);
    panel_decode(0x1FFF & ~BIT(2), &p);      ck("bit2=0 -> B", p.preset == 1);
    panel_decode(0x1FFF, &p);                ck("both high -> A", p.preset == 0);

    /* x1/x2 bit 3 (measured: 1 = x1) */
    panel_decode(0x1FFF, &p);                ck("bit3=1 -> x1", p.octave == 1);
    panel_decode(0x1FFF & ~BIT(3), &p);      ck("bit3=0 -> x2", p.octave == 2);
    ck("octave_factor(x2) == 2.0", panel_octave_factor(&p) == 2.0f);

    /* RED AUTO CONTROL bits 7/8: all-sounds latch / next-sound arm / center */
    panel_decode(0x1FFF, &p);                ck("7,8 high -> READY (0)", p.automode == 0);
    panel_decode(0x1FFF & ~BIT(7), &p);      ck("bit7=0 -> all sounds/DELAY (1)", p.automode == 1);
    panel_decode(0x1FFF & ~BIT(8), &p);      ck("bit8=0 -> next-sound arm (2)", p.automode == 2);

    /* CYCLE 3-way: bits 9/10, sub_1110-exact */
    panel_decode(0x1FFF & ~BIT(10), &p);     ck("bit10=0 -> cycle 0", p.cycle == 0);
    panel_decode(0x1FFF & ~BIT(9), &p);      ck("bit9=0 (10=1) -> cycle 2", p.cycle == 2);
    panel_decode(0x1FFF, &p);                ck("both high -> cycle 1", p.cycle == 1);
    ck("cycle_factor(center) == 0.5", panel_cycle_factor(&p) == 0.5f);
    panel_decode(0x1FFF & ~BIT(9), &p);      ck("bit9-low end -> 1.0", panel_cycle_factor(&p) == 1.0f);
    panel_decode(0x1FFF & ~BIT(10), &p);     ck("bit10-low end -> 0.25", panel_cycle_factor(&p) == 0.25f);

    /* red WRITE/RECIRC momentary: bits 11/12 active-low */
    panel_decode(0x1FFF, &p);                ck("idle -> no transport press", !p.write_trig && !p.recirc_trig);
    panel_decode(0x1FFF & ~BIT(11), &p);     ck("bit11=0 -> WRITE pressed", p.write_trig == 1);
    panel_decode(0x1FFF & ~BIT(12), &p);     ck("bit12=0 -> RECIRC pressed", p.recirc_trig == 1);
    /* black store switch: bits 5/6 */
    panel_decode(0x1FFF & ~BIT(5), &p);      ck("bit5=0 -> store beg", p.store_beg == 1);
    panel_decode(0x1FFF & ~BIT(6), &p);      ck("bit6=0 -> store end", p.store_end == 1);
    panel_decode(0x1FFF, &p);                ck("bit4 -> time_pitch", (panel_decode(0x1FFF & ~BIT(4), &p), p.time_pitch == 0));

    /* preset phase rows: A is the real default; B/C are flagged placeholders */
    float ph[NUM_TAPS];
    int aReal = panel_preset_phase(0, ph);
    ck("preset A is real (returns 1)", aReal == 1);
    ck("preset A phases 20..160", ph[0] == 20.0f && ph[NUM_TAPS-1] == 160.0f);
    int bReal = panel_preset_phase(1, ph);
    ck("preset B flagged placeholder (returns 0)", bReal == 0);
    ck("preset B falls back to A phases", ph[0] == 20.0f && ph[NUM_TAPS-1] == 160.0f);

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
