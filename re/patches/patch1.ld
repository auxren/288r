/* Linker script for Patch 1 code caves.
 * Places caves in free flash above the stock image (ends 0x08006D07).
 * Return addresses inside the read functions are absolute symbols so the caves'
 * `b.w ret_*` resolve at link time.
 *
 * Path A = sub_1968, Path B = sub_1c98 (structurally identical; different return
 * addresses only). Each path: caveA (floor+frac) + caveB (2-tap lerp). */

/* Path A (sub_1968) */
ret_after_A   = 0x08001ab6;   /* instr after caveA's replicated store        */
ret_after_B   = 0x08001aec;   /* the caller's str.w r3,[fp]                   */

/* Path B (sub_1c98) */
ret_after_A_B = 0x08001de2;   /* instr after caveA_B's replicated store       */
ret_after_B_B = 0x08001e18;   /* the caller's str.w r3,[fp]                   */

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
