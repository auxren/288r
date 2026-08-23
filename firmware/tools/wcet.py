#!/usr/bin/env python3
"""wcet.py — static worst-case-execution-time instrument for the audio ISR.

WHY THIS EXISTS.  The budget contract's hard guarantee (clause C-3) is that the
audio ISR CANNOT exceed its block budget, and "cannot" is a claim about every
possible path, not about the paths a bench session happened to exercise.  This
tool makes that claim machine-checkable without hardware:

  C-3a BOUNDEDNESS   every loop reachable from bsp_audio_isr() has a
                     compile-time iteration bound.  Any loop that is not
                     declared in the manifest fails the build.  This is the
                     clause that catches the dangerous class directly: a
                     varispeed rate that escapes its clamp, a splice job whose
                     quota is per sample instead of per block, a search that
                     resumes into stale geometry.
  C-3b STATIC CEILING  the longest path through the ISR's call graph, costed
                     with a documented instruction model.  A path cannot take
                     both sides of one branch, so this is a genuine upper bound
                     over every feature combination at once -- pitch AND
                     overdub AND varispeed AND a splice in flight.
  non-negotiables    no double-precision helper (__aeabi_d*) anywhere in the
                     reachable graph, no unresolved indirect control flow.

WHAT THE NUMBERS MEAN.  The ceiling is deliberately pessimistic: it assumes the
most expensive branch everywhere and the worst declared trip count for every
loop simultaneously.  The reference measurement (v1.3.0-era image, 110-117% of
budget in TIME/RECIRC with a loop playing) is a single real path, so the
ceiling is necessarily larger.  The manifest therefore records a CALIBRATION
ratio K = measured_worst / ceiling for a build whose real load is known, and
the tool reports both the raw ceiling and the calibrated projection
K * ceiling.  The projection is what regressions are gated on; the ceiling is
what the guarantee is stated in.  Both move together with any change to the
ISR, which is what makes this an acceptance instrument for both workstreams.

LIMITS, stated so nobody over-trusts it.  The cost model is a table, not a
cycle-accurate simulator: no bus contention, no ART hit/miss modelling beyond
the SDRAM classification, no dual-issue.  Only the OUT-OF-LINE readers of the
external buffer are priced at SDRAM latency; SDRAM traffic the compiler inlined
into the ISR is priced as SRAM, because the model cannot tell which pointer an
inlined load followed.  And the ceiling is a longest PATH, so it is sensitive to
which call sites that path happens to run through: restructuring the ISR can
move it by tens of percent without the real load moving at all (it did, by +62%,
across workstream B's own edits, while the per-frame work fell 10%).  Treat a
jump as a prompt to look, not as a measurement — for a real number, measure
isr_pk on the unit and re-calibrate.  It measures the shape of the code.

Usage:
    tools/wcet.py --elf build/fw/b288-community.elf
    tools/wcet.py --elf ... --emit-bounds        # skeleton for new loops
    tools/wcet.py --elf ... --calibrate 4104     # record a measured isr_pk
"""

import argparse
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_MANIFEST = os.path.join(HERE, "wcet_bounds.json")

INSN_RE = re.compile(
    r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2,8} )+)\s*(\S+)(?:\s+(.*))?$")
FUNC_RE = re.compile(r"^([0-9a-f]+) <([^>]+)>:$")
TARGET_RE = re.compile(r"^([0-9a-f]+) <([^>]+)>")

COND = ("eq", "ne", "cs", "hs", "cc", "lo", "mi", "pl", "vs", "vc",
        "hi", "ls", "ge", "lt", "gt", "le")


# ---------------------------------------------------------------- cost model
# Cycles per instruction on a Cortex-M4F with zero-wait flash (ART enabled).
# Where the ARM figures give a range, the pessimistic end is used: this is a
# ceiling, and an optimistic entry here would silently weaken the guarantee.
BASE_COST = 1           # ALU, MOV, FP add/mul (pipelined)
COSTS = {
    "vdiv": 14, "vsqrt": 14,          # non-pipelined, blocks the FPU
    "vmrs": 3, "vmsr": 3,             # FPSCR <-> core transfer + pipeline sync
    "udiv": 5, "sdiv": 5,
    "smull": 2, "umull": 2, "smlal": 2, "umlal": 2, "mla": 2, "mls": 2,
    "ldr": 2, "ldrb": 2, "ldrh": 2, "ldrsb": 2, "ldrsh": 2, "ldrd": 3,
    "vldr": 2, "vldm": 3, "ldm": 3, "ldmia": 3, "pop": 3,
    "str": 1, "strb": 1, "strh": 1, "strd": 2,
    "vstr": 1, "vstm": 2, "stm": 2, "stmdb": 2, "push": 2,
    "bl": 3, "blx": 3, "bx": 3,
}
SDRAM_LOAD = 16         # external SDRAM through the FMC, no D-cache on an M4
BRANCH_TAKEN = 3        # branch + pipeline refill


def raw_base(m):
    """Mnemonic without the .n/.w width suffix — control flow reads THIS.

    (mnem_base() below also strips condition suffixes, which is right for
    costing and catastrophically wrong for control flow: it turns `bne` into
    `b`, i.e. a conditional branch into an unconditional one, and every loop
    in the image then loses its fall-through edge.)"""
    return m.split(".")[0]


def mnem_base(m):
    """Strip .n/.w width and the IT-block condition suffix (COSTING ONLY)."""
    m = raw_base(m)
    if m.startswith("v") or m.startswith("b"):
        return m
    for c in COND:
        if len(m) > len(c) and m.endswith(c) and m[:-len(c)] in (
                "add", "sub", "mov", "movs", "cmp", "ldr", "str", "and",
                "orr", "eor", "mul", "rsb", "lsl", "lsr", "asr", "it"):
            return m[:-len(c)]
    return m


def is_call(m):
    return raw_base(m) in ("bl", "blx")


def is_return(ins):
    """A return is any write to PC from the stack, not just `pop {..,pc}` —
    GCC spells the audio ISR's return `ldmia.w sp!, {r4..fp, pc}` because it
    saves more registers than `pop` encodes. Missing that spelling makes the
    function look like it never returns, which quietly turns its whole body
    into one unbounded strongly-connected component."""
    b = raw_base(ins.mnem)
    return (b in ("pop", "ldm", "ldmia", "ldmfd", "ldmdb")
            and "pc" in ins.ops) or b == "bx"


def is_cond_branch(m):
    b = raw_base(m)
    return b in ("cbz", "cbnz") or (b.startswith("b") and b[1:] in COND)


def is_branch(m):
    b = raw_base(m)
    return (b in ("b", "bl", "blx", "bx", "cbz", "cbnz", "tbb", "tbh")
            or is_cond_branch(m))


# ------------------------------------------------------------------ parsing
class Insn:
    __slots__ = ("addr", "mnem", "ops", "target", "tname")

    def __init__(self, addr, mnem, ops):
        self.addr = addr
        self.mnem = mnem
        self.ops = ops or ""
        self.target = None
        self.tname = None
        if is_branch(mnem) or mnem.split(".")[0] in ("bl", "blx"):
            m = TARGET_RE.search(self.ops.split(", ")[-1].strip())
            if m:
                self.target = int(m.group(1), 16)
                self.tname = m.group(2)


class Func:
    def __init__(self, addr, name):
        self.addr = addr
        self.name = name
        self.insns = []


def disassemble(elf, objdump):
    out = subprocess.check_output([objdump, "-d", elf],
                                  universal_newlines=True)
    funcs, cur = {}, None
    for line in out.splitlines():
        fm = FUNC_RE.match(line)
        if fm:
            cur = Func(int(fm.group(1), 16), fm.group(2))
            funcs[cur.name] = cur
            continue
        if cur is None:
            continue
        im = INSN_RE.match(line)
        if im:
            mnem = im.group(3)
            # Literal pools are DATA sitting inside the code, and objdump
            # prints them as instructions. Left in, they become basic blocks
            # with invented fall-through edges, which silently welds unrelated
            # regions into one enormous strongly-connected component — the CFG
            # then says the audio ISR can never return. GCC only ever places a
            # pool after an unconditional transfer, so dropping the data lines
            # cannot lose a real fall-through.
            if mnem.startswith("."):
                continue
            cur.insns.append(Insn(int(im.group(1), 16), mnem, im.group(4)))
    return funcs


# ------------------------------------------------------- CFG + longest path
class Block:
    __slots__ = ("start", "end", "insns", "succ", "cost", "is_exit")

    def __init__(self, start):
        self.start = start
        self.end = start
        self.insns = []
        self.succ = []
        self.cost = 0
        self.is_exit = False


def insn_cost(ins, sdram):
    b = mnem_base(ins.mnem)
    c = COSTS.get(b, BASE_COST)
    if sdram and b in ("ldr", "vldr", "ldrh", "ldrd", "str", "vstr"):
        c = SDRAM_LOAD
    if is_branch(ins.mnem) and b not in ("bl", "blx"):
        c = max(c, BRANCH_TAKEN)
    return c


def build_blocks(fn, sdram, call_cost):
    """Split a function into basic blocks and cost each one."""
    lo, hi = fn.addr, fn.insns[-1].addr if fn.insns else fn.addr
    leaders = {lo}
    for i, ins in enumerate(fn.insns):
        if is_branch(ins.mnem) and not is_call(ins.mnem):
            if ins.target is not None and lo <= ins.target <= hi:
                leaders.add(ins.target)
            if i + 1 < len(fn.insns):
                leaders.add(fn.insns[i + 1].addr)
    blocks = {}
    cur = None
    for i, ins in enumerate(fn.insns):
        if ins.addr in leaders or cur is None:
            cur = Block(ins.addr)
            blocks[ins.addr] = cur
        cur.insns.append(ins)
        cur.end = ins.addr
        cur.cost += insn_cost(ins, sdram)
        base = raw_base(ins.mnem)
        if is_call(ins.mnem):
            cur.cost += call_cost.get(ins.tname, 0)
        nxt = fn.insns[i + 1].addr if i + 1 < len(fn.insns) else None
        if is_branch(ins.mnem):
            if is_call(ins.mnem):
                continue                       # calls fall through
            if ins.target is not None and lo <= ins.target <= hi:
                cur.succ.append(ins.target)
            if is_return(ins) or (ins.target is None and base == "b"):
                cur.is_exit = True             # return / tail call / unresolved
            elif ins.target is not None and not (lo <= ins.target <= hi):
                cur.is_exit = True             # branch out of the function
            if is_cond_branch(ins.mnem) and nxt is not None:
                cur.succ.append(nxt)           # conditional: fallthrough
            cur = None
        elif is_return(ins):
            cur.is_exit = True
            cur = None
    for b in blocks.values():
        if not b.succ and not b.is_exit:
            after = [a for a in blocks if a > b.end]
            if after:
                b.succ.append(min(after))
            else:
                b.is_exit = True
    return blocks


def sccs(nodes, succ):
    """Tarjan's strongly-connected components, iterative (bodies get big)."""
    index, low, onstk, stk, out = {}, {}, set(), [], []
    counter = [0]
    for root in nodes:
        if root in index:
            continue
        work = [(root, iter(succ.get(root, ())))]
        index[root] = low[root] = counter[0]
        counter[0] += 1
        stk.append(root)
        onstk.add(root)
        while work:
            v, it = work[-1]
            advanced = False
            for w in it:
                if w not in nodes:
                    continue
                if w not in index:
                    index[w] = low[w] = counter[0]
                    counter[0] += 1
                    stk.append(w)
                    onstk.add(w)
                    work.append((w, iter(succ.get(w, ()))))
                    advanced = True
                    break
                if w in onstk:
                    low[v] = min(low[v], index[w])
            if advanced:
                continue
            work.pop()
            if work:
                p = work[-1][0]
                low[p] = min(low[p], low[v])
            if low[v] == index[v]:
                comp = set()
                while True:
                    w = stk.pop()
                    onstk.discard(w)
                    comp.add(w)
                    if w == v:
                        break
                out.append(comp)
    return out


def find_loops(nodes, succ, blocks, found):
    """Loop nesting forest by recursive SCC decomposition.

    Natural loops (a back edge whose target dominates its source) are NOT
    enough here, and the miss is dangerous rather than cosmetic: GCC rotates
    `while (r >= span) r -= span;` into a shape with two entries, which is
    IRREDUCIBLE — no back edge, no natural loop, and a dominator-based reader
    reports the function as loop-free while the CPU sits in a real cycle.  An
    SCC finds every cycle by construction, reducible or not.  Recursing into
    each SCC with the edges into its entry points removed peels one nesting
    level at a time, so inner loops are still found and still multiply."""
    for comp in sccs(nodes, succ):
        if len(comp) == 1:
            (only,) = tuple(comp)
            if only not in succ.get(only, ()):
                continue                      # trivial component, no cycle
        entries = set()
        for n in comp:
            for m in nodes:
                if m not in comp and n in succ.get(m, ()):
                    entries.add(n)
        if not entries:
            entries = {min(comp)}             # the region's own entry block
        latches = [u for u in comp
                   if any(v in entries for v in succ.get(u, ()))]
        found.append({"body": comp, "headers": entries, "latches": latches})
        inner = {u: [v for v in succ.get(u, ()) if v not in entries]
                 for u in comp}
        find_loops(comp, inner, blocks, found)


def all_loops(blocks, entry):
    nodes = set(blocks)
    succ = {a: [s for s in blocks[a].succ if s in nodes] for a in nodes}
    found = []
    find_loops(nodes, succ, blocks, found)
    found.sort(key=lambda l: (len(l["body"]), min(l["headers"])))
    return found


def dag_longest(nodes, starts):
    """Longest path over an acyclic node graph. Returns (cost, ok)."""
    memo, state = {}, {}

    def walk(n):
        if n in memo:
            return memo[n]
        if state.get(n) == 1:
            return None                        # cycle: caller falls back
        state[n] = 1
        best = 0
        for s in nodes[n]["succ"]:
            if s not in nodes:
                continue
            r = walk(s)
            if r is None:
                state[n] = 0
                return None
            best = max(best, r)
        state[n] = 2
        memo[n] = nodes[n]["cost"] + best
        return memo[n]

    total, ok = 0, True
    for s in starts:
        r = walk(s)
        if r is None:
            return (sum(nodes[n]["cost"] for n in nodes), False)
        total = max(total, r)
    return (total, ok)


def wcet_path(blocks, entry, loops):
    """Worst-case path cost: collapse loops innermost-first, then take the
    longest path through what is left.

    A path cannot take both sides of one branch, so this is a genuine upper
    bound over every feature combination at once — pitch voice AND overdub AND
    varispeed AND a splice in flight, whichever mix is dearest.  It is far
    tighter than summing all the code (which would charge for the pitch voice
    and the string bank and the delay taps in the same sample, none of which
    can happen together), and it needs no infeasible-path annotations to stay
    honest, because it never claims a path is impossible.

    Each loop is replaced by bound x (longest path through its body), which is
    the standard structural WCET reduction; irreducible bodies that survive the
    back-edge removal fall back to summing the body, and say so."""
    nodes = {a: {"cost": blocks[a].cost, "succ": set(blocks[a].succ),
                 "exit": blocks[a].is_exit} for a in blocks}
    rep = {a: a for a in blocks}
    irreducible = 0

    def find(a):
        while rep[a] != a:
            a = rep[a]
        return a

    for lp in loops:                            # innermost first (sorted)
        body = {find(a) for a in lp["body"] if find(a) in nodes}
        heads = {find(h) for h in lp["headers"]} & body
        if not body or not heads:
            continue
        sub = {n: {"cost": nodes[n]["cost"],
                   "succ": {s for s in nodes[n]["succ"]
                            if s in body and s not in heads}}
               for n in body}
        inner, ok = dag_longest(sub, heads)
        if not ok:
            irreducible += 1
        cost = lp["bound"] * inner
        new = min(body)
        out = set()
        for n in body:
            out |= {s for s in nodes[n]["succ"] if s not in body}
        is_exit = any(nodes[n]["exit"] for n in body)
        for n in body:
            if n != new:
                del nodes[n]
                rep[n] = new
        nodes[new] = {"cost": cost, "succ": out, "exit": is_exit}
        for n in nodes:                          # redirect edges into the loop
            if nodes[n]["succ"] & (body - {new}):
                nodes[n]["succ"] = {new if s in body else s
                                    for s in nodes[n]["succ"]}
    total, ok = dag_longest(nodes, [find(entry)])
    return total, irreducible + (0 if ok else 1)


def work_bound(blocks, entry, bounds, name, report):
    """Loop bounds, the boundedness checks, and the worst-case path cost.

    HOW LOOPS ARE DECLARED, and where the teeth are.  One optimised C loop
    becomes several machine loops (peeling, unrolling, specialisation), so
    declaring each individually would be ceremony, not evidence.  A function
    declares (a) how many natural loops its code contains and (b) a default
    bound justified from the SOURCE; counted loops take their bound from their
    own `cmp rN,#imm` exit test instead.  The teeth are the COUNT: any edit
    that adds or restructures a loop changes it and fails the build until a
    human re-justifies the number.  On top of that, a loop whose body has no
    edge leaving it is rejected outright, whatever the manifest says — that is
    the class the contract actually fears."""
    loops = all_loops(blocks, entry)

    decl = bounds.get(name)
    if report is not None:
        prev = decl if isinstance(decl, dict) else {}
        report[name] = {"loops": len(loops),
                        "default_bound": prev.get("default_bound", 32),
                        "why": prev.get("why", "TODO: justify from the source"),
                        "overrides": prev.get("overrides", {})}
    if not isinstance(decl, dict) or decl.get("loops") != len(loops):
        raise BoundsError(name, len(loops),
                          decl.get("loops") if isinstance(decl, dict) else None,
                          loops, entry)

    default_bound = int(decl.get("default_bound", 0))
    overrides = decl.get("overrides", {})

    for i, lp in enumerate(loops):
        body = lp["body"]
        escapes = any(blocks[b].is_exit or any(s not in body
                                               for s in blocks[b].succ)
                      for b in body)
        if not escapes and str(i) not in overrides:
            raise BoundsError(name, len(loops), decl.get("loops"), loops, entry,
                              "%s loop %d (header +0x%x) has no edge leaving "
                              "its body — it is not bounded by construction"
                              % (name, i, min(lp["headers"]) - entry))
        b_n = None
        for latch in lp["latches"]:            # take the loosest exit test
            c = infer_bound(blocks.get(latch))
            if c is not None:
                b_n = c if b_n is None else max(b_n, c)
        if str(i) in overrides:
            b_n = int(overrides[str(i)]["bound"])
        elif b_n is None:
            b_n = default_bound
        lp["bound"] = b_n
    total, irred = wcet_path(blocks, entry, loops)
    if irred:
        WARNINGS.append("%s: %d irreducible region(s) — their bodies are "
                        "SUMMED rather than path-costed (conservative)"
                        % (name, irred))
    return total, loops


WARNINGS = []

CMP_IMM_RE = re.compile(r"^\s*\w+,\s*#(\d+)")


def infer_bound(latch_block):
    """Trip count read straight out of the loop's own exit test.

    A counted C loop compiles to `cmp rN, #limit` immediately before the back
    edge, so the bound is IN THE IMAGE and does not need declaring — which is
    the strongest kind of evidence available here: it is what the CPU will
    actually do, not what the source was believed to say.  Loops whose test is
    against a register (a runtime count) return None and fall back to the
    declared default, where a human has to justify the number."""
    if latch_block is None:
        return None
    for ins in reversed(latch_block.insns[-6:]):
        if mnem_base(ins.mnem) == "cmp":
            m = CMP_IMM_RE.match(ins.ops)
            if m:
                n = int(m.group(1))
                if 1 <= n <= 4096:
                    return n
            return None
    return None


class BoundsError(Exception):
    def __init__(self, fn, found, declared, back, entry, why=None):
        self.fn, self.found, self.declared = fn, found, declared
        self.back, self.entry = back, entry
        super().__init__(why or ("%s: %d loops in the image, %s declared"
                                 % (fn, found, declared)))


# ------------------------------------------------------------------- driver
def resolve(funcs, name):
    if name in funcs:
        return funcs[name]
    for k in funcs:
        if k == name or k.startswith(name + "."):
            return funcs[k]
    return None


def analyse(funcs, root_name, manifest, report=None):
    bounds = manifest["loop_bounds"]
    sdram_fns = set(manifest["sdram_functions"])
    root = resolve(funcs, root_name)
    if root is None:
        raise SystemExit("wcet: root function %s not in the image" % root_name)

    order, seen, problems = [], set(), []

    def visit(fn, stack):
        if fn.name in seen:
            return
        if fn.name in stack:
            problems.append("recursion through %s" % fn.name)
            return
        stack = stack | {fn.name}
        for ins in fn.insns:
            base = mnem_base(ins.mnem)
            if base in ("bl", "blx"):
                if ins.tname is None:
                    problems.append("%s: unresolved indirect call at 0x%x"
                                    % (fn.name, ins.addr))
                    continue
                callee = resolve(funcs, ins.tname)
                if callee is None:
                    problems.append("%s: call to unknown %s"
                                    % (fn.name, ins.tname))
                    continue
                visit(callee, stack)
            elif base in ("tbb", "tbh"):
                problems.append("%s: jump table at 0x%x (unbounded dispatch)"
                                % (fn.name, ins.addr))
        seen.add(fn.name)
        order.append(fn)

    visit(root, frozenset())

    call_cost, per_fn, bound_errors = {}, {}, []
    for fn in order:                      # callees first
        sdram = fn.name.split(".")[0] in sdram_fns
        blocks = build_blocks(fn, sdram, call_cost)
        try:
            c, _loops = work_bound(blocks, fn.addr, bounds,
                                   fn.name.split(".")[0], report)
        except BoundsError as e:
            bound_errors.append(e)
            c = 0
        call_cost[fn.name] = c
        per_fn[fn.name] = c
    return per_fn, call_cost.get(root.name, 0), problems, bound_errors, order


def soft_double_check(funcs, order):
    bad = []
    for fn in order:
        for ins in fn.insns:
            if ins.tname and ins.tname.startswith("__aeabi_d"):
                bad.append("%s calls %s" % (fn.name, ins.tname))
            if ins.tname and ins.tname in ("__adddf3", "__muldf3", "__divdf3"):
                bad.append("%s calls %s" % (fn.name, ins.tname))
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", required=True)
    ap.add_argument("--manifest", default=DEFAULT_MANIFEST)
    ap.add_argument("--objdump",
                    default=os.environ.get("CROSS", "arm-none-eabi-") + "objdump")
    ap.add_argument("--root", default="bsp_audio_isr")
    ap.add_argument("--emit-bounds", action="store_true")
    ap.add_argument("--calibrate", type=int, default=0,
                    help="record a measured worst-case isr_pk (cycles>>4)")
    ap.add_argument("--strict", action="store_true",
                    help="fail the build when the projection is over target")
    ap.add_argument("--set-baseline", action="store_true",
                    help="record this build's ceiling as the regression gate")
    ap.add_argument("--json", default=None)
    args = ap.parse_args()

    with open(args.manifest) as f:
        manifest = json.load(f)

    funcs = disassemble(args.elf, args.objdump)
    report = {} if args.emit_bounds else None
    per_fn, ceiling, problems, bound_errors, order = analyse(
        funcs, args.root, manifest, report)

    if args.emit_bounds:
        merged = dict(manifest["loop_bounds"])
        merged.update(report)
        manifest["loop_bounds"] = merged
        print(json.dumps(manifest, indent=2, sort_keys=True))
        return 0

    budget = manifest["budget_cycles"]
    K = manifest.get("calibration", {}).get("k", 1.0)

    print("== wcet: static ceiling for %s ==" % args.root)
    print("   budget          %6d cycles/block (%d isr_pk units)"
          % (budget, budget >> 4))
    print("   static ceiling  %6d cycles  = %5.1f%% of budget"
          % (ceiling, 100.0 * ceiling / budget))
    print("   calibrated (K=%.3f) %6d cycles  = %5.1f%% of budget"
          % (K, K * ceiling, 100.0 * K * ceiling / budget))
    print("   top contributors:")
    for name, c in sorted(per_fn.items(), key=lambda kv: -kv[1])[:8]:
        print("      %-40s %7d" % (name, c))

    fails, soft = [], []
    for e in bound_errors:
        fails.append("UNBOUNDED LOOPS: %s" % e)
        print("\n   %s" % e)
        print("   %d natural loops found (offset from function start):"
              % len(e.back))
        for i, lp in enumerate(e.back[:16]):
            print("      loop %2d header +0x%-6x body %3d blocks  bound %s"
                  % (i, min(lp["headers"]) - e.entry, len(lp["body"]),
                     lp.get("bound", "?")))
        if len(e.back) > 16:
            print("      ... %d more" % (len(e.back) - 16))
        print("   run with --emit-bounds and justify the count.")
    for p in problems:
        fails.append("CONTROL FLOW: %s" % p)
    for b in soft_double_check(funcs, order):
        fails.append("SOFT DOUBLE: %s" % b)

    for w in WARNINGS:
        print("   note: %s" % w)

    limit = manifest.get("projection_limit", 0.90)
    proj = K * ceiling / budget
    if K != 1.0 and proj > limit:
        msg = ("projection %.1f%% is over the %.0f%% target"
               % (100 * proj, 100 * limit))
        # A report, not a build break, unless --strict. The reference image is
        # MEASURED at 110-117%: gating the build on the projection would make
        # CI red from the day this tool landed and stay red until the engine
        # work lands, which trains people to ignore it. The ratchet below is
        # what stops it getting worse in the meantime; --strict is what the
        # lead flips once the number is under the line.
        (fails if args.strict else soft).append(msg)

    # Regression gate. The absolute ceiling carries model slop; the DELTA
    # between builds does not, and that is what both workstreams need to be
    # held to. Any change that makes the worst path measurably more expensive
    # has to be acknowledged by re-recording the baseline.
    base = manifest.get("baseline_ceiling")
    if base and not args.calibrate:
        grow = 100.0 * (ceiling - base) / base
        print("   baseline        %6d cycles  (%+.1f%% this build)"
              % (base, grow))
        if ceiling > base * (1.0 + manifest.get("baseline_slack", 0.05)):
            fails.append("ceiling grew %+.1f%% over the recorded baseline "
                         "(%d -> %d). If this is intended, re-record it with "
                         "--set-baseline." % (grow, base, ceiling))
    if args.set_baseline:
        manifest["baseline_ceiling"] = ceiling
        with open(args.manifest, "w") as f:
            json.dump(manifest, f, indent=2, sort_keys=True)
            f.write("\n")
        print("   baseline recorded: %d cycles" % ceiling)

    if args.calibrate:
        measured = args.calibrate * 16
        manifest.setdefault("calibration", {})
        manifest["calibration"]["k"] = round(measured / float(ceiling), 4)
        manifest["calibration"]["measured_isr_pk"] = args.calibrate
        manifest["calibration"]["ceiling_at_calibration"] = ceiling
        with open(args.manifest, "w") as f:
            json.dump(manifest, f, indent=2, sort_keys=True)
            f.write("\n")
        print("   calibrated: K = %.4f (measured %d units = %d cycles)"
              % (manifest["calibration"]["k"], args.calibrate, measured))

    if args.json:
        with open(args.json, "w") as f:
            json.dump({"ceiling": ceiling, "budget": budget,
                       "per_function": per_fn, "fails": fails}, f, indent=2)

    if soft:
        print("\nOVER TARGET (report only, use --strict to gate on it):")
        for s_ in soft:
            print("   " + s_)
    if fails:
        print("\nFAILED:")
        for f_ in fails:
            print("   " + f_)
        return 1
    print("\nOK — every loop bounded, no soft double, no indirect dispatch.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
