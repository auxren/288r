/* preset_store.c — see preset_store.h. Scene records on storage.{c,h}. */
#include "preset_store.h"
#include "storage.h"

#define PRESET_VERSION 2   /* v2: +cycle in the scene */

void preset_defaults(preset_scene_t *s)
{
    for (unsigned i = 0; i < PRESET_TAPS; i++)
        s->phase[i] = 20.0f * (float)(i + 1);   /* A-ramp 20..160 (code-exact) */
    s->mult      = 0.5f;                          /* noon */
    s->octave    = 1;
    s->mute_mask = 0;
    s->cycle     = 2;   /* FULL */
    s->rsvd      = 0;
}

size_t preset_pack(const preset_scene_t *s, uint8_t *blob)
{
    return storage_pack(blob, PRESET_VERSION, s, (uint16_t)sizeof(*s));
}

int preset_load(const uint8_t *store, unsigned slot, preset_scene_t *s)
{
    if (slot < PRESET_SLOTS) {
        const uint8_t *b = store + (size_t)slot * PRESET_SLOT_BYTES;
        preset_scene_t tmp;
        if (storage_load(b, PRESET_SLOT_BYTES, PRESET_VERSION, &tmp,
                         (uint16_t)sizeof(tmp)) == (int)sizeof(tmp)) {
            *s = tmp;
            return 1;
        }
    }
    preset_defaults(s);
    return 0;
}
