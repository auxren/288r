@ =============================================================================
@ Patch 1 - Fractional (linear-interpolated) delay tap for the 288r read path.
@
@ Target: STM32F429, Cortex-M4F (FPv4-SP, single precision).  NOTE: Cortex-M4
@ has NO VRINT* family, so we use VCVT.S32.F32 (round-toward-zero == floor for
@ dist >= 0), exactly as the stock code does.
@
@ Two code caves, reached by b.w detours spliced into read fn sub_1968:
@   - caveA replaces the truncating  vcvt.s32.f32 s15,s15  @ 0x08001aa6
@       computes i0 = (int)dist and frac = dist - i0, stores read_ptr0,
@       leaves frac in s14 (survives the integer wrap logic), restores the
@       Z flag that the following `beq` depends on, returns to 0x08001ab6.
@   - caveB replaces the single fetch ldr.w r3,[r3,r2,lsl#2] @ 0x08001ae8
@       fetches bank[i0] and bank[i0-1] (wrapped by buffer length) and returns
@       out = s0*(1-frac) + s1*frac  (== s0 + (s1-s0)*frac) to 0x08001aec.
@
@ Register state assumed at each entry (from disassembly of sub_1968):
@   caveA: s15=dist(float>=0), r2=write_ptr, r0=mode&0xfb (preserve),
@          r9(sb)=&delay_read_offset(0x200000d4).  scratch: r3, s13, s14.
@   caveB: r3=delay_ram_bank_A base, r2=read_ptr0(wrapped), s14=frac.
@          r11(fp)=&output scratch(0x20002084) [used by caller's str].
@          scratch: r0, r1, r12, s11, s12.  result -> r3.
@ =============================================================================
        .syntax unified
        .cpu    cortex-m4
        .fpu    fpv4-sp-d16
        .thumb

        .section .text.cave,"ax",%progbits

        .global caveA
        .thumb_func
caveA:
        vcvt.s32.f32 s13, s15        @ s13 = i0 = trunc(dist)  (floor, dist>=0)
        vcvt.f32.s32 s14, s13        @ s14 = (float)i0
        vsub.f32     s14, s15, s14   @ s14 = frac = dist - i0   (kept for caveB)
        vmov         r3,  s13        @ r3  = i0 (int)
        sub.w        r3,  r2, r3     @ r3  = write_ptr - i0 = read_ptr0
        str.w        r3,  [r9]       @ *delay_read_offset = read_ptr0
        cmp          r0,  #2         @ restore Z for the caller's `beq` @0x08001aba
        b.w          ret_after_A     @ -> 0x08001ab6

        .global caveB
        .thumb_func
caveB:
        ldr.w        r0,  [r3, r2, lsl #2]   @ r0 = s0 = bank[i0]
        subs         r1,  r2, #1             @ r1 = i0 - 1
        bpl          1f
        movw         r12, #0x0000            @ r12 = 0x20000000 (&delay_ram_length)
        movt         r12, #0x2000
        ldr          r12, [r12]              @ r12 = length
        add          r1,  r1, r12            @ wrap neighbour: (i0-1)+length
1:
        ldr.w        r1,  [r3, r1, lsl #2]   @ r1 = s1 = bank[i0-1]
        vmov         s12, r0                 @ s12 = s0 bits
        vmov         s11, r1                 @ s11 = s1 bits
        vcvt.f32.s32 s12, s12                @ (float)s0
        vcvt.f32.s32 s11, s11                @ (float)s1
        vsub.f32     s11, s11, s12           @ s1 - s0
        vfma.f32     s12, s11, s14           @ s0 + (s1-s0)*frac
        vcvt.s32.f32 s12, s12                @ -> int
        vmov         r3,  s12                @ result sample -> r3
        b.w          ret_after_B             @ -> 0x08001aec

        .end
