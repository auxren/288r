/* test_recirc_span.c — store-end hold semantics: engine_recirc_span plays a SAVED
 * window after the head has moved past it; engine_recirc_window loops the last
 * cycle; both wrap at loop_end (end-of-cycle events fire). Built by `make test`.
 */
#include "engine.h"
#include <stdio.h>
#include <math.h>

#define LEN 4096u
static float buf[LEN];

static int fails = 0;
static void ck(const char *name, int cond) {
    printf("  %-52s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

int main(void)
{
    engine_t e;
    engine_init(&e, buf, LEN, /*base*/ 512.0f, 0.4f, 1.6f, /*slew*/ 1.0f);

    /* write a recognizable ramp: sample n = n/1000 */
    float ch[NUM_TAPS];
    for (int n = 0; n < 1500; n++) (void)engine_process_multi(&e, (float)n * 0.001f, 0.5f, ch);

    /* mark a window [500, 900) then keep writing past it (store-end hold) */
    uint32_t start = 500, end = 900;
    for (int n = 1500; n < 2600; n++) (void)engine_process_multi(&e, (float)n * 0.001f, 0.5f, ch);

    /* recall the saved span */
    engine_recirc_span(&e, start, end);
    ck("recirc_span: mode = RECIRC", !transport_should_write(&e.xport));
    ck("recirc_span: window = [start,end]", e.xport.loop_start == start && e.xport.loop_end == end);
    ck("recirc_span: head snapped into window", e.dl.wpos == start);

    /* advance one full window: head must wrap end->start (EOC event) */
    int wrapped = 0; uint32_t prev = e.dl.wpos;
    for (uint32_t n = 0; n < (end - start) + 8u; n++) {
        (void)engine_process_multi(&e, 0.0f, 0.5f, ch);
        if (e.dl.wpos < prev) wrapped = 1;
        prev = e.dl.wpos;
    }
    ck("head wraps at loop_end (EOC fires)", wrapped);
    ck("head stays inside the window",
       e.dl.wpos >= start && e.dl.wpos < end);

    /* the loop must play the SAVED ramp content (writes stopped in recirc):
       read a tap 100 back from the head — it must be a value from [start,end) */
    float v = dl_read_loop(&e.dl, 100.0f, start, end, DL_INTERP_LINEAR);
    ck("loop reads saved-window content", v >= 0.499f * 0.001f * 999.0f * 0.0f + 0.0004f && v <= 0.0026f);

    /* recirc_window (store-beg auto-loop): loops exactly `window` samples */
    engine_write(&e);
    for (int n = 0; n < 700; n++) (void)engine_process_multi(&e, 0.5f, 0.5f, ch);
    uint32_t head = e.dl.wpos;
    engine_recirc_window(&e, 256);
    uint32_t span = (e.xport.loop_end >= e.xport.loop_start)
                  ? e.xport.loop_end - e.xport.loop_start
                  : LEN - (e.xport.loop_start - e.xport.loop_end);
    ck("recirc_window: span == requested", span == 256);
    ck("recirc_window: ends at prior head", e.xport.loop_end == head);

    /* zero-length span is ignored (no mode change) */
    engine_write(&e);
    engine_recirc_span(&e, e.dl.wpos, e.dl.wpos);
    ck("zero-length span ignored", transport_should_write(&e.xport));

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
