/* panel_ctl.h — decode the 13-bit 74HC165 word (bsp_panel_switches_read) into
 * logical panel controls, plus the preset phase rows.
 *
 * The decode REPLICATES the stock firmware's own bit logic, verified against the
 * decompile (re/binja): preset = sub_4310(0..2) active-low priority; octave =
 * get_mode_from_switches (sub_1110). So there is no bench guessing here — we read
 * the same 165 on PA4/5/6, so the physical switch->bit mapping is identical. The
 * only thing still pending is the B/C/D preset ROW VALUES, which the stock fills
 * from the physical preset-DIP matrix (sub_3488) — that needs the matrix scan.
 * Wired live: the 165 read is input-only, so a wrong bit gives a wrong delay
 * (observable, recoverable), never an audio drop (unlike the gated 595 output).
 */
#ifndef PANEL_CTL_H
#define PANEL_CTL_H

#include <stdint.h>
#include "taps.h"   /* NUM_TAPS */

typedef struct {
    uint8_t preset;        /* 0=A 1=B 2=C 3=D (bits 0/1/2, active-low priority;
                              CONFIRMED live on the unit)                       */
    uint8_t octave;        /* 1 or 2 — the ×1/×2 switch, bit 3 (confirmed live) */
    uint8_t time_pitch;    /* TIME/pitch mode switch, bit 4 (confirmed live)    */
    uint8_t bank_b;        /* bit 6 (RE guess, unused)                          */
    uint8_t write_trig;    /* RAW momentary level, bit 8 (idle-polarity in main)*/
    uint8_t recirc_trig;   /* = write_trig (single confirmed momentary bit)     */
    uint8_t tap_raw_mode;  /* unused                                            */
} panel_ctl_t;

/* Decode the assembled 13-bit word (replicates the stock bit logic). */
void  panel_decode(uint16_t bits13, panel_ctl_t *out);

/* Coarse octave factor (1.0 / 2.0 / 4.0) for scaling the base delay. */
float panel_octave_factor(const panel_ctl_t *p);

/* Fill phase[NUM_TAPS] for preset 0..3. Preset A (0) = the code-exact linear ramp
 * (20,40,..,160); B/C/D read the preset-DIP table (pending the matrix scan) and
 * fall back to A. Returns 1 if the row is real (A), else 0 (placeholder). */
int   panel_preset_phase(unsigned preset, float phase[NUM_TAPS]);

#endif /* PANEL_CTL_H */
