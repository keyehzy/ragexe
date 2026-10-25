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

    def resolve_thunk(self, target):
        """Follow import thunks (jmp [IAT]) to the imported name."""
        fn = self.functions.get(target) if isinstance(target, int) else None
        if fn is not None and fn.kind == "thunk":
            for insn in self.insns(fn):
                t = self.jump_target(insn)
                if t is not None:
                    return t
        return target
