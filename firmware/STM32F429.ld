/* STM32F429 linker script for the 288r community firmware.
 *
 * Board brief (re/notes/hardware.md): MCU is STM32F429ZIT6 => FLASH 2M, RAM 192K+64K CCM
 * (VERIFY suffix with a loupe). SDRAM is ISSI 16-bit, density 8 or 32 MB (VERIFY — sets
 * LENGTH below and the max delay time). Sizes marked CONFIRM pending the bench check:
 *   - FLASH 2M (F429ZIT6).
 *   - RAM = 192K contiguous SRAM (0x20000000..0x2002FFFF) -> _estack 0x20030000,
 *     which matches the stock firmware's initial SP (a good cross-check).
 *   - CCM 64K at 0x10000000 (not DMA-accessible).
 *   - SDRAM at 0xC0000000; 8192K assumed (ISSI 16-bit) — CONFIRM density.
 */

_estack = 0x20030000;      /* top of 192K SRAM — matches stock SP */
_Min_Heap_Size  = 0x400;
_Min_Stack_Size = 0x800;

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 2048K   /* CONFIRM variant */
  RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 192K
  CCM   (xrw) : ORIGIN = 0x10000000, LENGTH = 64K
  SDRAM (xrw) : ORIGIN = 0xC0000000, LENGTH = 8192K   /* CONFIRM chip size */
}

SECTIONS
{
  .isr_vector : { KEEP(*(.isr_vector)) } >FLASH

  .text : {
    *(.text*) *(.rodata*)
    KEEP(*(.init)) KEEP(*(.fini))
    . = ALIGN(4); _etext = .;
  } >FLASH

  .ARM.extab : { *(.ARM.extab*) } >FLASH
  .ARM : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } >FLASH

  _sidata = LOADADDR(.data);
  .data : {
    . = ALIGN(4); _sdata = .;
    *(.data*)
    . = ALIGN(4); _edata = .;
  } >RAM AT>FLASH

  .bss : {
    . = ALIGN(4); _sbss = .; __bss_start__ = _sbss;
    *(.bss*) *(COMMON)
    . = ALIGN(4); _ebss = .; __bss_end__ = _ebss;
  } >RAM

  ._user_heap_stack : {
    . = ALIGN(8);
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  /* External SDRAM: the delay buffer. NOLOAD — not initialized from flash. */
  .sdram (NOLOAD) : {
    . = ALIGN(4);
    *(.sdram*)
    . = ALIGN(4);
  } >SDRAM

  /DISCARD/ : { libc.a(*) libm.a(*) libgcc.a(*) }
  .ARM.attributes 0 : { *(.ARM.attributes) }
}
