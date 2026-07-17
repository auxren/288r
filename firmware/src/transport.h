/* transport.h — 288r WRITE / RECIRC (looper) transport (faithful clone).
 *
 * Stock firmware tracks this in transport_mode (@0x200000d0): 1=write, 2=recirc,
 * with transitional codes; the write head records input, and on the WRITE->RECIRC
 * edge a loop window [loop_start, loop_end] is captured and playback loops within it
 * (sub_1250 write branch / sub_1968 loop-capture). We model the two primary states
 * and the window capture; the *trigger sources* (manual WRITE/RECIRC switch, CYCLE
 * short/full, pulse inputs, auto-cycle) are inputs driven by the panel/pulse layer,
 * because which pin/pulse does what is a hardware-wiring (bench) detail.
 *
 * Coordinates with delay_line: in WRITE the engine calls dl_write(input) (head
 * advances linearly); in RECIRC the engine calls dl_advance_loop() over the window.
 */
#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <stdint.h>

typedef enum { XP_WRITE = 1, XP_RECIRC = 2 } xport_mode_t;

typedef struct {
    xport_mode_t mode;
    uint32_t     write_start;  /* head when WRITE began                     */
    uint32_t     loop_start;   /* captured on WRITE->RECIRC                  */
    uint32_t     loop_end;
    uint8_t      auto_cycle;   /* 0 = manual toggle, 1 = auto (CYCLE switch) */
} transport_t;

void transport_init(transport_t *x, uint8_t auto_cycle);

/* Enter WRITE at the current head; marks the loop start-of-record. */
void transport_begin_write(transport_t *x, uint32_t head);

/* Enter RECIRC; captures loop window [write_start, head]. */
void transport_begin_recirc(transport_t *x, uint32_t head);

/* 1 if the engine should write input this sample (WRITE), 0 for RECIRC playback. */
int  transport_should_write(const transport_t *x);

/* ---- edge-driven triggering from the momentary WRITE/RECIRC switches -------
 * The panel's SW14/SW16 momentaries (panel_ctl write_trig/recirc_trig) are LEVELS;
 * this turns a rising edge into a transport transition (press = act once, holding
 * does nothing). Call at the panel-scan rate with the current head. */
typedef struct { uint8_t prev_w, prev_r; } xport_trig_t;

void transport_trig_init(xport_trig_t *t);

/* Rising edge on write_lvl -> begin_write; on recirc_lvl -> begin_recirc (captures
 * the loop window). Returns 1 if a transition fired this call. */
int  transport_update_trig(transport_t *x, xport_trig_t *t,
                           int write_lvl, int recirc_lvl, uint32_t head);

#endif /* TRANSPORT_H */
