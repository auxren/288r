/* STM32F429ZET6 linker script — 288r community firmware.
 *
 * Confirmed from the board (re/notes/hardware.md): FLASH 512K, RAM 192K + 64K CCM,
 * SDRAM = IS42S16400 8 MB @ 0xC0000000.  _estack 0x20030000 matches the stock SP.
 *
 * Links against newlib-nano (--specs=nano.specs) + nosys stubs; the ST F429 startup
 * calls __libc_init_array, so libc is NOT discarded (unlike the MARF script).
 */
ENTRY(Reset_Handler)

_estack = 0x20030000;      /* top of 192K SRAM — matches stock SP */
_Min_Heap_Size  = 0x400;
_Min_Stack_Size = 0x800;

MEMORY
{
  /* image capped at 384K: the TOP sector (7, 128K @ 0x08060000) is RESERVED
     for preset/calibration persistence — presets MUST survive firmware
     updates (owner hard requirement). Growing past this cap is a link error,
     never a silent overlap with the store. */
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 384K
  RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 192K
  CCM   (xrw) : ORIGIN = 0x10000000, LENGTH = 64K
  SDRAM (xrw) : ORIGIN = 0xD0000000, LENGTH = 8192K
}

SECTIONS
{
  .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) . = ALIGN(4); } >FLASH

  .text : {
    . = ALIGN(4);
    *(.text) *(.text*)
    *(.glue_7) *(.glue_7t) *(.eh_frame)
    KEEP(*(.init)) KEEP(*(.fini))
    . = ALIGN(4); _etext = .;
  } >FLASH

  .rodata : { . = ALIGN(4); *(.rodata) *(.rodata*) . = ALIGN(4); } >FLASH

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
  .ARM : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } >FLASH

  .preinit_array : {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } >FLASH
  .init_array : {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
  } >FLASH
  .fini_array : {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } >FLASH

  _sidata = LOADADDR(.data);
  .data : {
    . = ALIGN(4); _sdata = .;
    *(.data) *(.data*)
    . = ALIGN(4); _edata = .;
  } >RAM AT>FLASH

  .bss : {
    . = ALIGN(4); _sbss = .; __bss_start__ = _sbss;
    *(.bss) *(.bss*) *(COMMON)
    . = ALIGN(4); _ebss = .; __bss_end__ = _ebss;
  } >RAM

  ._user_heap_stack : {
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  /* CCM: fast core-coupled RAM (NOT DMA-accessible). NOLOAD for now — hot engine
   * state / prefetch scratch move here in the block-processing rework. */
  .ccmram (NOLOAD) : { . = ALIGN(4); _sccm = .; *(.ccmram) *(.ccmram*) . = ALIGN(4); _eccm = .; } >CCM

  /* External SDRAM: the delay buffer. NOLOAD — cleared at runtime by dl_clear(). */
  .sdram (NOLOAD) : { . = ALIGN(4); *(.sdram*) . = ALIGN(4); } >SDRAM

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
