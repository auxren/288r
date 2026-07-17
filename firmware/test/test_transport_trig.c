/* test_transport_trig.c — edge-driven WRITE/RECIRC triggering (see src/transport.c).
 * Momentary switch LEVELS -> transport transitions on the rising edge only, with
 * the loop window captured on WRITE->RECIRC. Built by `make test`.
 */
#include "transport.h"
#include <stdio.h>

static int fails = 0;
static void ck(const char *name, int cond) {
    printf("  %-46s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

int main(void)
{
    transport_t x; transport_init(&x, /*auto*/ 0);
    xport_trig_t t; transport_trig_init(&t);

    /* idle: no edges, stays in WRITE (init default) */
    int fired = transport_update_trig(&x, &t, 0, 0, 10);
    ck("idle: no transition", fired == 0 && transport_should_write(&x));

    /* rising WRITE edge at head=100: (re)enters WRITE, marks record start */
    transport_update_trig(&x, &t, 1, 0, 100);
    ck("write edge -> WRITE mode", transport_should_write(&x));
    ck("write edge marks write_start", x.write_start == 100);

    /* holding WRITE high: no re-trigger, write_start unchanged even as head moves */
    fired = transport_update_trig(&x, &t, 1, 0, 150);
    ck("holding write: no re-trigger", fired == 0);
    ck("holding write: write_start held", x.write_start == 100);

    /* rising RECIRC edge at head=250: capture loop window [write_start, head] */
    fired = transport_update_trig(&x, &t, 0, 1, 250);
    ck("recirc edge fired", fired == 1);
    ck("recirc edge -> RECIRC mode", !transport_should_write(&x));
    ck("loop_start = write_start (100)", x.loop_start == 100);
    ck("loop_end = head (250)", x.loop_end == 250);

    /* holding RECIRC: no re-trigger, window stable */
    fired = transport_update_trig(&x, &t, 0, 1, 999);
    ck("holding recirc: no re-trigger", fired == 0 && x.loop_end == 250);

    /* release both, then a fresh WRITE press starts a new record region */
    transport_update_trig(&x, &t, 0, 0, 999);          /* falling edges: no-op */
    transport_update_trig(&x, &t, 1, 0, 300);          /* new write press      */
    ck("re-press write -> WRITE, new start", transport_should_write(&x) && x.write_start == 300);

    /* another recirc press captures the new window */
    transport_update_trig(&x, &t, 0, 1, 420);
    ck("new loop window captured", x.loop_start == 300 && x.loop_end == 420);

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
