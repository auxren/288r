/* STM32F429 linker script for the 288r community firmware.
 *
 * Confirmed from the board (re/notes/hardware.md): MCU = STM32F429Z**E**T6 => FLASH 512K,
 * RAM 192K + 64K CCM. SDRAM = ISSI IS42S16400J-7TLI => 8 MB (4M x 16), 16-bit @ 0xC0000000.
 *   - FLASH 512K (F429ZET6 — confirmed from chip marking).
 *   - RAM = 192K contiguous SRAM (0x20000000..0x2002FFFF) -> _estack 0x20030000,
 *     which matches the stock firmware's initial SP (a good cross-check).
 *   - CCM 64K at 0x10000000 (not DMA-accessible).
 *   - SDRAM at 0xC0000000, 8 MB.
 */

_estack = 0x20030000;      /* top of 192K SRAM — matches stock SP */
_Min_Heap_Size  = 0x400;
_Min_Stack_Size = 0x800;

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K    /* F429ZET6 — confirmed */
  RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 192K
  CCM   (xrw) : ORIGIN = 0x10000000, LENGTH = 64K
  SDRAM (xrw) : ORIGIN = 0xC0000000, LENGTH = 8192K   /* IS42S16400 8 MB — confirmed */
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
