/* freestanding.c — tiny runtime for a no-newlib (-nostdlib) build.
 *
 * This homebrew arm-none-eabi toolchain ships libgcc but NOT newlib, so we link
 * -nostdlib -lgcc and supply the handful of symbols the ST startup and the
 * compiler expect: an empty __libc_init_array (no C++ ctors), the mem* functions,
 * and their AEABI aliases (GCC may emit these for array/struct init).
 */
#include <stddef.h>
#include <stdint.h>

void __libc_init_array(void) { }

/* WORD-WISE when it can be (both ends 4-aligned and >= 4 bytes left).
 *
 * The byte loop this replaces was not merely slow in the abstract: -flto
 * inlined it into the audio ISR's delay-line cache fill, where it turned a
 * 96-word SDRAM read into 384 single-BYTE FMC transactions (see dl_cache.h).
 * Anything in a -nostdlib image may bind to this function, including the
 * compiler's own struct/array copies, so it has to be the fast one. */
void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst; const unsigned char *s = src;
    if (((((uintptr_t)d) | ((uintptr_t)s)) & 3u) == 0u) {
        uint32_t *dw = (uint32_t *)(void *)d;
        const uint32_t *sw = (const uint32_t *)(const void *)s;
        while (n >= 4u) { *dw++ = *sw++; n -= 4u; }
        d = (unsigned char *)dw; s = (const unsigned char *)sw;
    }
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst; const unsigned char *s = src;
    if (d < s) { while (n--) *d++ = *s++; }
    else { d += n; s += n; while (n--) *--d = *--s; }
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}

/* AEABI aliases. Note __aeabi_memset arg order is (dest, n, c). */
void __aeabi_memcpy(void *d, const void *s, size_t n)  { memcpy(d, s, n); }
void __aeabi_memcpy4(void *d, const void *s, size_t n) { memcpy(d, s, n); }
void __aeabi_memcpy8(void *d, const void *s, size_t n) { memcpy(d, s, n); }
void __aeabi_memmove(void *d, const void *s, size_t n)  { memmove(d, s, n); }
void __aeabi_memmove4(void *d, const void *s, size_t n) { memmove(d, s, n); }
void __aeabi_memmove8(void *d, const void *s, size_t n) { memmove(d, s, n); }
void __aeabi_memset(void *d, size_t n, int c)  { memset(d, c, n); }
void __aeabi_memset4(void *d, size_t n, int c) { memset(d, c, n); }
void __aeabi_memset8(void *d, size_t n, int c) { memset(d, c, n); }
void __aeabi_memclr(void *d, size_t n)  { memset(d, 0, n); }
void __aeabi_memclr4(void *d, size_t n) { memset(d, 0, n); }
void __aeabi_memclr8(void *d, size_t n) { memset(d, 0, n); }
