"""Recover class field layouts from how each class's own functions use `this`.

Sources, per class: its constructors and destructors (from recover_objects.py), its
virtual functions (from ApplyRtti), and the non-virtual methods found here, all
__thiscall. Every [this + n] memory operand is
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

Non-virtual methods: a default-named function that reads ECX before writing it (a
`push ecx` that only reserves stack space does not count) and is called with
ECX = this + n from a class's functions, or with ECX = the address of a global object
plus n from anywhere, is a method of:
  the class itself (n = 0), the most-derived base at n, or the std::string / std::list /
  std::_Tree member at n.
Global objects and their classes come from the type column of the data label files.

Global objects: an absolute memory operand [G + n] anywhere in the program, with G a
global object of class X, is an access to X's field at n, typed like the this-relative
ones, and a call with ECX = G + n to a container or member-constructor function places
that member at n. Only offsets inside X's size, or inside the extent X's own functions
establish, count: the globals that follow an object in .data are not part of it, and
nothing tells where an object of unknown size ends.
With several calling classes the owner is the most-derived class all of them derive
from; callers from unrelated classes leave the function unassigned. New methods are
scanned in turn, until no more are found.

std::_Tree (map and set) members: a function that takes the STL lock, allocates a node
with operator new and writes this + 4 (_Head) and this + 0xc (_Size) is an MSVC 6
_Tree::_Init. A function that calls an _Init on its own `this`, and writes nothing
past the tree's 16 bytes, is a tree constructor.
Calling either on this + n puts a 16-byte tree at n; the value size is the node size
minus 12 (left, parent, right) and 4 (color). Calling one with ECX = a global address
makes that global a tree. A call to any method already assigned to std::string,
std::list or std::_Tree on this + n also marks the member.

Classes without RTTI, and their constructors, come from the curated
analysis/labels/inferred.*.tsv files; they have no bases.

Inheritance: accesses inside a base class's extent belong to that base (they are
merged into the base's fields with the derived function as evidence). The extent is
the base's size when known, otherwise estimated from its own accesses. Every class
struct is written flattened: base fields at their offsets, then the class's own, so
the class's vftable field keeps the class's own vtable type.

Writes:
  analysis/labels/layouts.fields.tsv   class offset size type name evidence
  analysis/labels/layouts.classes.tsv  estimated sizes for classes never created with new
  analysis/labels/methods.functions.tsv  non-virtual methods and _Tree::_Init instances
  analysis/labels/layouts.data.tsv     global std::_Tree objects, typed
  analysis/exports/Ragexe.exe.layouts.tsv  per-class coverage and notes
"""
import os
import sys
from bisect import bisect_right
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from binindex import BinIndex, ROOT  # noqa: E402
import capstone  # noqa: E402
from capstone import x86  # noqa: E402

EXPORTS = os.path.join(ROOT, "analysis", "exports")
LABELS = os.path.join(ROOT, "analysis", "labels")

STRING_FUNCS = {0x423980, 0x424FA0, 0x425210}
OPERATOR_NEW = 0x5C183B
LOCKIT = 0x5B3267
UNLOCKIT = 0x5B3303
CONTAINERS = {"std_string": "std::string", "std_list": "std::list", "std_tree": "std::_Tree"}
CONTAINER_SIZE = {"std::string": 16, "std::list": 12, "std::_Tree": 16}
EH_VECTOR_CTOR = 0x5C2002
# the critical section _Lockit enters
LOCK_GLOBALS = {0x688A88}

X87_MEM = {
    x86.X86_INS_FLD, x86.X86_INS_FST, x86.X86_INS_FSTP, x86.X86_INS_FADD, x86.X86_INS_FSUB,
    x86.X86_INS_FSUBR, x86.X86_INS_FMUL, x86.X86_INS_FDIV, x86.X86_INS_FDIVR, x86.X86_INS_FCOM,
    x86.X86_INS_FCOMP,
}
X87_INT = {x86.X86_INS_FILD, x86.X86_INS_FIST, x86.X86_INS_FISTP, x86.X86_INS_FIADD, x86.X86_INS_FIMUL}
CLOBBERED_BY_CALL = (x86.X86_REG_EAX, x86.X86_REG_ECX, x86.X86_REG_EDX)
WIDTH_TYPE = {1: "byte", 2: "word", 4: "dword", 8: "qword"}
TYPE_RANK = {"class": 0, "array": 0, "std_string": 0, "std_list": 0, "std_tree": 0, "ptr": 1, "double": 2, "float": 2, "pointer": 3,
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
        curated = read_tsv(os.path.join(LABELS, "inferred.classes.tsv"))
        self.sizes.update({r["class"]: int(r["size"]) for r in curated if r["size"]})
        self.ctor_class = {}
        for r in read_tsv(os.path.join(LABELS, "objects.functions.tsv")):
            if not r["name"].startswith(("~", "`")):
                self.ctor_class[int(r["address"], 16)] = r["namespace"]
        curated_names = {r["class"] for r in curated}
        for r in read_tsv(os.path.join(LABELS, "inferred.functions.tsv")):
            if r["namespace"] in curated_names and r["name"] == r["namespace"]:
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
        for c in curated_names:
            self.bases.setdefault(c, [])
        self.classes = set(self.bases)
        self.methods = defaultdict(list)
        for fn in self.bx.functions.values():
            if fn.kind == "code" and fn.namespace in self.classes:
                self.methods[fn.namespace].append(fn)
        self.this_calls = defaultdict(list)   # callee -> [(class, offset of ECX from this, caller)]
        self.assigned = {}                    # callee -> (owner namespace, evidence)
        self.tree_statics = set()             # each _Tree instantiation's nil node and its count
        self.tree_inits = self.find_tree_inits()
        self.tree_ctors = self.find_tree_ctors()
        self.tree_nodes = {**self.tree_inits, **self.tree_ctors}
        self.curated_data = {int(r["address"], 16) for r in read_tsv(os.path.join(LABELS, "inferred.data.tsv"))}
        self.curated_funcs = {int(r["address"], 16) for name in ("crt.functions.tsv", "inferred.functions.tsv")
                              for r in read_tsv(os.path.join(LABELS, name))}
        self.globals = self.global_objects()
        self.global_calls = self.find_global_calls()
        # classes whose object starts with a tree: a global of the class is initialised as one
        self.tree_at0 = {cls for g, cls in self.globals.items() if g in self.global_trees() and cls != "std::_Tree"}
        self.uses_tree_statics = set()
        for fn in self.bx.functions.values():
            if fn.kind == "code" and fn.entry not in self.tree_inits and any(
                    v in self.tree_statics for i in self.bx.insns(fn) for v in self.bx.data_refs(i)):
                self.uses_tree_statics.add(fn.entry)
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
            self.this_calls[t].append((cls, off, fn.entry))
            self.member_call(cls, off, t, fn.entry, i.address)
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

    # -- std::_Tree::_Init ---------------------------------------------------------

    def find_tree_inits(self):
        """node size by _Init address; see the module docstring for the shape."""
        out = {}
        for fn in self.bx.functions.values():
            if fn.kind != "code" or fn.size > 400:
                continue
            ins = self.bx.insns(fn)
            targets = [self.bx.call_target(i) for i in ins if i.id == x86.X86_INS_CALL]
            if LOCKIT not in targets or UNLOCKIT not in targets or OPERATOR_NEW not in targets:
                continue
            this, wrote, node = {x86.X86_REG_ECX}, set(), None
            for k, i in enumerate(ins):
                ops = i.operands
                if i.id == x86.X86_INS_CALL:
                    if self.bx.call_target(i) == OPERATOR_NEW and node is None and k > 0 and \
                            ins[k - 1].id == x86.X86_INS_PUSH and ins[k - 1].operands[0].type == x86.X86_OP_IMM:
                        node = ins[k - 1].operands[0].imm
                    this -= set(CLOBBERED_BY_CALL)
                    continue
                if i.id == x86.X86_INS_MOV and len(ops) == 2:
                    d, src = ops
                    if d.type == x86.X86_OP_REG and src.type == x86.X86_OP_REG:
                        (this.add if src.reg in this else this.discard)(d.reg)
                        continue
                    if d.type == x86.X86_OP_MEM and d.mem.base in this and d.mem.index == 0:
                        wrote.add(d.mem.disp)
                        continue
                try:
                    for r in i.regs_access()[1]:
                        this.discard(r)
                except Exception:
                    pass
            if {4, 0xC} <= wrote and node:
                out[fn.entry] = node
                for i in ins:
                    for v in self.bx.data_refs(i):
                        if self.bx.section_of(v) == ".data" and v not in LOCK_GLOBALS:
                            self.tree_statics.add(v)
        return out

    def ecx_before(self, ins, k):
        """('global', addr) when the closest write to ECX before instruction k, within ten
        instructions and with no call or branch target in between, is mov ecx, addr with
        addr in .data; otherwise None."""
        for p in reversed(ins[max(0, k - 10):k]):
            ops = p.operands
            if p.id in (x86.X86_INS_CALL, x86.X86_INS_RET, x86.X86_INS_JMP):
                return None
            if p.id == x86.X86_INS_MOV and len(ops) == 2 and ops[0].type == x86.X86_OP_REG \
                    and ops[0].reg == x86.X86_REG_ECX:
                if ops[1].type == x86.X86_OP_IMM and self.bx.section_of(ops[1].imm & 0xFFFFFFFF) == ".data":
                    return ("global", ops[1].imm & 0xFFFFFFFF)
                return None
            try:
                if x86.X86_REG_ECX in p.regs_access()[1]:
                    return None
            except Exception:
                return None
        return None

    def global_objects(self):
        """Global address -> class, from the type column of the data label files (other
        than this script's own) and the global trees found here."""
        out = {}
        for name in sorted(os.listdir(LABELS)):
            if not name.endswith(".data.tsv") or name == "layouts.data.tsv":
                continue
            for r in read_tsv(os.path.join(LABELS, name)):
                t = r.get("type", "")
                if t.startswith("class:"):
                    out[int(r["address"], 16)] = t[6:]
                elif t == "std_tree":
                    out[int(r["address"], 16)] = "std::_Tree"
        for g in self.global_trees():
            out.setdefault(g, "std::_Tree")
        return out

    def find_global_calls(self):
        """(callee, class, offset, caller) for every call made with ECX = an address inside
        a global object: its own address, or one inside its known size."""
        starts = sorted(self.globals)
        out = []
        for fn in self.bx.functions.values():
            if fn.kind != "code":
                continue
            ins = self.bx.insns(fn)
            for k, i in enumerate(ins):
                if i.id != x86.X86_INS_CALL:
                    continue
                t = self.bx.call_target(i)
                held = self.ecx_before(ins, k) if isinstance(t, int) else None
                if not held:
                    continue
                j = bisect_right(starts, held[1]) - 1
                if j < 0:
                    continue
                cls = self.globals[starts[j]]
                off = held[1] - starts[j]
                out.append((t, cls, off, fn.entry, i.address))
        return out

    def find_tree_ctors(self):
        """Functions that call an _Init with ECX still holding their own this, and write
        nothing past the tree's 16 bytes."""
        callees, callers = self.bx.build_calls()
        out = {}
        for t, node in self.tree_inits.items():
            for site, c in callers.get(t, []):
                fn = self.bx.functions[c]
                if c in self.tree_inits or fn.kind != "code" or not self.is_thiscall(fn):
                    continue
                if self.writes_past(fn, 16):
                    continue
                this = {x86.X86_REG_ECX}
                for i in self.bx.insns(fn):
                    if i.address == site:
                        if x86.X86_REG_ECX in this:
                            out[c] = node
                        break
                    ops = i.operands
                    if i.id == x86.X86_INS_MOV and len(ops) == 2 and ops[0].type == x86.X86_OP_REG \
                            and ops[1].type == x86.X86_OP_REG:
                        (this.add if ops[1].reg in this else this.discard)(ops[0].reg)
                        continue
                    if i.id == x86.X86_INS_CALL:
                        this -= set(CLOBBERED_BY_CALL)
                        continue
                    try:
                        for r in i.regs_access()[1]:
                            this.discard(r)
                    except Exception:
                        pass
        return out

    def writes_past(self, fn, size):
        """True when the function writes [this + n] with n >= size, this being ECX on entry
        or a register copy of it."""
        this = {x86.X86_REG_ECX}
        for i in self.bx.insns(fn):
            ops = i.operands
            if ops and ops[0].type == x86.X86_OP_MEM and ops[0].access & capstone.CS_AC_WRITE \
                    and ops[0].mem.base in this and ops[0].mem.index == 0 and ops[0].mem.disp >= size:
                return True
            if i.id == x86.X86_INS_MOV and len(ops) == 2 and ops[0].type == x86.X86_OP_REG \
                    and ops[1].type == x86.X86_OP_REG:
                (this.add if ops[1].reg in this else this.discard)(ops[0].reg)
                continue
            if i.id == x86.X86_INS_CALL:
                this -= set(CLOBBERED_BY_CALL)
                continue
            try:
                for r in i.regs_access()[1]:
                    this.discard(r)
            except Exception:
                pass
        return False

    def global_trees(self):
        """Globals passed as ECX to an _Init or tree constructor, by a call or by the tail
        jump of an initializer stub."""
        if getattr(self, "_global_trees", None) is not None:
            return self._global_trees
        out = {}
        for fn in self.bx.functions.values():
            if fn.kind != "code":
                continue
            ins = self.bx.insns(fn)
            for k, i in enumerate(ins):
                t = self.bx.call_target(i) if i.id == x86.X86_INS_CALL else \
                    self.bx.jump_target(i) if i.id == x86.X86_INS_JMP else None
                if t not in self.tree_nodes:
                    continue
                held = self.ecx_before(ins, k)
                if held and held[0] == "global":
                    out[held[1]] = (self.tree_nodes[t], t, fn.entry)
        self._global_trees = out
        return out

    # -- non-virtual methods -------------------------------------------------------

    def is_thiscall(self, fn):
        """True when the function reads ECX before writing it."""
        for i in self.bx.insns(fn):
            ops = i.operands
            if i.id == x86.X86_INS_PUSH and ops and ops[0].type == x86.X86_OP_REG and ops[0].reg == x86.X86_REG_ECX:
                continue  # push ecx reserves a stack slot in MSVC 6 prologues
            if i.id in (x86.X86_INS_XOR, x86.X86_INS_SUB) and len(ops) == 2 and ops[0].type == x86.X86_OP_REG \
                    and ops[1].type == x86.X86_OP_REG and ops[0].reg == ops[1].reg:
                if ops[0].reg == x86.X86_REG_ECX:
                    return False
                continue
            try:
                read, written = i.regs_access()
            except Exception:
                return False
            if x86.X86_REG_ECX in read:
                return True
            if x86.X86_REG_ECX in written or i.id in (x86.X86_INS_CALL, x86.X86_INS_RET, x86.X86_INS_JMP):
                return False
        return False

    def owner_at(self, cls, off, callee=None):
        if off == 0:
            # a class that starts with a tree: the tree's own methods use its nil node
            if callee in self.uses_tree_statics and (cls in self.tree_at0 or any(
                    a.off == 0 and type_kind(a.type) == "std_tree" for a in self.raw[cls])):
                return "std::_Tree"
            return cls
        for b, disp in self.bases.get(cls, []):
            if disp == off and b in self.classes:
                return b  # CHD order lists the most-derived base first
        for a in self.raw[cls]:
            if a.off == off and type_kind(a.type) in CONTAINERS:
                return CONTAINERS[type_kind(a.type)]
        return None

    def common_base(self, classes):
        first = sorted(classes)[0]
        for cand in [first] + [b for b, _ in self.bases.get(first, [])]:
            if all(c == cand or cand in (b for b, _ in self.bases.get(c, [])) for c in classes):
                return cand
        return None

    def discover(self):
        """Scan, assign, repeat until the assignment stops changing. Every round starts from
        a fresh scan and recomputes every owner from all callers seen so far, so a method
        first seen from one derived class moves up once callers from its siblings appear.
        Names from earlier runs (method_<addr>) are ignored, so the result does not depend
        on what is already in Ghidra."""
        base = {cls: [fn for fn in fns if not fn.name.startswith("method_")] for cls, fns in self.methods.items()}
        self.assigned = {}
        for rounds in range(1, 16):
            self.raw = defaultdict(list)
            self.this_calls = defaultdict(list)
            self.indexed = defaultdict(Counter)
            self.notes.pop("(unassigned)", None)
            methods = defaultdict(dict)
            for cls, fns in base.items():
                for fn in fns:
                    methods[cls][fn.entry] = fn
            for t, (owner, _) in self.assigned.items():
                if owner in self.classes:
                    methods[owner][t] = self.bx.functions[t]
            for cls in sorted(methods):
                for fn in methods[cls].values():
                    self.scan(cls, fn)
            for t, cls, off, caller, _ in self.global_calls:
                if off == 0 or off < self.sizes.get(cls, CONTAINER_SIZE.get(cls, 0)):
                    self.this_calls[t].append((cls, off, caller))
            new = self.assign()
            if {t: o for t, (o, _) in new.items()} == {t: o for t, (o, _) in self.assigned.items()}:
                self.assigned = new
                break
            self.assigned = new
        else:
            self.notes["(unassigned)"].append("method assignment did not settle in 15 rounds")
        self.methods = {cls: list(fns.values()) for cls, fns in methods.items()}
        self.rounds = rounds

    def assign(self):
        out = {}
        for t, calls in sorted(self.this_calls.items()):
            if t in self.tree_nodes or t in STRING_FUNCS or t in self.curated_funcs:
                continue
            fn = self.bx.functions.get(t)
            ours = fn is not None and fn.name == f"method_{t:08x}"  # named by an earlier run
            if fn is None or fn.kind != "code" or (fn.library and not ours) or not self.is_thiscall(fn) \
                    or not (ours or (fn.namespace == "Global" and fn.name.startswith("FUN_"))):
                continue
            owners = {self.owner_at(cls, off, t) for cls, off, _ in calls}
            if None in owners:
                continue
            containers = owners & set(CONTAINERS.values())
            if containers:
                if len(owners) != 1:
                    continue
                owner = next(iter(owners))
            else:
                owner = self.common_base(owners)
                if owner is None:
                    self.notes["(unassigned)"].append(f"{t:08x} called with this from unrelated {sorted(owners)}")
                    continue
            callers = sorted({c for _, _, c in calls})
            via = sorted({cls for cls, _, _ in calls})
            out[t] = (owner, f"__thiscall; called with this from {len(callers)} functions of "
                             f"{', '.join(via[:4])}{' and more' if len(via) > 4 else ''}, e.g. {callers[0]:08x}")
        return out

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

    def global_accesses(self):
        """Add [G + n] operands and member calls on global objects to their classes' raw
        accesses, within each class's size or the extent its own functions establish."""
        self.extents = {}
        limits = {}
        for g, cls in self.globals.items():
            if cls in self.classes:
                limits[g] = (cls, self.extent(cls))
        starts = sorted(limits)
        before = {cls: len(self.raw[cls]) for cls, _ in limits.values()}

        def place(v):
            j = bisect_right(starts, v) - 1
            if j < 0:
                return None
            cls, end = limits[starts[j]]
            off = v - starts[j]
            return (cls, off) if off < end else None

        for fn in self.bx.functions.values():
            if fn.kind != "code" or fn.library:
                continue
            loaded = {}                      # reg -> (class, offset of the field its value came from)
            for i in self.bx.insns(fn):
                ops = i.operands
                load = None
                if i.id != x86.X86_INS_LEA:
                    for n, op in enumerate(ops):
                        if op.type != x86.X86_OP_MEM:
                            continue
                        if op.mem.base in loaded:
                            cls, f = loaded[op.mem.base]
                            self.raw[cls].append(Access(f, 4, "pointer", fn.entry, i.address, False))
                            continue
                        if op.mem.base != 0 or op.mem.segment != 0:
                            continue
                        at = place(op.mem.disp & 0xFFFFFFFF)
                        if at is None:
                            continue
                        cls, off = at
                        if op.mem.index != 0:
                            self.indexed[cls][off] += 1
                            continue
                        if off in self.vfptrs.get(cls, ()) and (op.size or 4) == 4:
                            continue
                        if i.id in X87_MEM:
                            t = {4: "float", 8: "double"}.get(op.size, WIDTH_TYPE.get(op.size, "dword"))
                        elif i.id in X87_INT:
                            t = "int"
                        else:
                            t = WIDTH_TYPE.get(op.size, "dword")
                        write = n == 0 and i.id in (x86.X86_INS_MOV, x86.X86_INS_FSTP, x86.X86_INS_FST,
                                                    x86.X86_INS_FISTP)
                        self.raw[cls].append(Access(off, op.size or 4, t, fn.entry, i.address, write))
                        if i.id == x86.X86_INS_MOV and n == 1 and ops[0].type == x86.X86_OP_REG and op.size == 4:
                            load = (ops[0].reg, (cls, off))
                if i.id == x86.X86_INS_CALL:
                    t = self.bx.call_target(i)
                    callee = self.bx.functions.get(t) if isinstance(t, int) else None
                    if x86.X86_REG_ECX in loaded and callee is not None and callee.namespace in self.classes:
                        cls, f = loaded[x86.X86_REG_ECX]
                        self.raw[cls].append(Access(f, 4, f"ptr:{callee.namespace}", fn.entry, i.address, False))
                    written = CLOBBERED_BY_CALL
                elif i.id == x86.X86_INS_MOV and len(ops) == 2 and ops[0].type == x86.X86_OP_REG \
                        and ops[1].type == x86.X86_OP_REG:
                    written = ()
                    if ops[1].reg in loaded:
                        loaded[ops[0].reg] = loaded[ops[1].reg]
                    else:
                        loaded.pop(ops[0].reg, None)
                else:
                    try:
                        written = i.regs_access()[1]
                    except Exception:
                        written = ()
                for r in written:
                    loaded.pop(r, None)
                if load:
                    loaded[load[0]] = load[1]
        for t, cls, off, caller, site in self.global_calls:
            if 0 < off and cls in self.classes and off < self.extent(cls):
                self.member_call(cls, off, t, caller, site)
        self.global_access_counts = {cls: len(self.raw[cls]) - n for cls, n in before.items()}

    def member_call(self, cls, off, t, func, insn):
        """A call with ECX = this + off: a container method or member constructor puts that
        member at off."""
        out = self.raw[cls]
        owner = self.assigned.get(t, (None,))[0]
        if t in self.tree_nodes:
            out.append(Access(off, 16, f"std_tree:{self.tree_nodes[t] - 16}", func, insn, True))
        elif owner in CONTAINER_SIZE and owner != "std::_Tree":
            kind = next(k for k, v in CONTAINERS.items() if v == owner)
            out.append(Access(off, CONTAINER_SIZE[owner], kind, func, insn, False))
        elif owner == "std::_Tree":
            out.append(Access(off, 16, "std_tree", func, insn, False))
        elif t in STRING_FUNCS:
            out.append(Access(off, 16, "std_string", func, insn, False))
        elif t in self.ctor_class and off != 0 and (self.ctor_class[t], off) not in self.bases.get(cls, ()):
            m = self.ctor_class[t]
            out.append(Access(off, self.sizes.get(m, 0), f"class:{m}", func, insn, True))

    def run(self):
        self.discover()
        self.global_accesses()
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
        with open(os.path.join(LABELS, "methods.functions.tsv"), "w", encoding="utf-8") as f:
            f.write("address\tnamespace\tname\tcalling_convention\tevidence\n" + gen)
            for t, node in sorted(self.tree_inits.items()):
                f.write(f"{t:08x}\tstd::_Tree\t_Init\t__thiscall\tunder the STL lock creates the shared nil node, "
                        f"allocates a head node (operator new({node})) into this + 4 and zeroes the size at this + 0xc\n")
            for t, node in sorted(self.tree_ctors.items()):
                f.write(f"{t:08x}\tstd::_Tree\t_Tree\t__thiscall\tcalls a _Tree::_Init (node {node} bytes) on its own this\n")
            for t, (owner, ev) in sorted(self.assigned.items()):
                f.write(f"{t:08x}\t{owner}\tmethod_{t:08x}\t__thiscall\t{ev}\n")
        trees = {g: v for g, v in self.global_trees().items() if g not in self.curated_data}
        with open(os.path.join(LABELS, "layouts.data.tsv"), "w", encoding="utf-8") as f:
            f.write("address\tnamespace\tname\tevidence\ttype\n" + gen)
            for g, (node, t, c) in sorted(trees.items()):
                f.write(f"{g:08x}\tGlobal\tg_tree_{g:08x}\t{c:08x} calls {t:08x} with ECX = this global; "
                        f"value size {node - 16}\tstd_tree\n")
        by_owner = Counter("container" if o in CONTAINERS.values() else "class" for o, _ in self.assigned.values())
        print(f"calls through global objects: {len(self.global_calls)} sites on {len(self.globals)} globals; "
              f"direct accesses and member calls added: {sum(self.global_access_counts.values())} "
              f"to {sum(1 for n in self.global_access_counts.values() if n)} classes")
        print(f"non-virtual methods assigned: {len(self.assigned)} ({dict(by_owner)}) in {self.rounds} rounds; "
              f"_Tree::_Init instances: {len(self.tree_inits)}, tree constructors: {len(self.tree_ctors)}, "
              f"global trees: {len(trees)}")
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
