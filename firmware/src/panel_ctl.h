/* panel_ctl.h — decode the 13-bit 74HC165 word (bsp_panel_switches_read) into
 * logical panel controls, plus the A/B/C preset phase rows.
 *
 * Bit ROLES are confirmed from the stock disassembly (re/notes/panel-scan.md §1);
 * per-bit POLARITY, the exact octave encoding, and the B/C preset rows are [BENCH]
 * (a live toggle sweep labels them). The 165 read is input-only, so acting on it
 * can't break the audio path — worst case a mis-decoded bit gives a wrong delay,
 * which is observable and recoverable. That's why this is wired live (unlike the
 * 595 output, which is gated).
 */
#ifndef PANEL_CTL_H
#define PANEL_CTL_H

#include <stdint.h>
#include "taps.h"   /* NUM_TAPS */

typedef struct {
    uint8_t preset;        /* 0=A 1=B 2=C (bits 0/1/2)                     */
    uint8_t octave;        /* coarse delay factor 1/2/4 (bits 9/10)        */
    uint8_t bank_b;        /* bit 6: second buffer / recirc path           */
    uint8_t write_trig;    /* bit 7: momentary WRITE  (SW14 candidate)     */
    uint8_t recirc_trig;   /* bit 8: momentary RECIRC (SW16 candidate)     */
    uint8_t tap_raw_mode;  /* bit 3: raw read vs phase*mult                */
} panel_ctl_t;

/* Decode the assembled 13-bit word. [BENCH] polarity: assumes a set bit = asserted;
 * flip in one place here once measured. */
void  panel_decode(uint16_t bits13, panel_ctl_t *out);

/* Coarse octave factor (1.0 / 2.0 / 4.0) for scaling the base delay. */
float panel_octave_factor(const panel_ctl_t *p);

/* Fill phase[NUM_TAPS] for preset 0/1/2. Row A = the confirmed stock default
 * (20,40,..,160); B/C are [BENCH] placeholders (= A) until the preset tables are
 * recovered. Returns 1 if the row is a real (non-placeholder) preset, else 0. */
int   panel_preset_phase(unsigned preset, float phase[NUM_TAPS]);

#endif /* PANEL_CTL_H */
