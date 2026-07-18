/* math.h — freestanding compat shim for the bare-metal (STM32F429) build.
 *
 * The firmware links -nostdlib and this arm-none-eabi toolchain ships no newlib
 * headers, so <math.h> does not exist for the target. The engine is otherwise
 * self-contained (no libm), but pitch_shift.c legitimately needs single-precision
 * sinf/cosf/fabsf. On the target we declare exactly those (definitions come from
 * the firmware's own single-precision math at link time — a sin^2 table is the
 * intended hot-loop form, see PITCH_SHIFT.md). On a hosted build (-Isrc puts this
 * ahead of the system header) we simply forward to the real <math.h>, so host
 * unit tests keep the full library.
 */
#if defined(__arm__) || defined(__ARM_EABI__)
  #ifndef SRC_FREESTANDING_MATH_H
  #define SRC_FREESTANDING_MATH_H
  float sinf(float);
  float cosf(float);
  float fabsf(float);
  #endif
#else
  #include_next <math.h>
#endif
