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

    /* preset select — ACTIVE-LOW priority bit0>bit1>bit2 (A/B/C/D), matching the
     * stock sub_4310(0..2) tap-position lookup. */
    panel_decode(0u, &p);                     ck("bit0=0 -> A (0)", p.preset == 0);
    panel_decode(BIT(0), &p);                 ck("bit0=1,bit1=0 -> B (1)", p.preset == 1);
    panel_decode(BIT(0)|BIT(1), &p);          ck("bits0,1=1,bit2=0 -> C (2)", p.preset == 2);
    panel_decode(BIT(0)|BIT(1)|BIT(2), &p);   ck("bits0,1,2=1 -> D (3)", p.preset == 3);
    panel_decode(0u, &p);                     ck("bit0=0 wins over bit1/2 (priority)", p.preset == 0);

    /* octave — sub_1110 logic: bit10 master. (10=0)->x1, (10=1,9=0)->x4, (10=1,9=1)->x2 */
    panel_decode(0u, &p);                 ck("bit10=0 -> x1", p.octave == 1);
    panel_decode(BIT(9), &p);             ck("bit10=0 (bit9 set) still x1", p.octave == 1);
    panel_decode(BIT(10), &p);            ck("bit10=1,bit9=0 -> x4", p.octave == 4);
    panel_decode(BIT(10)|BIT(9), &p);     ck("bit10=1,bit9=1 -> x2", p.octave == 2);
    ck("octave_factor(x2) == 2.0", panel_octave_factor(&p) == 2.0f);

    /* discrete flags */
    panel_decode(BIT(6), &p); ck("bit6 -> bank_b",       p.bank_b == 1);
    panel_decode(BIT(7), &p); ck("bit7 -> write_trig",   p.write_trig == 1);
    panel_decode(BIT(8), &p); ck("bit8 -> recirc_trig",  p.recirc_trig == 1);
    panel_decode(BIT(3), &p); ck("bit3 -> tap_raw_mode", p.tap_raw_mode == 1);
    panel_decode(0u, &p);     ck("clear word -> all flags 0",
                                 !p.bank_b && !p.write_trig && !p.recirc_trig && !p.tap_raw_mode);

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
