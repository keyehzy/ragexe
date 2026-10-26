"""Everything the binary says around one function, for naming it (docs/naming.md).

usage: .venv/bin/python tools/fn_context.py [--callers N] ADDRESS [ADDRESS ...]
       .venv/bin/python tools/fn_context.py --file LIST       (one hex address per line)

For each function it prints:
  - its ledger row and the named functions before and after it: the linker keeps a
    source file's functions together, so neighbours are the strongest context;
  - the calling convention evidence: the bytes `ret` pops, whether the body reads ECX
    before writing it, and at how many call sites the caller sets ECX just before the
    call (a __thiscall whose body never uses `this` shows only in its callers);
  - its callers with their names, including exception-unwind funclets and the function
    whose EH tables list them, and where its address is taken (vtable slots, tables,
    pushes);
  - its callees with imports resolved through thunks, and its virtual calls by slot;
  - the strings, data addresses (with their labels) and `this` offsets it uses; each data
    address shows how many other functions use it and the named ones among them, and
    the nil node of a std::Tree instantiation is marked with its node size;
  - other functions with the same code (same instructions, same call and jump targets),
    the named ones first: template copies and duplicated helpers.
Exception-unwind funclets are summarised on one line, by the functions that own them,
and do not count towards --callers.

       .venv/bin/python tools/fn_context.py --data ADDRESS [ADDRESS ...]
lists every instruction that uses each data address (absolute operands, the base of
indexed ones included) with its function.
Decompile with tools/ghidra/decompile.sh; this script only reads the binary and the
label files.
"""
import argparse
import os
import struct
import sys
from bisect import bisect_left
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from binindex import BinIndex, ROOT  # noqa: E402
from capstone import x86  # noqa: E402

LABELS = os.path.join(ROOT, "analysis", "labels")
EXPORTS = os.path.join(ROOT, "analysis", "exports")
ECX_WINDOW = 8          # instructions before a call searched for a write to ECX
OPERATOR_NEW = 0x5C183B
STL_LOCK, STL_UNLOCK = 0x5B3267, 0x5B3303
LISTED = 4              # named functions listed per data address or identical body


def read_tsv(path):
    with open(path, encoding="utf-8") as f:
        lines = [l for l in f.read().splitlines() if l and not l.startswith("#")]
    header = lines[0].split("\t")
    return [dict(zip(header, line.split("\t"))) for line in lines[1:]]


class Context:
    def __init__(self):
        self.bx = BinIndex()
        self.ledger = {int(r["entry"], 16): r for r in read_tsv(os.path.join(ROOT, "analysis", "ledger.tsv"))}
        self.order = sorted(self.ledger)
        self.data_names = {}
        for name in sorted(os.listdir(LABELS)):
            if name.endswith(".data.tsv"):
                for r in read_tsv(os.path.join(LABELS, name)):
                    self.data_names[int(r["address"], 16)] = r["name"]
        self.slots = defaultdict(list)
        for r in read_tsv(os.path.join(EXPORTS, "Ragexe.exe.vtables.tsv")):
            self.slots[int(r["target"], 16)].append(f"{r['class']} vtable {r['vftable']} slot {r['slot']}")
        self.parents = self.bx.eh_parents()
        self._refs = None
        self._data = None
        self._bodies = None
        self._nil = None

    # -- naming helpers -----------------------------------------------------------------

    def name(self, addr):
        if isinstance(addr, str):
            return addr
        fn = self.bx.functions.get(addr)
        return f"{addr:08x} {fn.qualified}" if fn else f"{addr:08x}"

    def target_name(self, t):
        if isinstance(t, int):
            t = self.bx.resolve_thunk(t)
        return self.name(t)

    def is_named(self, entry):
        r = self.ledger.get(entry)
        return r is not None and r["kind"] == "code" and r["status"] != "raw"

    # -- references ---------------------------------------------------------------------

    def refs(self):
        """target -> [(site, function, how)] for calls, jumps and address uses in all code,
        funclets included, and pointer-sized values in the data sections."""
        if self._refs is not None:
            return self._refs
        out = defaultdict(list)
        for fn in self.bx.functions.values():
            for i in self.bx.insns(fn):
                if i.id == x86.X86_INS_CALL:
                    t = self.bx.call_target(i)
                    if isinstance(t, int):
                        out[t].append((i.address, fn.entry, "call"))
                    continue
                if i.id == x86.X86_INS_JMP:
                    t = self.bx.jump_target(i)
                    if isinstance(t, int) and t != fn.entry and not (fn.entry <= t < fn.entry + fn.size):
                        out[t].append((i.address, fn.entry, "jmp"))
                    continue
                for v in self.bx.data_refs(i):
                    if v in self.bx.functions:
                        out[v].append((i.address, fn.entry, i.mnemonic))
        for sec in (".rdata", ".data"):
            lo, hi = self.bx.sections[sec]
            data = self.bx.image[lo - self.bx.base:hi - self.bx.base]
            for k in range(0, len(data) - 3, 4):
                v = struct.unpack_from("<I", data, k)[0]
                if v in self.bx.functions and self.bx.functions[v].kind != "thunk":
                    out[v].append((lo + k, None, sec))
        self._refs = out
        return out

    def data_uses(self):
        """data address -> [(site, function)] for absolute operands in .data and .rdata, the
        base of an indexed operand included."""
        if self._data is not None:
            return self._data
        out = defaultdict(list)
        for fn in self.bx.functions.values():
            for i in self.bx.insns(fn):
                for op in i.operands:
                    if op.type == x86.X86_OP_IMM:
                        v = op.imm & 0xFFFFFFFF
                    elif op.type == x86.X86_OP_MEM and op.mem.base == 0:
                        v = op.mem.disp & 0xFFFFFFFF
                    else:
                        continue
                    if self.bx.section_of(v) in (".data", ".rdata") and v not in self.bx.functions:
                        out[v].append((i.address, fn.entry))
        self._data = out
        return out

    def body_key(self, fn):
        """The instructions with call and jump targets made absolute outside the function and
        relative to its entry inside it: equal keys are the same code at another address."""
        key = []
        for i in self.bx.insns(fn):
            op = i.operands[0] if i.operands else None
            if op is not None and op.type == x86.X86_OP_IMM and (
                    i.id == x86.X86_INS_CALL or i.group(x86.X86_GRP_JUMP)):
                t = op.imm & 0xFFFFFFFF
                inside = fn.entry <= t < fn.entry + fn.size
                key.append((i.mnemonic, "+%x" % (t - fn.entry) if inside else "%x" % t))
            else:
                key.append((i.mnemonic, i.op_str))
        return tuple(key)

    def same_bodies(self):
        """function -> other functions with the same code (at least 4 instructions)."""
        if self._bodies is not None:
            return self._bodies
        groups = defaultdict(list)
        for fn in self.bx.functions.values():
            if fn.kind == "code" and fn.size >= 8 and len(self.bx.insns(fn)) >= 4:
                groups[self.body_key(fn)].append(fn.entry)
        out = {}
        for entries in groups.values():
            if len(entries) > 1:
                for e in entries:
                    out[e] = [x for x in entries if x != e]
        self._bodies = out
        return out

    def tree_nils(self):
        """nil-node global -> (node size, functions that create it) for each std::Tree
        instantiation. A function that calls operator new(node size) and then, under the STL
        lock, stores a register into one .data dword and steps another (the user count)
        creates the shared nil node: InitHead, or a constructor that inlines it."""
        if self._nil is not None:
            return self._nil
        out = {}
        for fn in self.bx.functions.values():
            if fn.kind != "code":
                continue
            ins = self.bx.insns(fn)
            targets = [self.bx.call_target(i) for i in ins if i.id == x86.X86_INS_CALL]
            if STL_LOCK not in targets or STL_UNLOCK not in targets or OPERATOR_NEW not in targets:
                continue
            size, locked = None, False
            for k, i in enumerate(ins):
                t = self.bx.call_target(i) if i.id == x86.X86_INS_CALL else None
                if t == OPERATOR_NEW and not locked:
                    # the size is the last push before the call; the compiler may schedule
                    # other stores between the two
                    for p in reversed(ins[max(0, k - 6):k]):
                        if p.id == x86.X86_INS_PUSH:
                            if p.operands[0].type == x86.X86_OP_IMM:
                                size = p.operands[0].imm
                            break
                elif t == STL_LOCK:
                    locked, writes = True, []
                elif t == STL_UNLOCK and locked:
                    locked = False
                    nil = [g for g, counted in writes if not counted]
                    if size is not None and len(set(nil)) == 1 and any(counted for _, counted in writes):
                        prev = out.get(nil[0], (size, []))
                        out[nil[0]] = (size, prev[1] + [fn.entry])
                elif locked and i.id == x86.X86_INS_MOV and i.operands[0].type == x86.X86_OP_MEM \
                        and i.operands[0].mem.base == 0 and i.operands[0].mem.index == 0 \
                        and i.operands[1].type == x86.X86_OP_REG \
                        and self.bx.section_of(i.operands[0].mem.disp & 0xFFFFFFFF) == ".data":
                    # the user count is stepped (inc or dec) just before it is stored
                    counted = ins[k - 1].id in (x86.X86_INS_INC, x86.X86_INS_DEC)
                    writes.append((i.operands[0].mem.disp & 0xFFFFFFFF, counted))
        self._nil = out
        return out

    def data_label(self, v):
        if v in self.data_names:
            return self.data_names[v]
        if v in self.tree_nils():
            size, inits = self.tree_nils()[v]
            return f"(std::Tree nil node, {size:#x}-byte nodes, created by {'; '.join(self.name(e) for e in inits)})"
        for d in (4, -4):
            if v + d in self.tree_nils():
                return f"(no label; beside the std::Tree nil node {v + d:08x})"
        return "(no label)"

    def users(self, v, entry):
        """'N other functions: named ones' for a data address."""
        fns = {self.parents.get(f, f) for _, f in self.data_uses().get(v, ()) if f != entry}
        fns.discard(self.parents.get(entry, entry))
        if not fns:
            return "used by no other function"
        named = sorted((f for f in fns if self.is_named(f)),
                       key=lambda f: (self.bx.functions[f].namespace.startswith("std"), f))[:LISTED]
        text = f"{len(fns)} other functions"
        if named:
            text += ": " + ", ".join(self.bx.functions[f].qualified for f in named)
            if len(fns) > len(named):
                text += ", ..."
        return text

    def describe_data(self, v):
        uses = self.data_uses().get(v, [])
        lines = [f"== data {v:08x} {self.data_label(v)}: {len(uses)} uses in "
                 f"{len({f for _, f in uses})} functions"]
        for site, f in sorted(uses):
            fn = self.bx.functions[f]
            ins = next(i for i in self.bx.insns(fn) if i.address == site)
            lines.append(f"  {site:08x} {ins.mnemonic} {ins.op_str}  in {self.name(f)}")
        return "\n".join(lines) + "\n"

    def sets_ecx_before(self, caller, site):
        fn = self.bx.functions.get(caller)
        if fn is None:
            return False
        ins = self.bx.insns(fn)
        k = next((n for n, i in enumerate(ins) if i.address == site), None)
        if k is None:
            return False
        for p in reversed(ins[max(0, k - ECX_WINDOW):k]):
            if p.id in (x86.X86_INS_CALL, x86.X86_INS_RET, x86.X86_INS_JMP):
                return False
            try:
                read, written = p.regs_access()
            except Exception:
                return False
            if x86.X86_REG_ECX in read:
                return False
            if x86.X86_REG_ECX in written:
                return True
        return False

    # -- one function -------------------------------------------------------------------

    def neighbours(self, entry, n=2):
        k = bisect_left(self.order, entry)
        before, after = [], []
        j = k - 1
        while j >= 0 and len(before) < n:
            if self.is_named(self.order[j]):
                before.append(self.order[j])
            j -= 1
        j = k + 1
        while j < len(self.order) and len(after) < n:
            if self.is_named(self.order[j]):
                after.append(self.order[j])
            j += 1
        return list(reversed(before)), after

    def describe(self, entry, max_callers):
        bx = self.bx
        fn = bx.functions.get(entry)
        if fn is None:
            return f"== {entry:08x}: not a function in the ledger\n"
        r = self.ledger.get(entry, {})
        lines = [f"== {entry:08x} {fn.qualified}  ({fn.size} bytes, {r.get('kind')}, "
                 f"{'library' if r.get('library') == '1' else 'game'}, {r.get('subsystem') or '-'}, "
                 f"{r.get('status')})"]
        before, after = self.neighbours(entry)
        lines.append("neighbours: " + "; ".join(self.name(a) for a in before) + "  <this>  "
                     + "; ".join(self.name(a) for a in after))
        ins = bx.insns(fn)
        rets = sorted({int(i.op_str, 0) if i.op_str else 0 for i in ins if i.id == x86.X86_INS_RET})
        refs = self.refs().get(entry, [])
        calls = [(s, c) for s, c, how in refs if how == "call" and c is not None]
        set_ecx = sum(1 for s, c in calls if self.sets_ecx_before(c, s))
        lines.append(f"convention: ret pops {', '.join(map(str, rets)) or 'nothing (no ret)'}; "
                     f"reads ECX first: {'yes' if bx.reads_ecx_first(fn) else 'no'}; "
                     f"caller sets ECX just before the call at {set_ecx} of {len(calls)} call sites")
        if fn.kind == "funclet" and entry in self.parents:
            lines.append(f"funclet of {self.name(self.parents[entry])}")
        groups = defaultdict(list)                # (function or section, how) -> sites
        unwinds = defaultdict(int)                # owner of the funclet -> references
        for site, caller, how in refs:
            if caller is not None and bx.functions[caller].kind == "funclet":
                unwinds[self.parents.get(caller)] += 1
                continue
            groups[(caller, how)].append(site)
        by_caller = {c for c, _ in groups if c is not None}
        direct = sum(len(v) for v in groups.values())
        lines.append(f"referenced from {direct} places in {len(by_caller)} functions"
                     + (f" and {sum(unwinds.values())} unwind funclets" if unwinds else "") + (":" if refs else ""))
        ordered = sorted(groups.items(), key=lambda g: (g[0][0] is None, min(g[1])))
        for n, ((caller, how), sites) in enumerate(ordered):
            if n >= max_callers:
                rest = sum(len(v) for _, v in ordered[n:])
                lines.append(f"  ... and {rest} more in {len(ordered) - n} more places")
                break
            sites.sort()
            where = f"{sites[0]:08x}" + (f" and {len(sites) - 1} more" if len(sites) > 1 else "")
            if caller is None:
                owner = "; ".join(self.slots.get(entry, [])) if how == ".rdata" else ""
                lines.append(f"  pointer at {where} ({how}){'  ' + owner if owner else ''}")
                continue
            extra = ""
            if how == "call":
                k = sum(1 for s in sites if self.sets_ecx_before(caller, s))
                extra = f"  [sets ECX at {k} of {len(sites)}]" if k else ""
            lines.append(f"  {how} at {where} in {self.name(caller)}{extra}")
        if unwinds:
            owners = sorted((p for p in unwinds if p is not None), key=lambda p: (not self.is_named(p), p))
            shown = ", ".join(bx.functions[p].qualified for p in owners[:6])
            more = len(owners) - 6
            lines.append(f"  unwind funclets: {sum(unwinds.values())} references, from the cleanup of "
                         f"{len(unwinds)} functions" + (f": {shown}" if shown else "")
                         + (f" and {more} more" if more > 0 else ""))
        same = self.same_bodies().get(entry, [])
        if same:
            same = sorted(same, key=lambda e: (not self.is_named(e), e))
            lines.append(f"same code as {len(same)} other functions: "
                         + "; ".join(self.name(e) for e in same[:LISTED]) + ("; ..." if len(same) > LISTED else ""))
        callees, virtual = [], Counter()
        strings, globals_, fields = [], [], Counter()
        for i in ins:
            if i.id == x86.X86_INS_CALL:
                t = bx.call_target(i)
                if t is not None:
                    n = self.target_name(t)
                    if n not in callees:
                        callees.append(n)
                else:
                    op = i.operands[0]
                    if op.type == x86.X86_OP_MEM and op.mem.base != 0 and op.mem.index == 0:
                        virtual[f"[vtable+0x{op.mem.disp:x}] (slot {op.mem.disp // 4})"] += 1
                continue
            for op in i.operands:
                if op.type == x86.X86_OP_MEM and op.mem.base == x86.X86_REG_ECX and op.mem.index == 0:
                    fields[op.mem.disp] += 1
            for v in bx.data_refs(i):
                sec = bx.section_of(v)
                if sec in (".rdata", ".data"):
                    s = bx.cstring(v, 80)
                    if s and len(s) >= 3 and all(32 <= ord(c) < 127 or c in "\r\n\t" for c in s):
                        text = f"{v:08x} {s!r}"
                        if text not in strings:
                            strings.append(text)
                    elif sec == ".data" or v in self.data_names:
                        if v not in globals_:
                            globals_.append(v)
        if callees:
            lines.append("calls: " + "; ".join(callees))
        if virtual:
            lines.append("virtual calls: " + "; ".join(f"{k} x{n}" for k, n in virtual.items()))
        if strings:
            lines.append("strings: " + "; ".join(strings))
        if globals_:
            lines.append("data:")
            for v in globals_[:16]:
                lines.append(f"  {v:08x} {self.data_label(v)}; {self.users(v, entry)}")
            if len(globals_) > 16:
                lines.append(f"  ... and {len(globals_) - 16} more")
        if fields:
            lines.append("[ecx+n] uses: " + ", ".join(f"+0x{o:x}" for o in sorted(fields)))
        return "\n".join(lines) + "\n"


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("addresses", nargs="*")
    p.add_argument("--file")
    p.add_argument("--callers", type=int, default=12, help="references listed per function (default 12)")
    p.add_argument("--data", action="store_true", help="the addresses are data: list every use")
    a = p.parse_args()
    addrs = list(a.addresses)
    if a.file:
        with open(a.file) as f:
            addrs += f.read().split()
    ctx = Context()
    for addr in addrs:
        print(ctx.describe_data(int(addr, 16)) if a.data else ctx.describe(int(addr, 16), a.callers))


if __name__ == "__main__":
    main()
