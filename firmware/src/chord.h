/* chord.h — "hold both switches" gesture detector (the preset-save chord).
 *
 * Fires ONCE when `both` has been held continuously for `hold_samples`, and won't
 * fire again until the switches are released — so a 2 s both-held press is distinct
 * from SW14/SW16's momentary WRITE/RECIRC taps and can't trigger by accident. Poll
 * at the panel-scan rate; hold_samples = seconds * scan_rate. Generic (reusable for
 * a cal-mode gesture too).
 */
#ifndef CHORD_H
#define CHORD_H

#include <stdint.h>

typedef struct {
    uint16_t count;   /* consecutive polls with both held */
    uint8_t  fired;   /* latched after firing until release */
} chord_t;

void chord_init(chord_t *c);

/* both: 1 if both trigger switches are currently held. Returns 1 exactly once,
 * when the hold first reaches hold_samples. */
int  chord_update(chord_t *c, int both, uint16_t hold_samples);

#endif /* CHORD_H */
