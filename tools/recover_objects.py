"""Recover constructors, destructors, object sizes and global objects.

Reads the binary through binindex plus the ApplyRtti exports, and writes label files
for ApplyLabels.java and a per-class report:

  analysis/labels/objects.functions.tsv     constructors, destructors, deleting destructors
  analysis/labels/initializers.functions.tsv  the C++ static initializer table entries
  analysis/labels/globals.data.tsv          global objects built by static initializers
                                            or on first use
  analysis/labels/objects.classes.tsv       class sizes
  analysis/exports/Ragexe.exe.objects.tsv   per-class evidence and conflicts

Rules, in order of strength:
  1. A vtable slot function that tests bit 0 (scalar) or bit 1 (vector) of its stack
     argument and calls operator delete is a deleting destructor of the slot's owner.
     The function it calls first, on `this`, is that class's destructor.
  2. A static initializer stub `mov ecx, G; call/jmp F` constructs the global G with F;
     the matching atexit stub `mov ecx, G; jmp D` destroys it with D. Elsewhere in the
     game's code, `mov ecx, G; call F` with F a constructor found by the other rules
     constructs G on first use (a function-local static); a vtable stored into [G]
     right after the call, by an inlined derived constructor, decides the class. The
     atexit stubs such code registers name G's destructor as in the initializers.
  3. A function that stores a vtable into `this` (ECX on entry, or a copy of it) and
     whose last such store at the vfptr offset is the class's own vtable, and which
     returns `this`, is a constructor. A destructor stores its own vtable first.
  4. `push n; call operator new` followed by a call on the result (ECX = EAX) gives
     sizeof = n for the class constructed by the callee. An inlined constructor that
     stores a vtable into the result gives the same.

Classes without RTTI come from the curated analysis/labels/inferred.*.tsv files: a
function row named after its class is that class's constructor, and rules 2 and 4 use
it like a recovered one. A curated size wins over operator new, and a curated global
name wins over a generated one; globals built by STL code (std::_Tree) are left to
recover_layouts.py.
"""
import os
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from binindex import BinIndex, ROOT  # noqa: E402
from capstone import x86  # noqa: E402

EXPORTS = os.path.join(ROOT, "analysis", "exports")
LABELS = os.path.join(ROOT, "analysis", "labels")

OPERATOR_NEW = 0x5C183B
OPERATOR_NEW_NOTHROW = 0x5B7CFE
OPERATOR_DELETE = 0x5B325C
ATEXIT = 0x5C18C7
EH_VECTOR_DTOR = 0x5C1F0E
CXX_INIT_TABLE = (0x605000, 0x605340)

SCALAR_DD = "`scalar_deleting_destructor'"
VECTOR_DD = "`vector_deleting_destructor'"


def short_name(cls):
    """Last component of a :: path, ignoring :: inside template arguments."""
    depth, start = 0, 0
    for k, ch in enumerate(cls):
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        elif depth == 0 and cls.startswith("::", k):
            start = k + 2
    return cls[start:]


def read_tsv(path):
    with open(path, encoding="utf-8") as f:
        lines = f.read().splitlines()
    header = lines[0].split("\t")
    return [dict(zip(header, line.split("\t"))) for line in lines[1:] if line and not line.startswith("#")]


class Recovery:
    def __init__(self):
        self.bx = BinIndex()
        self.callees, self.callers = self.bx.build_calls()
        self.vtables = {}           # vtable address -> (class, vfptr offset)
        self.slot_owner = {}        # slot target -> set of classes whose vtables hold it
        for r in read_tsv(os.path.join(EXPORTS, "Ragexe.exe.vtables.tsv")):
            vt = int(r["vftable"], 16)
            self.vtables[vt] = (r["class"], int(r["vfptr_offset"]))
            self.slot_owner.setdefault(int(r["target"], 16), set()).add(r["class"])
        self.bases = {}
        for r in read_tsv(os.path.join(EXPORTS, "Ragexe.exe.rtti.tsv")):
            self.bases[r["class"]] = [b.split("@")[0] for b in r["bases"].split("|") if b]
        self.names = {}             # address -> (namespace, name, evidence)
        self.sizes = defaultdict(Counter)   # class -> Counter(size -> sites)
        self.size_sites = defaultdict(list)
        self.globals = {}           # global address -> (class, constructor, evidence)
        self.local_statics = set()  # globals constructed on first use, not by an initializer
        self.notes = defaultdict(list)
        self.ctors = defaultdict(set)
        self.dtors = defaultdict(set)
        self.deleting = defaultdict(set)
        self.curated = {}           # class without RTTI -> curated size or None
        for r in read_tsv(os.path.join(LABELS, "inferred.classes.tsv")):
            self.curated[r["class"]] = int(r["size"]) if r["size"] else None
        self.curated_ctors = {}     # constructor -> class without RTTI
        for r in read_tsv(os.path.join(LABELS, "inferred.functions.tsv")):
            if r["namespace"] in self.curated and r["name"] == short_name(r["namespace"]):
                self.curated_ctors[int(r["address"], 16)] = r["namespace"]
                self.ctors[r["namespace"]].add(int(r["address"], 16))
        self.curated_data = {int(r["address"], 16) for r in read_tsv(os.path.join(LABELS, "inferred.data.tsv"))}
        self.curated_funcs = {int(r["address"], 16) for name in ("crt.functions.tsv", "inferred.functions.tsv")
                              for r in read_tsv(os.path.join(LABELS, name))}

    # -- helpers ---------------------------------------------------------------------

    def derives(self, derived, base):
        return derived == base or base in self.bases.get(derived, [])

    def most_derived(self, classes):
        for c in classes:
            if all(self.derives(c, o) for o in classes):
                return c
        return None

    def disasm_at(self, addr, limit=64):
        """Linear code at an address that may not be a known function, stopping after ret."""
        off = addr - self.bx.base
        out = []
        for i in self.bx.md.disasm(self.bx.image[off:off + 512], addr):
            out.append(i)
            if i.id == x86.X86_INS_RET or len(out) >= limit:
                break
        return out

    def insns_of(self, addr):
        fn = self.bx.functions.get(addr)
        return self.bx.insns(fn) if fn else self.disasm_at(addr)

    def name(self, addr, namespace, name, evidence):
        if addr in self.curated_funcs:
            return
        prev = self.names.get(addr)
        if prev and prev[:2] != (namespace, name):
            # A weaker rule that only sees a base vtable agrees with a more specific name.
            if not self.derives(prev[0], namespace):
                self.notes[namespace].append(f"{addr:08x} named both {prev[0]}::{prev[1]} and {name}")
            return
        self.names[addr] = (namespace, name, evidence)

    @staticmethod
    def is_vtable_store(insn):
        """mov dword ptr [reg + disp], imm: returns (base reg id, disp, imm) or None."""
        if insn.id != x86.X86_INS_MOV or len(insn.operands) != 2:
            return None
        dst, src = insn.operands
        if dst.type != x86.X86_OP_MEM or src.type != x86.X86_OP_IMM or dst.size != 4:
            return None
        if dst.mem.base == 0 or dst.mem.index != 0:
            return None
        return dst.mem.base, dst.mem.disp, src.imm & 0xFFFFFFFF

    def this_analysis(self, insns):
        """Track copies of ECX-on-entry; returns (vtable stores into this, returns_this)."""
        this = {x86.X86_REG_ECX}
        copies = set(this)
        stores = []
        returns_this = False
        for k, i in enumerate(insns):
            if i.id == x86.X86_INS_RET and x86.X86_REG_EAX in this:
                returns_this = True
            st = self.is_vtable_store(i)
            if st and st[0] in this and st[2] in self.vtables:
                stores.append((i.address, st[1], st[2]))
            if i.id == x86.X86_INS_MOV and len(i.operands) == 2 and \
                    i.operands[0].type == x86.X86_OP_REG and i.operands[1].type == x86.X86_OP_REG:
                d, s = i.operands[0].reg, i.operands[1].reg
                if s in this:
                    this.add(d)
                    copies.add(d)
                    if d == x86.X86_REG_EAX and any(n.id == x86.X86_INS_RET for n in insns[k + 1:k + 8]):
                        returns_this = True
                    continue
                this.discard(d)
                continue
            if i.id == x86.X86_INS_CALL:
                for r in (x86.X86_REG_EAX, x86.X86_REG_ECX, x86.X86_REG_EDX):
                    this.discard(r)
                continue
            try:
                _, written = i.regs_access()
            except Exception:
                written = []
            for r in written:
                this.discard(r)
        return stores, returns_this

    # -- rule 1: deleting destructors ------------------------------------------------

    def deleting_destructors(self):
        for target, owners in self.slot_owner.items():
            fn = self.bx.functions.get(target)
            if fn is None or fn.size > 96:
                continue
            ins = self.bx.insns(fn)
            calls = [(k, self.bx.call_target(i)) for k, i in enumerate(ins) if i.id == x86.X86_INS_CALL]
            if not any(t == OPERATOR_DELETE for _, t in calls):
                continue
            flag = None
            for i in ins:
                if i.id == x86.X86_INS_TEST and len(i.operands) == 2 and i.operands[0].type == x86.X86_OP_MEM \
                        and i.operands[1].type == x86.X86_OP_IMM:
                    flag = i.operands[1].imm
                    break
            if flag not in (1, 2):
                continue
            cls = self.most_derived(sorted(owners))
            if cls is None:
                continue
            vector = flag == 2 or any(t == EH_VECTOR_DTOR for _, t in calls)
            self.name(target, cls, VECTOR_DD if vector else SCALAR_DD,
                      f"vtable slot of {cls}; tests bit {0 if flag == 1 else 1} of the flags argument and calls operator delete")
            self.deleting[cls].add(target)
            first = next((t for _, t in calls if isinstance(t, int) and t not in (OPERATOR_DELETE, EH_VECTOR_DTOR)), None)
            if first is not None and not vector:
                short = short_name(cls)
                self.name(first, cls, "~" + short, f"called on this by {cls}::{SCALAR_DD} {target:08x} before operator delete")
                self.dtors[cls].add(first)

    # -- rule 2: static initializers and global objects --------------------------------

    def initializers(self):
        """Table entries are `call A; jmp B` pairs or single stubs. A construction stub is
        `mov ecx, G; call/jmp F`; a registration stub is `push D; call atexit`, and D is
        itself `mov ecx, G; jmp F` with F the destructor."""
        lo, hi = CXX_INIT_TABLE
        self.initializer_rows = []
        atexit_args = set()
        for idx, slot in enumerate(range(lo, hi, 4)):
            entry = self.bx.u32(slot)
            if not entry:
                continue
            self.initializer_rows.append((entry, idx))
            for body in self.stub_bodies(entry):
                ins = self.disasm_at(body, 16)
                for k, i in enumerate(ins):
                    if self.bx.call_target(i) == ATEXIT and k > 0 and ins[k - 1].id == x86.X86_INS_PUSH \
                            and ins[k - 1].operands[0].type == x86.X86_OP_IMM:
                        atexit_args.add(ins[k - 1].operands[0].imm & 0xFFFFFFFF)
                self.global_from_stub(body, entry, "ctor")
        for stub in sorted(atexit_args):
            self.global_from_stub(stub, stub, "dtor")

    def stub_bodies(self, entry):
        out = [entry]
        for i in self.disasm_at(entry, 2):
            t = self.bx.call_target(i) if i.id == x86.X86_INS_CALL else self.bx.jump_target(i)
            if isinstance(t, int) and t != ATEXIT and self.bx.section_of(t) == ".text":
                out.append(t)
        return out

    def global_from_stub(self, stub, entry, role):
        ins = self.disasm_at(stub, 16)
        for k, i in enumerate(ins[:-1]):
            if i.id != x86.X86_INS_MOV or len(i.operands) != 2:
                continue
            d, s = i.operands
            if d.type != x86.X86_OP_REG or d.reg != x86.X86_REG_ECX or s.type != x86.X86_OP_IMM:
                continue
            g = s.imm & 0xFFFFFFFF
            if self.bx.section_of(g) != ".data":
                continue
            nxt = ins[k + 1]
            t = self.bx.call_target(nxt) if nxt.id == x86.X86_INS_CALL else self.bx.jump_target(nxt)
            if not isinstance(t, int):
                continue
            if t in self.curated_ctors:
                if role == "ctor":
                    self.globals[g] = (self.curated_ctors[t], t, entry)
                return
            stores, _ = self.this_analysis(self.insns_of(t))
            classes = [self.vtables[v][0] for _, off, v in stores if off == 0]
            cls = self.most_derived(classes) if classes else None
            if t in self.names and self.names[t][1].lstrip("~") == short_name(self.names[t][0]):
                cls = self.names[t][0]
            if role == "ctor":
                prev = self.globals.get(g)
                self.globals[g] = (cls or (prev[0] if prev else None), t, entry)
                if cls:
                    self.name(t, cls, short_name(cls),
                              f"constructs global {g:08x} from static initializer {entry:08x}; stores the {cls} vtable")
                    self.ctors[cls].add(t)
            elif cls:
                self.name(t, cls, "~" + short_name(cls),
                          f"atexit stub {stub:08x} destroys global {g:08x} with it; stores the {cls} vtable")
                self.dtors[cls].add(t)
                self.globals.setdefault(g, (cls, None, stub))
            return

    def first_use_statics(self):
        ctor_of = {a: ns for a, (ns, name, _) in self.names.items() if name == short_name(ns)}
        ctor_of.update(self.curated_ctors)
        for fn in self.bx.functions.values():
            if fn.kind != "code" or fn.library:
                continue
            ins = self.bx.insns(fn)
            for k, i in enumerate(ins):
                if i.id != x86.X86_INS_MOV or len(i.operands) != 2 or i.operands[0].type != x86.X86_OP_REG \
                        or i.operands[0].reg != x86.X86_REG_ECX or i.operands[1].type != x86.X86_OP_IMM:
                    continue
                g = i.operands[1].imm & 0xFFFFFFFF
                if self.bx.section_of(g) != ".data" or g in self.globals:
                    continue
                call = None
                for j in range(k + 1, min(k + 9, len(ins))):
                    n = ins[j]
                    if n.id == x86.X86_INS_CALL:
                        call = j
                        break
                    try:
                        if n.group(x86.X86_GRP_JUMP) or x86.X86_REG_ECX in n.regs_access()[1]:
                            break
                    except Exception:
                        break
                t = self.bx.call_target(ins[call]) if call is not None else None
                if t not in ctor_of or ctor_of[t].startswith("std"):
                    continue
                classes = [ctor_of[t]]
                for n in ins[call + 1:call + 8]:
                    if n.id == x86.X86_INS_CALL:
                        break
                    if n.id == x86.X86_INS_MOV and len(n.operands) == 2 and n.operands[0].type == x86.X86_OP_MEM \
                            and n.operands[0].mem.base == 0 and n.operands[0].mem.index == 0 \
                            and n.operands[0].mem.disp & 0xFFFFFFFF == g and n.operands[1].type == x86.X86_OP_IMM:
                        vt = self.vtables.get(n.operands[1].imm & 0xFFFFFFFF)
                        if vt and vt[1] == 0:
                            classes.append(vt[0])
                cls = self.most_derived(classes)
                if cls is None:
                    self.notes["(unrelated)"].append(f"global {g:08x} built by {t:08x} gets vtables of {sorted(set(classes))}")
                    continue
                self.globals[g] = (cls, t, fn.entry)
                self.local_statics.add(g)
            for k, i in enumerate(ins[1:], start=1):
                p = ins[k - 1]
                if self.bx.call_target(i) == ATEXIT and p.id == x86.X86_INS_PUSH and p.operands[0].type == x86.X86_OP_IMM:
                    stub = p.operands[0].imm & 0xFFFFFFFF
                    if self.bx.section_of(stub) == ".text":
                        self.global_from_stub(stub, stub, "dtor")

    # -- rule 3: constructors and destructors from vtable stores --------------------

    def from_vtable_stores(self):
        for fn in self.bx.functions.values():
            if fn.kind != "code":
                continue
            if fn.entry in self.names and self.names[fn.entry][1] in (SCALAR_DD, VECTOR_DD):
                continue
            stores, returns_this = self.this_analysis(self.bx.insns(fn))
            at0 = [(a, v) for a, off, v in stores if off == self.vtables[v][1] and off == 0]
            if not at0:
                continue
            classes = [self.vtables[v][0] for _, v in at0]
            cls = self.most_derived(classes)
            if cls is None:
                self.notes["(unrelated)"].append(f"{fn.entry:08x} stores vtables of unrelated classes {sorted(set(classes))}")
                continue
            own = [k for k, (_, v) in enumerate(at0) if self.vtables[v][0] == cls]
            short = short_name(cls)
            if fn.entry in self.names:
                # A stronger rule already named it. A derived destructor with an inlined
                # base destructor may store only the base vtable (the own store is dead).
                continue
            if own[-1] == len(at0) - 1 and returns_this:
                self.name(fn.entry, cls, short, f"stores the {cls} vtable into this last ({len(at0)} vtable stores) and returns this")
                self.ctors[cls].add(fn.entry)
            elif own[0] == 0 and len(at0) > 1:
                self.name(fn.entry, cls, "~" + short, f"stores the {cls} vtable into this first, then base vtables ({len(at0)} stores)")
                self.dtors[cls].add(fn.entry)

    # -- rule 4: sizes from operator new ---------------------------------------------

    def new_sites(self):
        self.new_targets = set()
        self.unknown_sized = []
        for new in (OPERATOR_NEW, OPERATOR_NEW_NOTHROW):
            for site, caller in self.callers.get(new, []):
                fn = self.bx.functions[caller]
                ins = self.bx.insns(fn)
                k = next(j for j, i in enumerate(ins) if i.address == site)
                size = None
                for p in reversed(ins[max(0, k - 4):k]):
                    if p.id == x86.X86_INS_PUSH:
                        if p.operands[0].type == x86.X86_OP_IMM:
                            size = p.operands[0].imm
                        break
                if size is None:
                    continue
                held = {x86.X86_REG_EAX}
                slots = set()
                called = None
                for i in ins[k + 1:k + 24]:
                    st = self.is_vtable_store(i)
                    if st and st[0] in held and st[1] == 0 and st[2] in self.vtables:
                        # an inlined constructor; when it follows a base constructor call,
                        # the stored (derived) vtable decides the class
                        cls = self.vtables[st[2]][0]
                        self.sizes[cls][size] += 1
                        self.size_sites[cls].append(f"{site:08x} inline")
                        called = None
                        break
                    if i.id == x86.X86_INS_MOV and len(i.operands) == 2:
                        d, s = i.operands
                        if d.type == x86.X86_OP_REG and s.type == x86.X86_OP_REG:
                            (held.add if s.reg in held else held.discard)(d.reg)
                            continue
                        if d.type == x86.X86_OP_MEM and s.type == x86.X86_OP_REG and s.reg in held:
                            slots.add((d.mem.base, d.mem.disp))
                            continue
                        if d.type == x86.X86_OP_REG and s.type == x86.X86_OP_MEM and (s.mem.base, s.mem.disp) in slots:
                            held.add(d.reg)
                            continue
                    if i.id == x86.X86_INS_CALL:
                        t = self.bx.call_target(i)
                        if called is None:
                            if x86.X86_REG_ECX not in held or not isinstance(t, int):
                                break
                            # A constructor returns this. Keep watching: an inlined derived
                            # constructor may construct members, then store its own vtable.
                            called = t
                            held -= {x86.X86_REG_EAX, x86.X86_REG_ECX, x86.X86_REG_EDX}
                            held.add(x86.X86_REG_EAX)
                        else:
                            held -= {x86.X86_REG_EAX, x86.X86_REG_ECX, x86.X86_REG_EDX}
                        continue
                    if i.id in (x86.X86_INS_RET, x86.X86_INS_JMP):
                        break
                    try:
                        _, written = i.regs_access()
                    except Exception:
                        written = []
                    for r in written:
                        held.discard(r)
                if called is not None:
                    self.new_targets.add(called)
                    self.pending_sizes.append((called, size, site))

    def resolve_sizes(self):
        ctor_class = {}
        for cls, fs in self.ctors.items():
            for f in fs:
                ctor_class[f] = cls
        for t, size, site in self.pending_sizes:
            cls = ctor_class.get(t)
            if cls is None:
                stores, _ = self.this_analysis(self.insns_of(t))
                classes = [self.vtables[v][0] for _, off, v in stores if off == 0]
                cls = self.most_derived(classes) if classes else None
                if cls is not None:
                    short = short_name(cls)
                    self.name(t, cls, short, f"called on the result of operator new({size}) at {site:08x} and stores the {cls} vtable")
                    self.ctors[cls].add(t)
            if cls is None:
                callee = self.bx.functions.get(t)
                if callee is not None and (callee.namespace == "std" or callee.namespace.startswith("std::")):
                    self.stl_sized.append((t, size, site))
                else:
                    self.unknown_sized.append((t, size, site))
                continue
            if self.curated.get(cls) is not None:
                continue
            self.sizes[cls][size] += 1
            self.size_sites[cls].append(f"{site:08x}")

    # -- output ------------------------------------------------------------------------

    def run(self):
        self.pending_sizes = []
        self.stl_sized = []
        self.deleting_destructors()
        self.initializers()
        self.new_sites()
        self.from_vtable_stores()
        self.first_use_statics()
        self.resolve_sizes()
        self.write()

    def chosen_size(self, cls):
        c = self.sizes.get(cls)
        if not c:
            return None, ""
        if len(c) > 1:
            self.notes[cls].append("conflicting new() sizes " + ", ".join(f"{s}x{n}" for s, n in sorted(c.items())))
            return None, "conflict"
        return next(iter(c)), ""

    def write(self):
        os.makedirs(LABELS, exist_ok=True)
        gen = "# generated by tools/recover_objects.py; edit the script, not this file\n"
        with open(os.path.join(LABELS, "objects.functions.tsv"), "w", encoding="utf-8") as f:
            f.write("address\tnamespace\tname\tcalling_convention\tevidence\n" + gen)
            for a, (ns, name, ev) in sorted(self.names.items()):
                f.write(f"{a:08x}\t{ns}\t{name}\t__thiscall\t{ev}\n")
        with open(os.path.join(LABELS, "initializers.functions.tsv"), "w", encoding="utf-8") as f:
            f.write("address\tnamespace\tname\tcalling_convention\tevidence\n" + gen)
            for entry, idx in self.initializer_rows:
                f.write(f"{entry:08x}\tGlobal\tstatic_init_{idx:03d}\t__cdecl\tentry {idx} of the C++ initializer table run by _cinit\n")
        per_class = Counter(cls for cls, _, _ in self.globals.values() if cls)
        self.unnamed_globals = 0
        with open(os.path.join(LABELS, "globals.data.tsv"), "w", encoding="utf-8") as f:
            f.write("address\tnamespace\tname\tevidence\ttype\n" + gen)
            for g, (cls, ctor, entry) in sorted(self.globals.items()):
                if g in self.curated_data:
                    continue
                ctor_fn = self.bx.functions.get(ctor) if ctor else None
                if not cls and ctor_fn is not None and ctor_fn.namespace.startswith("std::"):
                    continue
                if g in self.local_statics:
                    how = f"constructed on first use by {ctor:08x} in {entry:08x}"
                elif ctor:
                    how = f"constructed by {ctor:08x} from static initializer {entry:08x}"
                else:
                    how = f"destroyed by atexit stub {entry:08x}"
                if not cls:
                    self.unnamed_globals += 1
                    f.write(f"{g:08x}\tGlobal\tg_obj_{g:08x}\t{how}; the class has no RTTI\t\n")
                    continue
                short = cls.replace("::", "_")
                name = f"g_{short}" if per_class[cls] == 1 else f"g_{short}_{g:08x}"
                f.write(f"{g:08x}\tGlobal\t{name}\t{how}\tclass:{cls}\n")
        sized = 0
        with open(os.path.join(LABELS, "objects.classes.tsv"), "w", encoding="utf-8") as f:
            f.write("class\tsize\tevidence\n" + gen)
            for cls in sorted(self.sizes):
                size, _ = self.chosen_size(cls)
                if size is None:
                    continue
                need = max(off for (c, off) in self.vtables.values() if c == cls) + 4 if any(
                    c == cls for c, _ in self.vtables.values()) else 0
                if size < need:
                    self.notes[cls].append(f"new() size {size} is smaller than its last vfptr offset + 4 ({need})")
                    continue
                sized += 1
                f.write(f"{cls}\t{size}\toperator new({size}) at {', '.join(self.size_sites[cls][:4])}"
                        f"{' and more' if len(self.size_sites[cls]) > 4 else ''}\n")
        self.check_base_sizes()
        classes = sorted(set(c for c, _ in self.vtables.values()))
        with open(os.path.join(EXPORTS, "Ragexe.exe.objects.tsv"), "w", encoding="utf-8") as f:
            f.write("class\tsize\tnew_sites\tconstructors\tdestructors\tdeleting_destructors\tnotes\n")
            for cls in classes:
                size, flag = self.chosen_size(cls)
                hexes = lambda s: ",".join(f"{a:08x}" for a in sorted(s))
                f.write(f"{cls}\t{size if size is not None else flag}\t{sum(self.sizes[cls].values())}\t"
                        f"{hexes(self.ctors[cls])}\t{hexes(self.dtors[cls])}\t{hexes(self.deleting[cls])}\t"
                        f"{' | '.join(sorted(set(self.notes[cls])))}\n")
        with open(os.path.join(EXPORTS, "Ragexe.exe.unknown_new.tsv"), "w", encoding="utf-8") as f:
            f.write("callee\tsize\tsite\n")
            for t, size, site in sorted(set(self.unknown_sized)):
                f.write(f"{t:08x}\t{size}\t{site:08x}\n")
        n_ctor = sum(1 for v in self.names.values() if not v[1].startswith(("~", "`")))
        n_dtor = sum(1 for v in self.names.values() if v[1].startswith("~"))
        n_dd = sum(1 for v in self.names.values() if v[1].startswith("`"))
        print(f"classes with vtables: {len(classes)}")
        print(f"constructors {n_ctor}, destructors {n_dtor}, deleting destructors {n_dd}")
        print(f"classes sized: {sized}; new() sites resolved to a class: {sum(sum(c.values()) for c in self.sizes.values())}; "
              f"to STL helpers: {len(self.stl_sized)}; to an unknown callee: {len(self.unknown_sized)}")
        print(f"static initializers: {len(self.initializer_rows)}; global objects of known class: "
              f"{sum(1 for v in self.globals.values() if v[0])} of {len(self.globals)} "
              f"({len(self.local_statics)} constructed on first use); left as g_obj_: {self.unnamed_globals}")
        conflicts = [c for c in self.notes if any("conflict" in n or "smaller" in n or "named both" in n for n in self.notes[c])]
        print(f"classes with conflicts: {len(conflicts)} {conflicts[:12]}")

    def check_base_sizes(self):
        for cls, bases in self.bases.items():
            size, _ = self.chosen_size(cls) if cls in self.sizes else (None, "")
            if size is None:
                continue
            for b in bases:
                bsize, _ = self.chosen_size(b) if b in self.sizes else (None, "")
                if bsize is not None and bsize > size:
                    self.notes[cls].append(f"size {size} is smaller than base {b} size {bsize}")


if __name__ == "__main__":
    Recovery().run()
