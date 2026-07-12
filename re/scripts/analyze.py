#!/usr/bin/env python3
"""
288r firmware analyzer (STM32F42x/F43x, Cortex-M4F, Thumb-2).
Black-box static analysis of B288-REV1.0.bin loaded at 0x08000000.

Produces:
  - resolved PC-relative literal-pool loads  (ldr rX,[pc,#imm] -> constant)
  - function prologue candidates             (push {...,lr})
  - per-function tags for peripherals / SDRAM the function references
The goal: locate the delay engine (SDRAM @ 0xC0000000), the audio path
(SAI2 @ 0x40015800 + DMA2), and the delay-time control path (ADC).
"""
import struct, sys
from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_MCLASS
from capstone.arm import ARM_OP_IMM, ARM_OP_MEM, ARM_REG_PC

BASE = 0x08000000
BIN  = sys.argv[1] if len(sys.argv) > 1 else "re/B288-REV1.0.bin"
data = open(BIN, "rb").read()
END  = BASE + len(data)

def rd32(addr):
    off = addr - BASE
    if 0 <= off <= len(data) - 4:
        return struct.unpack_from("<I", data, off)[0]
    return None

# Peripheral / memory map fingerprints (STM32F42x/F43x)
REGIONS = [
    (0xC0000000, 0xD0000000, "SDRAM (delay buffer)"),
    (0xD0000000, 0xE0000000, "SDRAM bank2"),
    (0xA0000000, 0xA0001000, "FMC ctrl regs"),
    (0x40015800, 0x40015C00, "SAI2 (codec)"),
    (0x40015400, 0x40015800, "SAI1"),
    (0x40012000, 0x40012400, "ADC1/2/3"),
    (0x40026400, 0x40026800, "DMA2"),
    (0x40026000, 0x40026400, "DMA1"),
    (0x40023800, 0x40023C00, "RCC"),
    (0x40010000, 0x40010400, "TIM1"),
    (0x40000000, 0x40000400, "TIM2"),
    (0x40000400, 0x40000800, "TIM3"),
    (0x40003800, 0x40003C00, "SPI2/I2S2"),
    (0x40011000, 0x40011400, "USART1"),
    (0x40020000, 0x40021C00, "GPIOA-G"),
    (0x50000000, 0x50060000, "USB-OTG"),
    (0xE000E000, 0xE000F000, "SysTick/NVIC/SCB"),
]
def region(v):
    if v is None: return None
    for lo, hi, n in REGIONS:
        if lo <= v < hi: return n
    return None

md = Cs(CS_ARCH_ARM, CS_MODE_THUMB + CS_MODE_MCLASS)
md.detail = True

# ---- Vector table: system + external IRQ handlers ----
sp    = rd32(0x08000000)
reset = rd32(0x08000004)
irqs  = {}
for i in range(16, 106):
    v = rd32(0x08000000 + i*4)
    if v and (v & 1) and BASE <= (v & ~1) < END:
        irqs[i] = v & ~1

# ---- Linear sweep over the whole image ----
literals = {}     # addr -> (const, region)
prologues = []     # function-start candidates (push {...,lr})
code = md.disasm(data, BASE)
insns = list(code)

for ins in insns:
    # PC-relative literal loads: ldr rX,[pc,#imm]
    if ins.mnemonic.startswith("ldr"):
        for op in ins.operands:
            if op.type == ARM_OP_MEM and op.mem.base == ARM_REG_PC:
                pc = (ins.address + 4) & ~3
                tgt = pc + op.mem.disp
                val = rd32(tgt)
                if val is not None:
                    literals[ins.address] = (val, region(val))
    if ins.mnemonic == "push" and "lr" in ins.op_str:
        prologues.append(ins.address)

# ---- Bucket literal loads by the function they live in ----
prologues_sorted = sorted(prologues)
def func_of(addr):
    import bisect
    i = bisect.bisect_right(prologues_sorted, addr) - 1
    return prologues_sorted[i] if i >= 0 else None

func_tags = {}
for laddr, (val, reg) in literals.items():
    if reg:
        f = func_of(laddr)
        func_tags.setdefault(f, {}).setdefault(reg, set()).add(val)

# ---- Report ----
print(f"# 288r firmware map  ({len(data)} bytes @ {hex(BASE)})")
print(f"Initial SP = {hex(sp)}   Reset = {hex(reset)}   functions(prologues)={len(prologues)}")
print(f"External IRQ handlers wired: {len(irqs)}")
print()
print("## Functions that touch peripherals / SDRAM (candidate engine/driver code)")
order = ["SDRAM (delay buffer)","SDRAM bank2","SAI2 (codec)","SAI1","DMA2","DMA1",
         "ADC1/2/3","TIM1","TIM3","TIM2","FMC ctrl regs","RCC","USART1","USB-OTG",
         "SPI2/I2S2","GPIOA-G","SysTick/NVIC/SCB"]
for f in sorted(func_tags):
    tags = func_tags[f]
    keys = [k for k in order if k in tags]
    if not any(k.startswith("SDRAM") or k in ("SAI2 (codec)","ADC1/2/3","DMA2","TIM1","TIM3","FMC ctrl regs") for k in keys):
        continue
    desc = "  ".join(f"{k}={sorted(hex(x) for x in tags[k])}" for k in keys)
    print(f"  func {hex(f)}:  {desc}")
