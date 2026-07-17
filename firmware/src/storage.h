/* storage.h — versioned/checksummed persistence records + control-pinning on recall.
 *
 * Mirrors the MARF 248r house style (DESIGN.md "Persistence & recall"). Every saved
 * block is { magic, version, crc16, payload }; on load we verify magic+version+CRC
 * and REFUSE invalid or wrong-version blocks (fall back to defaults, never memcpy
 * garbage). Backing-store-agnostic: this is pure serialization — the backend
 * (F429 internal-flash EEPROM emulation) reads/writes the byte blob and is a
 * separate [BENCH] hardware layer.
 *
 * Control pinning reconciles the 288r's LIVE-read trimmers with a recalled soft
 * value: after recall, the physical control is ignored until it sweeps THROUGH the
 * stored value, then live control resumes (no jump). Host-testable; no hardware.
 */
#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>
#include <stddef.h>

/* CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflect). crc16("123456789")=0x29B1. */
uint16_t crc16_ccitt(const void *data, size_t len);

/* On-wire record: [magic u32][version u16][length u16][crc16 u16][payload...],
 * all little-endian; CRC covers version+length+payload. */
#define STORAGE_MAGIC        0x32383872u   /* 'r','8','8','2' LE — fixed sentinel */
#define STORAGE_HEADER_BYTES 10u

/* Serialize payload into `out` (needs STORAGE_HEADER_BYTES + len). Returns total
 * bytes written. */
size_t storage_pack(void *out, uint16_t version, const void *payload, uint16_t len);

/* Validate `in` (in_len bytes) against magic + expect_version + CRC. On success,
 * copies up to max_len payload bytes into payload_out and returns the payload
 * length; returns -1 on ANY failure (short/magic/version/length/crc). */
int storage_load(const void *in, size_t in_len, uint16_t expect_version,
                 void *payload_out, uint16_t max_len);

/* ---- control pinning on recall -------------------------------------------- */
typedef struct {
    float   value;     /* value to USE (stored while pinned, else live)         */
    float   target;    /* stored value the live control must cross to unpin     */
    float   last_live; /* previous live reading (crossing detection)            */
    uint8_t pinned;    /* 1 = using stored value, waiting for the sweep         */
    uint8_t have_last; /* 1 once we have a previous live reading                */
} ctrl_pin_t;

void  pin_recall(ctrl_pin_t *p, float stored);   /* pin to a recalled value */
void  pin_free(ctrl_pin_t *p, float live);        /* no pin: follow live now */
int   pin_is_pinned(const ctrl_pin_t *p);

/* Feed the current live control reading; returns the value to use. Unpins the
 * moment the live control crosses (straddles/touches) the stored target. */
float pin_update(ctrl_pin_t *p, float live);

#endif /* STORAGE_H */
