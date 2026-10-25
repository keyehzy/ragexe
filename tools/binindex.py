"""In-memory index of Ragexe.exe for analysis scripts.

Function boundaries come from the ledger (which mirrors Ghidra), instructions from a
linear capstone sweep of each function's [entry, entry+size) range. That matches
Ghidra's bodies for this binary's contiguous MSVC 6 functions; a function whose body
is not contiguous gets a truncated or over-long sweep, so callers should treat the
index as evidence, not truth.
"""
import os
import struct
from collections import defaultdict

import capstone
import pefile
from capstone import x86

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "original", "client", "Ragexe.exe")
LEDGER = os.path.join(ROOT, "analysis", "ledger.tsv")
# The stack probe in large-frame prologues saves and restores ECX (push ecx ... mov ecx, [eax]).
CHKSTK = 0x5C22B0
EH_PROLOG = 0x5C441C              # __EH_prolog
EH_MAGIC = 0x19930520            # MSVC 6 C++ FuncInfo


class Function:
    __slots__ = ("entry", "size", "kind", "namespace", "name", "library", "_insns")

    def __init__(self, row):
        self.entry = int(row["entry"], 16)
        self.size = int(row["size"])
        self.kind = row["kind"]
        self.namespace = row["namespace"]
        self.name = row["name"]
        self.library = row["library"] == "1"
        self._insns = None

    @property
    def qualified(self):
        return self.name if self.namespace == "Global" else f"{self.namespace}::{self.name}"

    def __repr__(self):
        return f"<{self.entry:08x} {self.qualified}>"


class BinIndex:
    def __init__(self, exe=EXE, ledger=LEDGER):
        self.pe = pefile.PE(exe, fast_load=False)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.image = self.pe.get_memory_mapped_image()
        self.sections = {}
        for s in self.pe.sections:
            name = s.Name.rstrip(b"\0").decode()
            self.sections[name] = (self.base + s.VirtualAddress, self.base + s.VirtualAddress + s.Misc_VirtualSize)
        self.iat = {}
        for d in self.pe.DIRECTORY_ENTRY_IMPORT:
            for imp in d.imports:
                name = imp.name.decode() if imp.name else f"#{imp.ordinal}"
                self.iat[imp.address] = f"{d.dll.decode().upper()}!{name}"
        with open(ledger, encoding="utf-8") as f:
            lines = f.read().splitlines()
        header = lines[0].split("\t")
        self.functions = {}
        for line in lines[1:]:
            fn = Function(dict(zip(header, line.split("\t"))))
            self.functions[fn.entry] = fn
        self.by_name = defaultdict(list)
        for fn in self.functions.values():
            self.by_name[fn.qualified].append(fn)
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.md.detail = True
        self._calls = None
        self._eh_parents = None

    # -- memory ---------------------------------------------------------------------

    def section_of(self, va):
        for name, (lo, hi) in self.sections.items():
            if lo <= va < hi:
                return name
        return None

    def u32(self, va):
        return struct.unpack_from("<I", self.image, va - self.base)[0]

    def i32(self, va):
        return struct.unpack_from("<i", self.image, va - self.base)[0]

    def cstring(self, va, limit=512):
        off = va - self.base
        end = self.image.find(b"\0", off, off + limit)
        if end < 0:
            return None
        raw = self.image[off:end]
        return raw.decode("cp1252", errors="replace")

    # -- code -----------------------------------------------------------------------

    def insns(self, fn):
        if fn._insns is None:
            off = fn.entry - self.base
            fn._insns = list(self.md.disasm(self.image[off:off + fn.size], fn.entry))
        return fn._insns

    def call_target(self, insn):
        """Direct call target, or 'DLL!name' for a call through the import table."""
        if insn.id != x86.X86_INS_CALL or not insn.operands:
            return None
        op = insn.operands[0]
        if op.type == x86.X86_OP_IMM:
            return op.imm
        if op.type == x86.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
            return self.iat.get(op.mem.disp)
        return None

    def jump_target(self, insn):
        if insn.id != x86.X86_INS_JMP or not insn.operands:
            return None
        op = insn.operands[0]
        if op.type == x86.X86_OP_IMM:
            return op.imm
        if op.type == x86.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
            return self.iat.get(op.mem.disp)
        return None

    def data_refs(self, insn):
        """Absolute addresses inside the image named by an instruction's operands."""
        out = []
        for op in insn.operands:
            if op.type == x86.X86_OP_IMM:
                v = op.imm & 0xffffffff
            elif op.type == x86.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
                v = op.mem.disp & 0xffffffff
            else:
                continue
            if self.section_of(v):
                out.append(v)
        return out

    def build_calls(self):
        """callees[f] = list of (insn address, target); callers[target] = list of (insn address, f)."""
        if self._calls is None:
            callees, callers = defaultdict(list), defaultdict(list)
            for fn in self.functions.values():
                if fn.kind == "funclet":
                    continue
                for insn in self.insns(fn):
                    t = self.call_target(insn)
                    if t is None and fn.kind == "thunk":
                        t = self.jump_target(insn)
                    if t is not None:
                        callees[fn.entry].append((insn.address, t))
                        callers[t].append((insn.address, fn.entry))
            self._calls = (callees, callers)
        return self._calls

    def eh_parents(self):
        """funclet entry -> the function whose C++ EH FuncInfo lists it: the handler is
        found from the `mov eax, handler; call __EH_prolog` or `push -1; push handler`
        prologue, and the handler's `mov eax, FuncInfo` (magic 0x19930520) lists the
        unwind actions and catch blocks."""
        if self._eh_parents is not None:
            return self._eh_parents
        parents = {}
        funclets = {fn.entry for fn in self.functions.values() if fn.kind == "funclet"}
        for fn in self.functions.values():
            if fn.kind != "code":
                continue
            ins = self.insns(fn)
            for k, i in enumerate(ins):
                handler = None
                if i.id == x86.X86_INS_MOV and k + 1 < len(ins) and self.call_target(ins[k + 1]) == EH_PROLOG \
                        and i.operands[1].type == x86.X86_OP_IMM:
                    handler = i.operands[1].imm & 0xFFFFFFFF
                elif i.id == x86.X86_INS_PUSH and k > 0 and i.operands[0].type == x86.X86_OP_IMM \
                        and ins[k - 1].id == x86.X86_INS_PUSH and ins[k - 1].operands[0].type == x86.X86_OP_IMM \
                        and ins[k - 1].operands[0].imm & 0xFFFFFFFF == 0xFFFFFFFF:
                    handler = i.operands[0].imm & 0xFFFFFFFF
                if handler is None or self.section_of(handler) != ".text":
                    continue
                for f in self.funclets_of(handler):
                    if f in funclets:
                        parents.setdefault(f, fn.entry)
        self._eh_parents = parents
        return parents

    def funclets_of(self, handler):
        off = handler - self.base
        if self.image[off] != 0xB8:              # mov eax, FuncInfo; jmp __CxxFrameHandler
            return []
        info = self.u32(handler + 1)
        if self.section_of(info) is None or self.u32(info) != EH_MAGIC:
            return []
        out = []
        states, unwind, ntry, tries = (self.u32(info + 4), self.u32(info + 8),
                                       self.u32(info + 12), self.u32(info + 16))
        for s in range(min(states, 4096)):
            action = self.u32(unwind + 8 * s + 4)
            if action:
                out.append(action)
        for t in range(min(ntry, 256)):
            ncatch, catches = self.u32(tries + 20 * t + 12), self.u32(tries + 20 * t + 16)
            for c in range(min(ncatch, 64)):
                out.append(self.u32(catches + 16 * c + 12))
        return out

    def reads_ecx_first(self, fn):
        """True when the function reads ECX before writing it, the mark of __thiscall.
        `push ecx` only reserves a stack slot in MSVC 6 prologues, and the stack probe
        _chkstk keeps ECX."""
        for i in self.insns(fn):
            ops = i.operands
            if i.id == x86.X86_INS_PUSH and ops and ops[0].type == x86.X86_OP_REG and ops[0].reg == x86.X86_REG_ECX:
                continue
            if i.id in (x86.X86_INS_XOR, x86.X86_INS_SUB) and len(ops) == 2 and ops[0].type == x86.X86_OP_REG \
                    and ops[1].type == x86.X86_OP_REG and ops[0].reg == ops[1].reg:
                if ops[0].reg == x86.X86_REG_ECX:
                    return False
                continue
            if i.id == x86.X86_INS_CALL and self.call_target(i) == CHKSTK:
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

    def resolve_thunk(self, target):
        """Follow import thunks (jmp [IAT]) to the imported name."""
        fn = self.functions.get(target) if isinstance(target, int) else None
        if fn is not None and fn.kind == "thunk":
            for insn in self.insns(fn):
                t = self.jump_target(insn)
                if t is not None:
                    return t
        return target
