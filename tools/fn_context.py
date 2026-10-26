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
  - the strings, data addresses (with their labels) and `this` offsets it uses.
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
        for site, caller, how in refs:
            groups[(caller, how)].append(site)
        by_caller = {c for c, _ in groups if c is not None}
        lines.append(f"referenced from {len(refs)} places in {len(by_caller)} functions" + (":" if refs else ""))
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
            cf = bx.functions[caller]
            extra = ""
            if cf.kind == "funclet":
                p = self.parents.get(caller)
                extra = f"  (unwind funclet of {self.name(p)})" if p else "  (unwind funclet)"
            elif how == "call":
                k = sum(1 for s in sites if self.sets_ecx_before(caller, s))
                extra = f"  [sets ECX at {k} of {len(sites)}]" if k else ""
            lines.append(f"  {how} at {where} in {self.name(caller)}{extra}")
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
                        g = f"{v:08x} {self.data_names.get(v, '(no label)')}"
                        if g not in globals_:
                            globals_.append(g)
        if callees:
            lines.append("calls: " + "; ".join(callees))
        if virtual:
            lines.append("virtual calls: " + "; ".join(f"{k} x{n}" for k, n in virtual.items()))
        if strings:
            lines.append("strings: " + "; ".join(strings))
        if globals_:
            lines.append("data: " + "; ".join(globals_[:16]) + (f"; and {len(globals_) - 16} more" if len(globals_) > 16 else ""))
        if fields:
            lines.append("[ecx+n] uses: " + ", ".join(f"+0x{o:x}" for o in sorted(fields)))
        return "\n".join(lines) + "\n"


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("addresses", nargs="*")
    p.add_argument("--file")
    p.add_argument("--callers", type=int, default=12, help="references listed per function (default 12)")
    a = p.parse_args()
    addrs = list(a.addresses)
    if a.file:
        with open(a.file) as f:
            addrs += f.read().split()
    ctx = Context()
    for addr in addrs:
        print(ctx.describe(int(addr, 16), a.callers))


if __name__ == "__main__":
    main()
