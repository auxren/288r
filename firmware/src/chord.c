/* chord.c — see chord.h. */
#include "chord.h"

void chord_init(chord_t *c) { c->count = 0; c->fired = 0; }

int chord_update(chord_t *c, int both, uint16_t hold_samples)
{
    if (!both) {                       /* released -> re-arm */
        c->count = 0;
        c->fired = 0;
        return 0;
    }
    if (c->count < hold_samples) c->count++;
    if (c->count >= hold_samples && !c->fired) {
        c->fired = 1;                  /* latch: one fire per hold */
        return 1;
    }
    return 0;
}
