/* test_looper.c — the AUTO CONTROL state machine (src/looper.c), the logic
 * behind field issues #10/#13/#16. Covers the full transition matrix: first
 * capture arming, cycle-quantized store beg., signal-gated store end (auto)
 * vs hold-and-recall (manual), loop auto re-arm, both switch-movement resets,
 * manual punches from every state, the arm-jack pulse, and the LED intents.
 *
 * The engine runs against ordinary RAM; every "tick" advances the engine by
 * TICK samples (the hardware's ~5 ms panel tick) and then runs looper_tick.
 */
#include "looper.h"
#include <stdio.h>

static int fails = 0;
static void ck(const char *name, int cond) {
    printf("  %-52s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

#define LEN   48000u
#define TICK  480u          /* samples per panel tick (15 blocks x 32) */
#define CYC   4800.0f       /* base delay = cycle = 10 ticks           */
static float buf[LEN];

/* thresholds scaled like the board: fire > 0.02, re-arm < 0.008 */
#define REF   0.02f
#define HI    0.05f         /* "signal present"  */
#define LO    0.001f        /* "silence"         */

static engine_t E;
static looper_t L;

/* advance one panel tick: N engine samples, then the state machine */
static void tick(unsigned automode, unsigned store_end,
                 int wr, int rc, int arm, float sens)
{
    float chan[NUM_TAPS];
    for (unsigned i = 0; i < TICK; i++) engine_process_multi(&E, 0.1f, 0.5f, chan);
    looper_tick(&L, &E, automode, store_end, wr, rc, /*rc_lvl*/ rc, arm, sens);
}
/* tick with the recirc momentary HELD (level 1, no fresh edge) */
static void tick_hold(unsigned automode, unsigned store_end, float sens)
{
    float chan[NUM_TAPS];
    for (unsigned i = 0; i < TICK; i++) engine_process_multi(&E, 0.1f, 0.5f, chan);
    looper_tick(&L, &E, automode, store_end, 0, 0, /*rc_lvl*/ 1, 0, sens);
}
static void ticks(int n, unsigned am, unsigned se, float sens)
{
    for (int i = 0; i < n; i++) tick(am, se, 0, 0, 0, sens);
}

static void fresh(void)
{
    engine_init(&E, buf, LEN, CYC, 0.4f, 1.6f, 0.02f);
    looper_cfg_t cfg = { .sens_ref = REF, .arm_frac = 0.4f,
                         .release_ticks = 4, .release_samp = 4 * TICK,
                         .min_loop_samp = TICK, .delay_len = LEN,
                         .floor_win = 40, .onset_ratio = 2.0f };
    looper_init(&L, &cfg);
    tick(0, 0, 0, 0, 0, HI);   /* boot latch (no reset action); signal present
                                  so nothing arms before a scenario wants it */
}

int main(void)
{
    /* ---- first capture: "next sound" arming ---- */
    fresh();
    ck("boot: READY, ready LED", L.state == LP_READY && L.led_ready == 1);
    ticks(3, 0, 0, HI);        /* entered mid-signal: must NOT fire */
    ck("mid-signal entry holds READY", L.state == LP_READY);
    ticks(2, 0, 0, LO);        /* silence arms */
    ck("silence arms", L.armed == 1);
    tick(0, 0, 0, 0, 0, HI);   /* onset fires */
    ck("onset -> WRITE, auto take", L.state == LP_WRITE && L.take_auto == 1);
    /* the fire tick still shows READY's lamps (set in the READY branch, as in
     * the original inline machine); WRITE's lamps appear on the next tick */
    tick(0, 0, 0, 0, 0, HI);
    ck("WRITE: write LED, ready off", L.led_write == 1 && L.led_ready == 0);

    /* ---- store beg.: cycle-quantized capture, then LOOP ---- */
    ticks(12, 0, 0, HI);       /* > 10 ticks = one cycle */
    ck("store beg.: cap -> LOOP", L.state == LP_LOOP);
    ck("LOOP: recirc LED, ready EOC-owned",
       L.led_recirc == 1 && L.led_ready == -1);
    ck("loop window = one cycle",
       (E.xport.loop_end + LEN - E.xport.loop_start) % LEN == (uint32_t)CYC);

    /* ---- loop auto re-arm (#10) ---- */
    ticks(4, 0, 0, HI);        /* same sound continues: no re-trigger */
    ck("sustained sound holds LOOP", L.state == LP_LOOP);
    ticks(2, 0, 0, LO);
    tick(0, 0, 0, 0, 0, HI);
    ck("silence->onset punches new take (auto)",
       L.state == LP_WRITE && L.take_auto == 1);

    /* ---- manual write punch from LOOP + restart during WRITE ---- */
    ticks(12, 0, 0, HI);       /* back to LOOP */
    tick(0, 0, 1, 0, 0, HI);   /* wr momentary */
    ck("wr in LOOP -> manual take", L.state == LP_WRITE && L.take_auto == 0);
    uint32_t s0 = L.start;
    ticks(2, 0, 0, HI);
    tick(0, 0, 1, 0, 0, HI);   /* wr again: restart */
    ck("wr in WRITE restarts take", L.state == LP_WRITE && L.start != s0);
    tick(0, 0, 0, 1, 0, HI);   /* rc momentary: punch out */
    ck("rc in WRITE -> LOOP (manual window)", L.state == LP_LOOP);

    /* ---- arm-jack pulse fires regardless of arm state ---- */
    ticks(2, 0, 0, HI);        /* not armed (signal present) */
    tick(0, 0, 0, 0, 1, HI);
    ck("arm pulse punches from LOOP", L.state == LP_WRITE && L.take_auto == 1);

    /* ---- store end, auto take: signal-gated (#10) ---- */
    fresh();
    ticks(2, 0, 1, LO);        /* silence arms (store end selected) */
    tick(0, 1, 0, 0, 0, HI);   /* onset */
    ck("store end: onset -> WRITE", L.state == LP_WRITE && L.take_auto == 1);
    ticks(3, 0, 1, HI);        /* play 3 ticks (phrase) */
    ticks(4, 0, 1, LO);        /* 4 silent ticks = release */
    ck("silence punches out -> LOOP", L.state == LP_LOOP);
    {   /* hang is trimmed: the take spans 3 play + 4 silent ticks after the
         * onset tick's begin_take; the 4 release ticks are cut, leaving the
         * 3-tick phrase */
        uint32_t win = (E.xport.loop_end + LEN - E.xport.loop_start) % LEN;
        ck("release hang trimmed from loop", win == 3u * TICK);
    }

    /* ---- store end, auto take: NO cap — envelope is the master (#10 v2) --- */
    fresh();
    ticks(2, 0, 1, LO);
    tick(0, 1, 0, 0, 0, HI);
    ticks(16, 0, 1, HI);       /* sustained well past the cycle cap */
    ck("store end auto: signal present = keep WRITING", L.state == LP_WRITE);
    ticks(4, 0, 1, LO);        /* silence: loop the LAST phrase, cycle-bounded */
    ck("store end auto: silence -> LOOP", L.state == LP_LOOP);
    {
        uint32_t win = (E.xport.loop_end + LEN - E.xport.loop_start) % LEN;
        ck("rolling window bounded to one cycle", win == (uint32_t)CYC);
    }

    /* ---- min-statistics floor (#13): bleed absorbed, staccato untouched --- */
    fresh();
    ticks(2, 0, 0, LO); tick(0, 0, 0, 0, 0, HI); ticks(12, 0, 0, HI); /* LOOP */
    /* loop plays; the sens pickup hears CONTINUOUS playback bleed: within
     * one floor window the minimum IS the bleed -> reads as ambient */
    ticks(85, 0, 0, HI);              /* > 2 windows of steady bleed        */
    ck("steady bleed becomes the floor (arms)",
       L.armed == 1 && L.state == LP_LOOP && L.baseline >= 0.9f * HI);
    tick(0, 0, 0, 0, 0, 3.0f * HI);   /* a real onset pokes above the bleed */
    ck("onset above bleed re-triggers",
       L.state == LP_WRITE && L.take_auto == 1);
    /* a rising steady signal from READY is a legitimate onset (fires once);
     * once looping, the absorbed bleed must never re-fire */
    fresh();
    ticks(2, 0, 0, LO);
    tick(0, 0, 0, 0, 0, HI);
    ck("first onset fires (legitimate)", L.state == LP_WRITE);
    ticks(12, 0, 0, HI);              /* cap -> LOOP                        */
    uint32_t stable_start = L.start;
    ticks(85, 0, 0, HI);              /* bleed absorbed; steady level holds */
    ck("absorbed bleed cannot re-trigger",
       L.state == LP_LOOP && L.start == stable_start);
    /* input-A pot is OUT of the law (sidechain patch, parallel wiring) */
    fresh();
    ticks(2, 0, 0, LO);
    tick(0, 0, 0, 0, 0, HI);
    ck("capture is independent of input-A level",
       L.state == LP_WRITE && L.take_auto == 1);

    /* ---- THE STACCATO REGRESSION (#13): repeated hits keep triggering ----- */
    /* Field failure: the old one-pole baseline tracked the PLAYING and
     * ratcheted the threshold up until staccato couldn't trigger. With the
     * min-statistics floor, the gaps keep the floor at ambient and EVERY
     * hit after silence can fire. Simulate 12 hit/gap cycles in store beg.
     * with a short cycle; count auto take starts. */
    fresh();
    int fires = 0;
    for (int cyc_i = 0; cyc_i < 12; cyc_i++) {
        for (int k = 0; k < 4; k++) tick(0, 0, 0, 0, 0, LO);   /* gap  */
        uint8_t st_before = L.state;
        for (int k = 0; k < 12; k++) tick(0, 0, 0, 0, 0, HI);  /* hit + cap */
        if (st_before != LP_WRITE && L.take_auto == 1 &&
            (L.state == LP_WRITE || L.state == LP_LOOP)) fires++;
    }
    ck("staccato keeps triggering (>=10 of 12 hits)", fires >= 10);
    ck("floor stayed at ambient under staccato", L.baseline < 0.5f * HI);

    /* ---- store end, manual take: hold-and-recall ---- */
    fresh();
    tick(0, 1, 1, 0, 0, HI);   /* wr momentary starts a manual take */
    ck("manual take flagged", L.state == LP_WRITE && L.take_auto == 0);
    ticks(12, 0, 1, HI);       /* cap */
    ck("manual store end cap -> HOLD", L.state == LP_HOLD);
    ck("HOLD: write+ready together", L.led_write == 1 && L.led_ready == 1);
    ticks(3, 0, 1, HI);        /* signal present must NOT punch (not armed) */
    ck("HOLD survives sustained signal", L.state == LP_HOLD);
    uint32_t held_s = L.start, held_e = L.end;
    tick(0, 1, 0, 1, 0, HI);   /* rc recalls */
    ck("rc recalls the held window", L.state == LP_LOOP &&
       E.xport.loop_start == held_s && E.xport.loop_end == held_e);

    /* ---- HOLD auto re-arm: dip + onset starts a fresh (auto) take ---- */
    fresh();
    tick(0, 1, 1, 0, 0, HI);
    ticks(12, 0, 1, HI);       /* HOLD */
    ticks(2, 0, 1, LO);        /* dip arms */
    tick(0, 1, 0, 0, 0, HI);   /* onset */
    ck("HOLD re-arms into an auto take",
       L.state == LP_WRITE && L.take_auto == 1);

    /* ---- red-switch movement resets (#13) ---- */
    fresh();
    ticks(2, 0, 0, LO); tick(0, 0, 0, 0, 0, HI); ticks(12, 0, 0, HI); /* LOOP */
    tick(1, 0, 0, 0, 0, HI);   /* -> all sounds: loop released to write */
    ck("all-sounds entry releases loop",
       L.state == LP_READY && transport_should_write(&E.xport));
    ck("delay mode: write LED", L.led_write == 1 && L.led_recirc == 0);
    tick(0, 0, 0, 0, 0, HI);   /* back to center: READY armed... */
    tick(0, 0, 0, 0, 0, HI);   /* ...and the present signal fires */
    ck("looper re-entry captures a present signal", L.state == LP_WRITE);

    /* ---- store-selector movement resets (#16) ---- */
    fresh();
    ticks(2, 0, 0, LO); tick(0, 0, 0, 0, 0, HI); ticks(12, 0, 0, HI); /* LOOP */
    tick(0, 1, 0, 0, 0, HI);   /* flip store beg. -> store end: the reset
                                  arms READY and the present signal captures
                                  on the SAME tick (armed-on-entry semantics) */
    ck("store flip re-captures immediately (auto)",
       L.state == LP_WRITE && L.take_auto == 1);

    /* ---- next-sound flick: manual immediate trigger ---- */
    fresh();
    tick(2, 0, 0, 0, 0, HI);   /* automode 2 = flick side */
    ck("next-sound flick fires immediately (manual)",
       L.state == LP_WRITE && L.take_auto == 0);

    /* ---- delay mode manual recirc ---- */
    fresh();
    ticks(3, 1, 0, HI);        /* all sounds: continuous write */
    ck("all sounds writes continuously",
       L.state == LP_READY && transport_should_write(&E.xport));
    tick(1, 0, 0, 1, 0, HI);   /* rc momentary loops the cycle */
    ck("delay-mode rc -> loop", L.state == LP_LOOP &&
       !transport_should_write(&E.xport));
    tick(1, 0, 1, 0, 0, HI);   /* wr punches back in */
    ck("delay-mode wr -> write", transport_should_write(&E.xport));

    /* ---- OVERDUB: hold recirc in a playing loop ---------------------------- */
    fresh();
    ticks(2, 0, 0, LO); tick(0, 0, 0, 0, 0, HI); ticks(12, 0, 0, HI); /* LOOP */
    ck("pre-overdub: engine flag clear", E.od_active == 0);
    tick_hold(0, 0, HI);
    ck("hold recirc in LOOP -> overdub engaged",
       L.state == LP_LOOP && E.od_active == 1);
    ck("overdub LEDs: write+recirc together",
       L.led_write == 1 && L.led_recirc == 1);
    /* auto re-arm suspended: silence then a loud onset must NOT punch */
    L.min_cur = 0.0f; L.min_prev = 0.0f;   /* floor at ambient */
    tick_hold(0, 0, LO);
    tick_hold(0, 0, 3.0f * HI);
    ck("re-arm suspended while held", L.state == LP_LOOP);
    /* release: flag clears, re-arm law resumes */
    tick(0, 0, 0, 0, 0, LO);
    ck("release clears overdub", E.od_active == 0);
    tick(0, 0, 0, 0, 0, 3.0f * HI);
    ck("re-arm resumes after release", L.state == LP_WRITE);

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
