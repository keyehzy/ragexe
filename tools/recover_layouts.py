"""Recover class field layouts from how each class's own functions use `this`.

Sources, per class: its constructors and destructors (from recover_objects.py) and its
virtual functions (from ApplyRtti), all __thiscall. Every [this + n] memory operand is
an access to the field at n. `this` is ECX on entry and is tracked through register
copies, `lea reg, [this + n]` and EBP stack slots.

Field types come from how a field is used:
  float / double     x87 operand of size 4 / 8 (fld, fstp, fadd, ...)
  pointer            the loaded value is later used as a memory base
  ptr:<Class>        the loaded value is passed as `this` to a method of <Class>
  std_string         std::string::_Tidy/_Grow/_Copy is called on this + n
  std_list:<elem>    this + n + 4 receives a fresh operator new(8 + elem) node whose
                     next and prev were set to itself (the MSVC 6 list constructor)
  class:<Class>      a constructor of <Class> is called on this + n
  array:<elem>:<n>   eh_vector_constructor_iterator(this + n, size, count, ctor, ...)
  byte/word/dword    otherwise, from the access width

Inheritance: accesses inside a base class's extent belong to that base (they are
merged into the base's fields with the derived function as evidence). The extent is
the base's size when known, otherwise estimated from its own accesses. Every class
struct is written flattened: base fields at their offsets, then the class's own, so
the class's vftable field keeps the class's own vtable type.

Writes:
  analysis/labels/layouts.fields.tsv   class offset size type name evidence
  analysis/labels/layouts.classes.tsv  estimated sizes for classes never created with new
  analysis/exports/Ragexe.exe.layouts.tsv  per-class coverage and notes
"""
import os
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from binindex import BinIndex, ROOT  # noqa: E402
from capstone import x86  # noqa: E402

EXPORTS = os.path.join(ROOT, "analysis", "exports")
LABELS = os.path.join(ROOT, "analysis", "labels")

STRING_FUNCS = {0x423980, 0x424FA0, 0x425210}
OPERATOR_NEW = 0x5C183B
EH_VECTOR_CTOR = 0x5C2002

X87_MEM = {
    x86.X86_INS_FLD, x86.X86_INS_FST, x86.X86_INS_FSTP, x86.X86_INS_FADD, x86.X86_INS_FSUB,
    x86.X86_INS_FSUBR, x86.X86_INS_FMUL, x86.X86_INS_FDIV, x86.X86_INS_FDIVR, x86.X86_INS_FCOM,
    x86.X86_INS_FCOMP,
}
X87_INT = {x86.X86_INS_FILD, x86.X86_INS_FIST, x86.X86_INS_FISTP, x86.X86_INS_FIADD, x86.X86_INS_FIMUL}
CLOBBERED_BY_CALL = (x86.X86_REG_EAX, x86.X86_REG_ECX, x86.X86_REG_EDX)
WIDTH_TYPE = {1: "byte", 2: "word", 4: "dword", 8: "qword"}
TYPE_RANK = {"class": 0, "array": 0, "std_string": 0, "std_list": 0, "ptr": 1, "double": 2, "float": 2, "pointer": 3,
             "int": 4, "dword": 5, "qword": 5, "word": 5, "byte": 5}


def read_tsv(path):
    with open(path, encoding="utf-8") as f:
        lines = [l for l in f.read().splitlines() if l and not l.startswith("#")]
    header = lines[0].split("\t")
    return [dict(zip(header, line.split("\t"))) for line in lines[1:]]


class Access:
    __slots__ = ("off", "size", "type", "func", "insn", "write")

    def __init__(self, off, size, type_, func, insn, write):
        self.off, self.size, self.type, self.func, self.insn, self.write = off, size, type_, func, insn, write


def type_kind(t):
    return t.split(":")[0]


class Layouts:
    def __init__(self):
        self.bx = BinIndex()
        self.sizes = {r["class"]: int(r["size"]) for r in read_tsv(os.path.join(LABELS, "objects.classes.tsv"))}
        self.ctor_class = {}
        for r in read_tsv(os.path.join(LABELS, "objects.functions.tsv")):
            if not r["name"].startswith(("~", "`")):
                self.ctor_class[int(r["address"], 16)] = r["namespace"]
        self.vfptrs = defaultdict(set)
        for r in read_tsv(os.path.join(EXPORTS, "Ragexe.exe.vtables.tsv")):
            self.vfptrs[r["class"]].add(int(r["vfptr_offset"]))
        self.bases = {}
        for r in read_tsv(os.path.join(EXPORTS, "Ragexe.exe.rtti.tsv")):
            out = []
            for b in [b for b in r["bases"].split("|") if b]:
                name, disp = b.rsplit("@", 1)
                out.append((name, None if "v" in disp else int(disp)))
            self.bases[r["class"]] = out
        self.classes = set(self.bases)
        self.methods = defaultdict(list)
        for fn in self.bx.functions.values():
            if fn.kind == "code" and fn.namespace in self.classes:
                self.methods[fn.namespace].append(fn)
        self.raw = defaultdict(list)      # class -> [Access] at class-relative offsets
        self.indexed = defaultdict(Counter)
        self.notes = defaultdict(list)

    # -- access collection ---------------------------------------------------------

    def scan(self, cls, fn):
        """Collect [this + n] accesses in one __thiscall function."""
        this = {x86.X86_REG_ECX: 0}          # reg -> offset of the pointer from this
        slots = {}                           # ebp displacement -> offset from this
        loaded = {}                          # reg -> field offset its value was loaded from
        node = {}                            # reg -> fresh operator new block {size, links}
        ins = self.bx.insns(fn)
        out = self.raw[cls]

        def mem_off(op):
            if op.type != x86.X86_OP_MEM:
                return None
            if op.mem.base in this:
                if op.mem.index != 0:
                    self.indexed[cls][this[op.mem.base] + op.mem.disp] += 1
                    return None
                return this[op.mem.base] + op.mem.disp
            return None

        for k, i in enumerate(ins):
            ops = i.operands
            # stack slots holding this
            if i.id == x86.X86_INS_MOV and len(ops) == 2:
                d, s = ops
                if d.type == x86.X86_OP_MEM and d.mem.base == x86.X86_REG_EBP and s.type == x86.X86_OP_REG \
                        and s.reg in this and this[s.reg] == 0:
                    slots[d.mem.disp] = 0
                    continue
                if d.type == x86.X86_OP_REG and s.type == x86.X86_OP_MEM and s.mem.base == x86.X86_REG_EBP \
                        and s.mem.index == 0 and s.mem.disp in slots:
                    this[d.reg] = slots[s.mem.disp]
                    loaded.pop(d.reg, None)
                    continue
                if d.type == x86.X86_OP_REG and s.type == x86.X86_OP_REG:
                    if s.reg in this:
                        this[d.reg] = this[s.reg]
                    else:
                        this.pop(d.reg, None)
                    if s.reg in loaded:
                        loaded[d.reg] = loaded[s.reg]
                    else:
                        loaded.pop(d.reg, None)
                    if s.reg in node:
                        node[d.reg] = node[s.reg]
                    else:
                        node.pop(d.reg, None)
                    continue
                # a fresh node linked to itself: [node] = node, [node + 4] = node
                if d.type == x86.X86_OP_MEM and s.type == x86.X86_OP_REG and d.mem.base in node \
                        and d.mem.index == 0 and node.get(s.reg) is node[d.mem.base] and d.mem.disp in (0, 4):
                    node[d.mem.base]["links"] |= 1 << (d.mem.disp // 4)
                    continue
            if i.id == x86.X86_INS_LEA and ops[1].mem.base in this and ops[1].mem.index == 0:
                this[ops[0].reg] = this[ops[1].mem.base] + ops[1].mem.disp
                loaded.pop(ops[0].reg, None)
                continue

            # memory operands relative to this
            for n, op in enumerate(ops):
                off = mem_off(op)
                if off is None:
                    # a value loaded from a field, used as a pointer
                    if op.type == x86.X86_OP_MEM and op.mem.base in loaded and op.mem.base not in this:
                        f = loaded[op.mem.base]
                        out.append(Access(f, 4, "pointer", fn.entry, i.address, False))
                    continue
                if i.id in X87_MEM:
                    t = {4: "float", 8: "double"}.get(op.size, WIDTH_TYPE.get(op.size, "dword"))
                elif i.id in X87_INT:
                    t = "int"
                elif i.id == x86.X86_INS_MOV and n == 0 and len(ops) == 2 and ops[1].type == x86.X86_OP_IMM \
                        and op.size == 4 and off in self.vfptrs.get(cls, ()) and \
                        (ops[1].imm & 0xFFFFFFFF) >= self.bx.sections[".rdata"][0]:
                    continue  # the vtable store; ApplyRtti owns vftable fields
                else:
                    t = WIDTH_TYPE.get(op.size, "dword")
                if off in self.vfptrs.get(cls, ()) and (op.size or 4) == 4:
                    continue  # reading the vtable pointer for a virtual call
                if n == 0 and i.id == x86.X86_INS_MOV and len(ops) == 2 and ops[1].type == x86.X86_OP_REG \
                        and ops[1].reg in node and node[ops[1].reg]["links"] == 3 and off >= 4:
                    elem = node[ops[1].reg]["size"] - 8
                    out.append(Access(off - 4, 12, f"std_list:{elem}", fn.entry, i.address, True))
                write = n == 0 and i.id in (x86.X86_INS_MOV, x86.X86_INS_FSTP, x86.X86_INS_FST, x86.X86_INS_FISTP)
                out.append(Access(off, op.size or 4, t, fn.entry, i.address, write))
                if i.id == x86.X86_INS_MOV and n == 1 and ops[0].type == x86.X86_OP_REG and op.size == 4:
                    loaded[ops[0].reg] = off
                    this.pop(ops[0].reg, None)
                    break

            if i.id == x86.X86_INS_CALL:
                t = self.bx.call_target(i)
                self.call(cls, fn, i, ins, k, t, this, loaded)
                for r in CLOBBERED_BY_CALL:
                    this.pop(r, None)
                    loaded.pop(r, None)
                    node.pop(r, None)
                if t == OPERATOR_NEW:
                    for p in reversed(ins[max(0, k - 3):k]):
                        if p.id == x86.X86_INS_PUSH:
                            if p.operands[0].type == x86.X86_OP_IMM:
                                node[x86.X86_REG_EAX] = {"size": p.operands[0].imm, "links": 0}
                            break
                continue
            if i.id == x86.X86_INS_MOV and len(ops) == 2 and ops[0].type == x86.X86_OP_REG and \
                    ops[1].type == x86.X86_OP_MEM:
                if mem_off(ops[1]) is None:
                    this.pop(ops[0].reg, None)
                    loaded.pop(ops[0].reg, None)
                node.pop(ops[0].reg, None)
                continue
            try:
                _, written = i.regs_access()
            except Exception:
                written = []
            for r in written:
                this.pop(r, None)
                loaded.pop(r, None)
                node.pop(r, None)

    def call(self, cls, fn, i, ins, k, t, this, loaded):
        if not isinstance(t, int):
            return
        out = self.raw[cls]
        if x86.X86_REG_ECX in this:
            off = this[x86.X86_REG_ECX]
            if t in STRING_FUNCS:
                out.append(Access(off, 16, "std_string", fn.entry, i.address, False))
            elif t in self.ctor_class and off != 0 and (self.ctor_class[t], off) not in self.bases.get(cls, ()):
                m = self.ctor_class[t]
                out.append(Access(off, self.sizes.get(m, 0), f"class:{m}", fn.entry, i.address, True))
        elif x86.X86_REG_ECX in loaded:
            callee = self.bx.functions.get(t)
            if callee is not None and callee.namespace in self.classes:
                out.append(Access(loaded[x86.X86_REG_ECX], 4, f"ptr:{callee.namespace}", fn.entry, i.address, False))
        if t == EH_VECTOR_CTOR:
            args = []
            for p in reversed(ins[max(0, k - 8):k]):
                if p.id == x86.X86_INS_PUSH:
                    args.append(p)
                    if len(args) == 5:
                        break
            # pushed right to left: ptr, size, count, ctor, dtor
            if len(args) >= 4 and args[0].operands[0].type == x86.X86_OP_REG and args[0].operands[0].reg in this \
                    and all(a.operands[0].type == x86.X86_OP_IMM for a in args[1:4]):
                off = this[args[0].operands[0].reg]
                size, count, ctor = (a.operands[0].imm & 0xFFFFFFFF for a in args[1:4])
                elem = self.ctor_class.get(ctor)
                et = f"class:{elem}" if elem and self.sizes.get(elem) == size else f"bytes{size}"
                out.append(Access(off, size * count, f"array:{et}:{count}", fn.entry, i.address, True))

    # -- inheritance ---------------------------------------------------------------

    def depth_order(self):
        return sorted(self.classes, key=lambda c: (len(self.bases.get(c, [])), c))

    def extent(self, cls):
        """Size when known, else the end of the last field the class's own functions or
        its bases touch. Only the class's own functions count, so the estimate does not
        depend on the order in which derived accesses are attributed."""
        if cls in self.sizes:
            return self.sizes[cls]
        if cls in self.extents:
            return self.extents[cls]
        end = max((o + 4 for o in self.vfptrs.get(cls, ())), default=0)
        for b, disp in self.bases.get(cls, []):
            if disp is not None and b in self.classes:
                end = max(end, disp + self.extent(b))
        for a in self.raw[cls]:
            end = max(end, a.off + a.size)
        end = (end + 3) & ~3
        self.extents[cls] = end
        return end

    def attribute(self):
        """Move accesses that fall inside a base subobject to the base, base-relative."""
        self.own = defaultdict(list)
        self.extents = {}
        for cls in self.depth_order():
            regions = []
            for b, disp in self.bases.get(cls, []):
                if disp is None or b not in self.classes:
                    continue
                regions.append((disp, b))
            for a in self.raw[cls]:
                # the most-base class whose subobject contains the access: of the base
                # regions that contain it, the smallest
                owner = None
                for disp, b in regions:
                    e = self.extent(b)
                    if disp <= a.off and a.off + a.size <= disp + e and (owner is None or e < owner[2]):
                        owner = (disp, b, e)
                if owner is None:
                    self.own[cls].append(a)
                else:
                    disp, b, _ = owner
                    self.own[b].append(Access(a.off - disp, a.size, a.type, a.func, a.insn, a.write))

    # -- field resolution ------------------------------------------------------------

    def resolve(self, cls, lo, hi):
        """Non-overlapping fields for one class's own region [lo, hi)."""
        groups = defaultdict(list)
        for a in self.own[cls]:
            if lo <= a.off and a.off + a.size <= hi and a.size > 0:
                groups[(a.off, a.size, a.type)].append(a)
        # merge plain widths with typed uses of the same span
        cands = []
        by_span = defaultdict(list)
        for (off, size, t), accs in groups.items():
            by_span[(off, size)].append((t, accs))
        for (off, size), uses in by_span.items():
            uses.sort(key=lambda u: (TYPE_RANK[type_kind(u[0])], -len(u[1])))
            best, accs = uses[0]
            allacc = [a for _, al in uses for a in al]
            kinds = {type_kind(t) for t, _ in uses}
            if "float" in kinds and ("int" in kinds or "pointer" in kinds):
                self.notes[cls].append(f"0x{off:x} used as float and as {sorted(kinds - {'float'})}")
            cands.append((TYPE_RANK[type_kind(best)], -len(allacc), off, size, best, allacc))
        cands.sort()
        taken = []
        fields = []
        for _, _, off, size, t, accs in cands:
            if any(off < e and s < off + size for s, e in taken):
                continue
            taken.append((off, off + size))
            fields.append((off, size, t, accs))
        dropped = len(cands) - len(fields)
        if dropped:
            self.notes[cls].append(f"{dropped} overlapping uses dropped")
        return sorted(fields)

    def run(self):
        for cls in self.classes:
            for fn in self.methods[cls]:
                self.scan(cls, fn)
        self.attribute()
        self.own_fields = {}
        for cls in self.depth_order():
            self.own_fields[cls] = self.resolve(cls, 0, self.extent(cls))
        self.write()

    def flattened(self, cls, disp=0, seen=None):
        seen = seen if seen is not None else set()
        out = []
        for b, bdisp in self.bases.get(cls, []):
            if bdisp is None or (b, disp + bdisp) in seen:
                continue
            seen.add((b, disp + bdisp))
            for off, size, t, accs, origin in self.flattened_own(b):
                out.append((off + disp + bdisp, size, t, accs, origin))
        out.extend((off + disp, size, t, accs, cls) for off, size, t, accs in self.own_fields[cls])
        return out

    def flattened_own(self, cls):
        return [(off, size, t, accs, cls) for off, size, t, accs in self.own_fields[cls]]

    # -- output ------------------------------------------------------------------------

    def write(self):
        gen = "# generated by tools/recover_layouts.py; edit the script, not this file\n"
        report = []
        with open(os.path.join(LABELS, "layouts.fields.tsv"), "w", encoding="utf-8") as f:
            f.write("class\toffset\tsize\ttype\tname\tevidence\n" + gen)
            for cls in sorted(self.classes):
                ext = self.extent(cls)
                if ext == 0:
                    continue
                rows = {}
                spans = [(o, o + 4) for o in self.vfptrs.get(cls, ())]
                for off, size, t, accs, origin in sorted(self.flattened(cls), key=lambda r: (r[0], -r[1])):
                    if off + size > ext or any(off < e and s < off + size for s, e in spans):
                        self.notes[cls].append(f"0x{off:x} from {origin} overlaps another field")
                        continue
                    spans.append((off, off + size))
                    rows[off] = (size, t, accs, origin)
                covered = sum(s for s, _, _, _ in rows.values()) + 4 * len(self.vfptrs.get(cls, ()))
                for off in sorted(rows):
                    size, t, accs, origin = rows[off]
                    funcs = sorted({a.func for a in accs})
                    writes = sum(1 for a in accs if a.write)
                    where = f"{origin} field" if origin != cls else "own field"
                    ev = (f"{where}; {len(accs)} uses ({writes} writes) in {len(funcs)} functions, "
                          f"e.g. {accs[0].insn:08x} in {accs[0].func:08x}")
                    f.write(f"{cls}\t0x{off:x}\t{size}\t{t}\tfield_0x{off:x}\t{ev}\n")
                report.append((cls, ext, cls in self.sizes, len(rows), covered))
        with open(os.path.join(LABELS, "layouts.classes.tsv"), "w", encoding="utf-8") as f:
            f.write("class\tsize\tevidence\n" + gen)
            for cls, ext, known, _, _ in sorted(report):
                if not known:
                    f.write(f"{cls}\t{ext}\testimated: end of the last field the class or its bases use\n")
        with open(os.path.join(EXPORTS, "Ragexe.exe.layouts.tsv"), "w", encoding="utf-8") as f:
            f.write("class\tsize\tsize_source\tfields\tcovered_bytes\tcoverage\tindexed_bases\tnotes\n")
            for cls, ext, known, nf, cov in sorted(report):
                idx = ",".join(f"0x{o:x}" for o, _ in self.indexed[cls].most_common(6))
                f.write(f"{cls}\t{ext}\t{'new' if known else 'estimated'}\t{nf}\t{cov}\t{min(cov, ext) / ext:.2f}\t"
                        f"{idx}\t{' | '.join(sorted(set(self.notes[cls])))}\n")
        sized = [r for r in report if r[2]]
        print(f"classes with layouts: {len(report)} ({len(sized)} sized by new, {len(report) - len(sized)} estimated)")
        print(f"fields written: {sum(r[3] for r in report)}")
        cov = sorted(min(r[4], r[1]) / r[1] for r in sized)
        if cov:
            print(f"sized classes, byte coverage: median {cov[len(cov) // 2]:.0%}, "
                  f"quartiles {cov[len(cov) // 4]:.0%}-{cov[3 * len(cov) // 4]:.0%}")
        kinds = Counter()
        for cls in self.own_fields:
            for _, _, t, _ in self.own_fields[cls]:
                kinds[type_kind(t)] += 1
        print("own-field types:", dict(kinds.most_common()))


if __name__ == "__main__":
    Layouts().run()
