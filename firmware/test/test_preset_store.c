/* test_preset_store.c — flash-backed preset slots (see src/preset_store.c). Simulates
 * the flash store as a RAM region (the real flash driver programs the same bytes).
 * Built by `make test`.
 */
#include "preset_store.h"
#include "storage.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
static void ck(const char *name, int cond) {
    printf("  %-48s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

/* "save" = pack the scene into the slot's region (what the flash driver programs). */
static void save(uint8_t *store, unsigned slot, const preset_scene_t *s) {
    preset_pack(s, store + slot * PRESET_SLOT_BYTES);
}

int main(void)
{
    uint8_t store[PRESET_SLOTS * PRESET_SLOT_BYTES];
    memset(store, 0xFF, sizeof store);        /* erased flash */

    preset_scene_t d, s;

    /* defaults are the A-ramp */
    preset_defaults(&d);
    ck("defaults: A-ramp 20..160", d.phase[0] == 20.0f && d.phase[PRESET_TAPS-1] == 160.0f);
    ck("defaults: mult noon, x1", d.mult == 0.5f && d.octave == 1);

    /* erased slot -> defaults, flagged */
    int r = preset_load(store, 0, &s);
    ck("erased slot -> defaults (returns 0)", r == 0 && s.phase[0] == 20.0f);

    /* capture-live: a custom scene saved + recalled intact */
    preset_scene_t live = d;
    for (unsigned i = 0; i < PRESET_TAPS; i++) live.phase[i] = 5.0f * (float)(i + 3);
    live.mult = 0.73f; live.octave = 4; live.mute_mask = 0x2A;
    save(store, 2, &live);
    r = preset_load(store, 2, &s);
    ck("saved slot loads (returns 1)", r == 1);
    ck("scene round-trips exactly", memcmp(&live, &s, sizeof s) == 0);

    /* slots are independent */
    preset_scene_t other = d; other.octave = 2; other.phase[0] = 99.0f;
    save(store, 3, &other);
    preset_load(store, 2, &s);   ck("slot 2 unaffected by slot 3 save", s.octave == 4);
    preset_load(store, 3, &s);   ck("slot 3 has its own scene", s.octave == 2 && s.phase[0] == 99.0f);

    /* corrupt a saved slot -> defaults, not garbage */
    store[3 * PRESET_SLOT_BYTES + 12] ^= 0x40;
    r = preset_load(store, 3, &s);
    ck("corrupt slot -> defaults (returns 0)", r == 0 && s.phase[0] == 20.0f);

    /* out-of-range slot -> defaults */
    r = preset_load(store, PRESET_SLOTS + 1u, &s);
    ck("out-of-range slot -> defaults", r == 0 && s.mult == 0.5f);

    /* a future-version record is refused (bump-to-invalidate works) */
    { uint8_t blob[PRESET_SLOT_BYTES];
      storage_pack(blob, /*version*/ 99, &live, (uint16_t)sizeof live);
      memcpy(store + 1 * PRESET_SLOT_BYTES, blob, PRESET_SLOT_BYTES);
      r = preset_load(store, 1, &s);
      ck("wrong-version slot -> defaults", r == 0); }

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
