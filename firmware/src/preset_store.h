/* preset_store.h — flash-backed, reboot-proof preset slots (Option A: capture-live).
 *
 * Replaces the stock's live rear-DIP presets with SAVABLE slots: a save chord snaps
 * the current setting into the selected slot in internal flash; the A/B/C/D selector
 * recalls it. Each slot is a versioned/CRC'd storage record, so a blank or corrupt
 * slot falls back to a safe default (the A-ramp) instead of garbage.
 *
 * This module is pure/host-tested: it packs a scene into a slot blob and loads/
 * validates one from a memory-mapped store. The actual flash erase+program (writing
 * a fresh blob at slot offset) is the [BENCH] F429 internal-flash driver, given the
 * bytes from preset_pack(); flash is memory-mapped for reads, so preset_load reads
 * the store base directly.
 */
#ifndef PRESET_STORE_H
#define PRESET_STORE_H

#include <stdint.h>
#include <stddef.h>

#define PRESET_SLOTS       4u     /* A/B/C/D */
#define PRESET_TAPS        8u
#define PRESET_SLOT_BYTES  64u    /* header(10) + scene(40) + margin, flash-friendly */

typedef struct {
    float   phase[PRESET_TAPS];   /* tap phase-select 0..160 (the preset core)      */
    float   mult;                 /* time-multiplier [0,1] (pinned on recall)       */
    uint8_t octave;               /* 1/2/4 coarse                                   */
    uint8_t mute_mask;            /* per-tap mute bits                              */
    uint8_t rsvd[2];              /* frozen padding / future use                    */
} preset_scene_t;

_Static_assert(sizeof(preset_scene_t) == 40, "preset scene layout is frozen");

/* Safe default: the code-exact A-ramp (20,40,..,160), mult at noon, x1, no mutes. */
void   preset_defaults(preset_scene_t *s);

/* Serialize a scene into `blob` (must hold >= PRESET_SLOT_BYTES). Returns bytes
 * written (a storage record); hand these to the flash driver to program the slot. */
size_t preset_pack(const preset_scene_t *s, uint8_t *blob);

/* Load slot `slot` from a memory-mapped store base (PRESET_SLOTS*PRESET_SLOT_BYTES).
 * Validates magic/version/CRC; on ANY failure (blank/corrupt/old/out-of-range) fills
 * defaults and returns 0, else 1. */
int    preset_load(const uint8_t *store, unsigned slot, preset_scene_t *s);

#endif /* PRESET_STORE_H */
