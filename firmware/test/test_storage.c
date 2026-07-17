/* test_storage.c — persistence records (CRC/version/validation) + control pinning
 * (see src/storage.c). Pure logic, no hardware. Built by `make test`.
 */
#include "storage.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
static void ck(const char *name, int cond) {
    printf("  %-48s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

int main(void)
{
    /* ---- CRC-16/CCITT-FALSE known vector ---- */
    ck("crc16(\"123456789\") == 0x29B1", crc16_ccitt("123456789", 9) == 0x29B1u);

    /* ---- pack / load round-trip ---- */
    uint8_t payload[16] = { 1, 2, 3, 4, 250, 128, 0, 99, 7, 7, 7, 7, 42, 0, 255, 16 };
    uint8_t rec[64];
    size_t total = storage_pack(rec, /*version*/ 3, payload, sizeof payload);
    ck("packed size = header + payload", total == STORAGE_HEADER_BYTES + sizeof payload);

    uint8_t out[16] = {0};
    int n = storage_load(rec, total, /*expect*/ 3, out, sizeof out);
    ck("load returns payload length", n == (int)sizeof payload);
    ck("payload round-trips intact", memcmp(out, payload, sizeof payload) == 0);

    /* ---- rejections: never copy garbage ---- */
    { uint8_t bad[64]; memcpy(bad, rec, total); bad[0] ^= 0xFF;   /* corrupt magic */
      ck("reject bad magic", storage_load(bad, total, 3, out, sizeof out) == -1); }
    { uint8_t bad[64]; memcpy(bad, rec, total); bad[STORAGE_HEADER_BYTES + 2] ^= 0x01; /* corrupt payload */
      ck("reject bad CRC (flipped payload bit)", storage_load(bad, total, 3, out, sizeof out) == -1); }
    ck("reject wrong version", storage_load(rec, total, /*expect*/ 4, out, sizeof out) == -1);
    ck("reject payload > max_len", storage_load(rec, total, 3, out, /*max*/ 8) == -1);
    ck("reject truncated buffer", storage_load(rec, STORAGE_HEADER_BYTES - 1u, 3, out, sizeof out) == -1);

    /* a fresh/erased blob (all 0xFF) must be refused, not trusted */
    { uint8_t erased[32]; memset(erased, 0xFF, sizeof erased);
      ck("reject erased/blank block", storage_load(erased, sizeof erased, 3, out, sizeof out) == -1); }

    /* ---- control pinning: approach the stored value from below ---- */
    ctrl_pin_t p;
    pin_recall(&p, 0.5f);
    ck("recall: pinned, uses stored", pin_is_pinned(&p) && pin_update(&p, 0.20f) == 0.5f);
    ck("still below: holds stored",   pin_update(&p, 0.40f) == 0.5f && pin_is_pinned(&p));
    ck("crosses target: unpins to live", pin_update(&p, 0.60f) == 0.60f && !pin_is_pinned(&p));
    ck("after unpin: follows live",   pin_update(&p, 0.90f) == 0.90f);

    /* approach from above */
    pin_recall(&p, 0.5f);
    ck("from above: holds stored",    pin_update(&p, 0.90f) == 0.5f && pin_is_pinned(&p));
    ck("from above crosses: unpins",  pin_update(&p, 0.30f) == 0.30f && !pin_is_pinned(&p));

    /* already sitting on the stored value -> unpin immediately */
    pin_recall(&p, 0.5f);
    ck("already at target: unpins now", pin_update(&p, 0.5f) == 0.5f && !pin_is_pinned(&p));

    /* free-running (no recall) just follows live */
    pin_free(&p, 0.1f);
    ck("free: not pinned, follows live", !pin_is_pinned(&p) && pin_update(&p, 0.77f) == 0.77f);

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
