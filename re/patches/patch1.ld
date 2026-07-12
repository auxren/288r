/* Linker script for Patch 1 code cave.
 * Places the cave in free flash above the stock image (ends 0x08006D07).
 * Return addresses inside sub_1968 are provided as absolute symbols so the
 * cave's `b.w ret_after_*` resolve at link time. */

ret_after_A = 0x08001ab6;   /* instr after caveA's replicated store  */
ret_after_B = 0x08001aec;   /* the caller's  str.w r3,[fp]           */

MEMORY
{
    CAVE (rx) : ORIGIN = 0x08007000, LENGTH = 0x1000
}

SECTIONS
{
    .cave 0x08007000 :
    {
        KEEP(*(.text.cave))
        *(.text*)
    } > CAVE
}
