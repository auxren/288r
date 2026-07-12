#!/usr/bin/env python3
"""
Apply Patch 1 (fractional interpolated delay tap) to B288-REV1.0.bin.

Steps:
  1. assemble+link re/patches/patch1_interp.s -> cave bytes @0x08007000, symbols
  2. verify the two anchor instructions in sub_1968 are what we expect
  3. splice: place cave, write `b.w caveA/caveB` over the anchors
  4. emit patched.bin + patched.hex
  5. re-disassemble anchors + cave with capstone and print for review

Run:  re/.venv/bin/python re/scripts/apply_patch1.py
Requires: arm-none-eabi-gcc/ld/nm/objcopy on PATH; capstone in the venv.
"""
import struct, subprocess, sys, os, tempfile
from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_MCLASS

BASE      = 0x08000000
CAVE_ADDR = 0x08007000

# (anchor addr, cave symbol, expected mnemonic, expected op substring)
# Path A = sub_1968, Path B = sub_1c98; each: truncate (caveA*) + main fetch +
# mode==6 bank_B fetch (both fetches -> caveB*).
ANCHORS = [
    (0x08001aa6, "caveA",   "vcvt.s32.f32", "s15,s15"),   # A: truncate dist
    (0x08001ae8, "caveB",   "ldr.w",        "lsl#2"),      # A: main tap fetch
    (0x08001b78, "caveB",   "ldr.w",        "lsl#2"),      # A: mode==6 bank_B fetch
    (0x08001dd2, "caveA_B", "vcvt.s32.f32", "s15,s15"),   # B: truncate dist
    (0x08001e14, "caveB_B", "ldr.w",        "lsl#2"),      # B: main tap fetch
    (0x08001ea4, "caveB_B", "ldr.w",        "lsl#2"),      # B: mode==6 bank_B fetch
]

ROOT   = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BIN    = os.path.join(ROOT, "re", "B288-REV1.0.bin")
SRC    = os.path.join(ROOT, "re", "patches", "patch1_interp.s")
LD     = os.path.join(ROOT, "re", "patches", "patch1.ld")
OUTBIN = os.path.join(ROOT, "re", "patches", "patched.bin")
OUTHEX = os.path.join(ROOT, "re", "patches", "patched.hex")

md = Cs(CS_ARCH_ARM, CS_MODE_THUMB + CS_MODE_MCLASS); md.detail = True

def sh(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode: sys.exit(f"cmd failed: {' '.join(cmd)}\n{r.stderr}")
    return r.stdout

def encode_bw(pc, target):
    """Thumb-2 unconditional B.W (T4). pc = address of the branch instruction."""
    off = target - (pc + 4)
    if not (-(1 << 24) <= off < (1 << 24)):
        sys.exit(f"b.w out of range: {hex(pc)}->{hex(target)}")
    off &= 0x1FFFFFF
    S     = (off >> 24) & 1
    I1    = (off >> 23) & 1
    I2    = (off >> 22) & 1
    imm10 = (off >> 12) & 0x3FF
    imm11 = (off >> 1)  & 0x7FF
    J1 = ((~I1) & 1) ^ S
    J2 = ((~I2) & 1) ^ S
    hw1 = 0xF000 | (S << 10) | imm10
    hw2 = 0x9000 | (J1 << 13) | (J2 << 11) | imm11
    return struct.pack("<HH", hw1, hw2)

def dis1(data, addr):
    off = addr - BASE
    return next(md.disasm(data[off:off+4], addr))

# --- 1. assemble + link the cave ------------------------------------------
with tempfile.TemporaryDirectory() as td:
    elf = os.path.join(td, "patch1.elf")
    cav = os.path.join(td, "cave.bin")
    sh(["arm-none-eabi-gcc", "-mcpu=cortex-m4", "-mthumb", "-mfpu=fpv4-sp-d16",
        "-mfloat-abi=hard", "-nostdlib", "-Wl,-T," + LD, SRC, "-o", elf])
    sh(["arm-none-eabi-objcopy", "-O", "binary", "--only-section=.cave", elf, cav])
    cave = open(cav, "rb").read()
    nm = sh(["arm-none-eabi-nm", elf])
    syms = {}
    for line in nm.splitlines():
        p = line.split()
        if len(p) == 3: syms[p[2]] = int(p[0], 16)

print(f"cave: {len(cave)} bytes @ {hex(CAVE_ADDR)}  " +
      "  ".join(f"{s}={hex(syms[s])}" for s in ("caveA","caveB","caveA_B","caveB_B")))

# --- 2. verify anchors -----------------------------------------------------
data = bytearray(open(BIN, "rb").read())
for addr, sym, mnem, opsub in ANCHORS:
    ins = dis1(data, addr)
    print(f"anchor {hex(addr)} -> {sym:8s}: {ins.mnemonic} {ins.op_str}")
    assert ins.mnemonic == mnem and opsub in ins.op_str.replace(" ",""), f"anchor {hex(addr)} mismatch!"
    assert ins.size == 4, f"anchor {hex(addr)} must be 4-byte (room for b.w)"

# --- 3. splice -------------------------------------------------------------
img = bytearray(data)
# extend image up to cave and drop the cave in
if len(img) < (CAVE_ADDR - BASE):
    img += b"\xFF" * ((CAVE_ADDR - BASE) - len(img))
img[CAVE_ADDR-BASE : CAVE_ADDR-BASE+len(cave)] = cave
# write all detours
for addr, sym, _, _ in ANCHORS:
    img[addr-BASE : addr-BASE+4] = encode_bw(addr, syms[sym])

open(OUTBIN, "wb").write(img)
sh(["arm-none-eabi-objcopy", "-I", "binary", "-O", "ihex",
    "--change-addresses", hex(BASE), OUTBIN, OUTHEX])
print(f"wrote {OUTBIN} ({len(img)} bytes) and {OUTHEX}")

# --- 4. re-disassemble for review -----------------------------------------
print("\n=== detours in place ===")
for a, sym, _, _ in ANCHORS:
    i = dis1(img, a); print(f"  {hex(a)} -> {sym:8s}: {i.mnemonic} {i.op_str}")
print("\n=== cave disassembly ===")
for i in md.disasm(bytes(img[CAVE_ADDR-BASE:CAVE_ADDR-BASE+len(cave)]), CAVE_ADDR):
    print(f"  {hex(i.address)}: {i.mnemonic:14s} {i.op_str}")
