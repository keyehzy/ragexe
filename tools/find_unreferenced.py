"""Find the functions nothing in the binary reaches, and mark them in the ledger.

A function is referenced by:
  - a call or jump to its entry from another function, funclets included;
  - an instruction operand holding its entry address (push offset, mov reg, offset, ...);
  - a 4-byte value equal to its entry anywhere outside the decoded instructions: the
    data sections at every alignment (vtables, the initializer tables, EH and SEH
    tables, callback and jump tables) and the gaps between functions in .text;
  - falling through from the function before it, when that one does not end in ret,
    jmp or int3 (an entry the analysis split off).
Every match counts, even a constant that only happens to equal an entry, so a function
is never marked by mistake for want of a reference; a stray match only keeps one live.

The roots are the PE entry point and every data value equal to an entry, except the
unwind and catch entries of C++ EH FuncInfo tables: those make a funclet reachable only
when its parent is (binindex.eh_parents). The ledger's reach column becomes:
  unreferenced  nothing references the function
  dead          references exist, but only from unreferenced or dead functions
  (empty)       reachable from a root
Writes the reach column of analysis/ledger.tsv and the report
analysis/exports/Ragexe.exe.unreferenced.tsv (entry, size, kind, library, reach, name,
referenced by).
"""
import os
import struct
import sys
from collections import defaultdict, deque

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from binindex import BinIndex, LEDGER, ROOT  # noqa: E402
from capstone import x86  # noqa: E402
import ledger  # noqa: E402

OUT = os.path.join(ROOT, "analysis", "exports", "Ragexe.exe.unreferenced.tsv")
ENDS = (x86.X86_INS_RET, x86.X86_INS_JMP, x86.X86_INS_INT3)


class Reach:
    def __init__(self):
        self.bx = BinIndex()
        self.entries = set(self.bx.functions)
        self.edges = defaultdict(set)      # function -> functions it references
        self.refby = defaultdict(set)      # function -> referencing functions, or "data"/"entry"
        self.roots = set()

    def add(self, src, dst):
        if dst in self.entries and dst != src:
            self.edges[src].add(dst)
            self.refby[dst].add(src)

    def scan_code(self):
        covered = []                       # (start, end) of decoded instruction bytes
        for fn in self.bx.functions.values():
            ins = self.bx.insns(fn)
            for i in ins:
                covered.append((i.address, i.address + i.size))
                for op in i.operands:
                    if op.type == x86.X86_OP_IMM:
                        self.add(fn.entry, op.imm & 0xFFFFFFFF)
                    elif op.type == x86.X86_OP_MEM and op.mem.base == 0:
                        self.add(fn.entry, op.mem.disp & 0xFFFFFFFF)
            if ins and ins[-1].id not in ENDS:
                nxt = ins[-1].address + ins[-1].size
                while nxt not in self.entries and nxt < fn.entry + fn.size + 16 and \
                        self.bx.image[nxt - self.bx.base] in (0x90, 0xCC):
                    nxt += 1
                if nxt in self.entries:
                    self.add(fn.entry, nxt)
        return covered

    def scan_data(self, covered):
        funclet_slots = self.funclet_slots()
        img = self.bx.image
        base = self.bx.base
        lo, hi = self.bx.sections[".text"]
        covered.sort()
        gaps, at = [], lo
        for s, e in covered:
            if s > at:
                gaps.append((at, s))
            at = max(at, e)
        gaps.append((at, hi))
        regions = gaps + [v for k, v in self.bx.sections.items() if k != ".text"]
        for a, b in regions:
            data = img[a - base:b - base]
            for k in range(0, len(data) - 3):
                v = struct.unpack_from("<I", data, k)[0]
                if v in self.entries and a + k not in funclet_slots:
                    self.refby[v].add("data")
                    self.roots.add(v)

    def funclet_slots(self):
        """Addresses in EH FuncInfo tables that hold a funclet: they follow their parent."""
        slots = set()
        parents = self.bx.eh_parents()
        for funclet, parent in parents.items():
            self.add(parent, funclet)
        lo, hi = self.bx.sections[".rdata"]
        for a in range(lo, hi - 3):
            if self.bx.u32(a) in parents:
                slots.add(a)
        return slots

    def run(self):
        covered = self.scan_code()
        self.scan_data(covered)
        entry = self.bx.base + self.bx.pe.OPTIONAL_HEADER.AddressOfEntryPoint
        self.roots.add(entry)
        self.refby[entry].add("entry")
        seen, todo = set(self.roots), deque(self.roots)
        while todo:
            f = todo.popleft()
            for g in self.edges.get(f, ()):
                if g not in seen:
                    seen.add(g)
                    todo.append(g)
        reach = {}
        for e in self.entries:
            if e in seen:
                reach[e] = ""
            elif not self.refby.get(e):
                reach[e] = "unreferenced"
            else:
                reach[e] = "dead"
        return reach

    def write(self, reach):
        rows = ledger.read_tsv(LEDGER)
        counts = defaultdict(lambda: [0, 0])
        with open(OUT, "w", encoding="utf-8") as f:
            f.write("entry\tsize\tkind\tlibrary\treach\tname\treferenced_by\n")
            for r in rows:
                e = int(r["entry"], 16)
                r["reach"] = reach.get(e, "")
                if r["reach"]:
                    by = sorted(x for x in self.refby.get(e, ()) if isinstance(x, int))
                    f.write(f"{r['entry']}\t{r['size']}\t{r['kind']}\t{r['library']}\t{r['reach']}\t"
                            f"{r['namespace']}::{r['name']}\t{' '.join(f'{x:08x}' for x in by)}\n")
                    key = (r["reach"], r["kind"], "library" if r["library"] == "1" else "game")
                    counts[key][0] += 1
                    counts[key][1] += int(r["size"])
        ledger.write_ledger(LEDGER, rows)
        for (what, kind, lib), (n, b) in sorted(counts.items()):
            print(f"{what:12} {kind:8} {lib:8} {n:5} functions {b:8} bytes")


if __name__ == "__main__":
    r = Reach()
    r.write(r.run())
