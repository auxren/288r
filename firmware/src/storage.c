/* storage.c — see storage.h. Pure serialization + pinning; no hardware, no libm. */
#include "storage.h"

uint16_t crc16_ccitt(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)p[i] << 8);
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
    }
    return crc;
}

/* little-endian field access (portable, avoids struct padding/alignment) */
static void put_u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint16_t get_u16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t get_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Layout: [magic u32][crc16 u16][version u16][length u16][payload]. CRC comes
 * BEFORE version/length/payload so those three are one contiguous CRC'd region
 * (offset 6 .. 10+len); the CRC never covers itself. */
size_t storage_pack(void *out, uint16_t version, const void *payload, uint16_t len)
{
    uint8_t *o = (uint8_t *)out;
    const uint8_t *pl = (const uint8_t *)payload;
    put_u32(o + 0, STORAGE_MAGIC);
    put_u16(o + 6, version);
    put_u16(o + 8, len);
    for (uint16_t i = 0; i < len; i++) o[STORAGE_HEADER_BYTES + i] = pl[i];
    uint16_t crc = crc16_ccitt(o + 6, (size_t)(4u + len));   /* version+length+payload */
    put_u16(o + 4, crc);
    return (size_t)STORAGE_HEADER_BYTES + len;
}

int storage_load(const void *in, size_t in_len, uint16_t expect_version,
                 void *payload_out, uint16_t max_len)
{
    const uint8_t *i = (const uint8_t *)in;
    if (in_len < STORAGE_HEADER_BYTES)               return -1;
    if (get_u32(i + 0) != STORAGE_MAGIC)             return -1;
    if (get_u16(i + 6) != expect_version)            return -1;
    uint16_t len = get_u16(i + 8);
    if (len > max_len)                               return -1;
    if ((size_t)STORAGE_HEADER_BYTES + len > in_len) return -1;
    uint16_t crc_stored = get_u16(i + 4);
    uint16_t crc_calc   = crc16_ccitt(i + 6, (size_t)(4u + len));
    if (crc_calc != crc_stored)                      return -1;
    uint8_t *o = (uint8_t *)payload_out;
    for (uint16_t k = 0; k < len; k++) o[k] = i[STORAGE_HEADER_BYTES + k];
    return (int)len;
}

/* ---- control pinning ------------------------------------------------------ */

static int straddles(float a, float b, float t)
{
    return (a <= t && b >= t) || (a >= t && b <= t);   /* t between a and b (incl.) */
}

void pin_recall(ctrl_pin_t *p, float stored)
{
    p->value = stored;
    p->target = stored;
    p->last_live = 0.0f;
    p->pinned = 1;
    p->have_last = 0;
}

void pin_free(ctrl_pin_t *p, float live)
{
    p->value = live;
    p->target = live;
    p->last_live = live;
    p->pinned = 0;
    p->have_last = 1;
}

int pin_is_pinned(const ctrl_pin_t *p) { return p->pinned != 0; }

float pin_update(ctrl_pin_t *p, float live)
{
    if (!p->pinned) { p->value = live; p->last_live = live; return live; }

    /* catch-band: values saved at a control's endpoint may be unreachable
     * exactly (ADC never quite hits full scale) — near counts as through. */
    float d = live - p->target;
    if (d < 0.0f) d = -d;
    int crossed = (d < 0.02f);
    if (!crossed) {
        if (!p->have_last) { crossed = straddles(live, live, p->target); }
        else               { crossed = straddles(p->last_live, live, p->target); }
    }

    p->last_live = live;
    p->have_last = 1;

    if (crossed) { p->pinned = 0; p->value = live; }
    else         { p->value = p->target; }            /* hold the stored value */
    return p->value;
}
