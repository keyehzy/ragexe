"""Static check that Ragexe.exe does not create or modify code at run time.

The original is never run, so a memory dump at the entry point cannot be compared with
the file. This script looks instead for every way the code could change:

  1. Section flags: which sections are both writable and executable.
  2. Every call to an import that can make memory executable or bring in new code
     (VirtualProtect, VirtualAlloc, MapViewOfFile, LoadLibraryA, GetProcAddress, ...),
     with the constant arguments pushed before it. Calls through a register loaded from
     the import table (mov esi, [IAT]; call esi) are followed.
  3. Every instruction that writes to an absolute address inside .text.
  4. Every immediate that points into .text but is not a function entry: a pointer into
     the middle of code is how a patch target would be named.

Writes through a computed pointer cannot be ruled out statically; items 2 and 4 cover
how such a pointer into code would have to be obtained or made writable.

usage: runtime_code_check.py [--out FILE]
"""
import argparse
import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from binindex import BinIndex, ROOT  # noqa: E402
from capstone import x86  # noqa: E402
import capstone  # noqa: E402

DEFAULT_OUT = os.path.join(ROOT, "analysis", "exports", "Ragexe.exe.runtime_code.txt")

# import name -> names of its stack arguments, first to last
WATCHED = {
    "KERNEL32.DLL!VirtualProtect": ("lpAddress", "dwSize", "flNewProtect", "lpflOldProtect"),
    "KERNEL32.DLL!VirtualAlloc": ("lpAddress", "dwSize", "flAllocationType", "flProtect"),
    "KERNEL32.DLL!VirtualFree": ("lpAddress", "dwSize", "dwFreeType"),
    "KERNEL32.DLL!CreateFileMappingA": ("hFile", "lpAttributes", "flProtect", "dwMaximumSizeHigh",
                                        "dwMaximumSizeLow", "lpName"),
    "KERNEL32.DLL!MapViewOfFile": ("hFileMappingObject", "dwDesiredAccess", "dwFileOffsetHigh",
                                   "dwFileOffsetLow", "dwNumberOfBytesToMap"),
    "KERNEL32.DLL!LoadLibraryA": ("lpLibFileName",),
    "KERNEL32.DLL!GetProcAddress": ("hModule", "lpProcName"),
    "KERNEL32.DLL!HeapCreate": ("flOptions", "dwInitialSize", "dwMaximumSize"),
    "KERNEL32.DLL!WriteProcessMemory": ("hProcess", "lpBaseAddress", "lpBuffer", "nSize", "lpNumberOfBytesWritten"),
    "KERNEL32.DLL!FlushInstructionCache": ("hProcess", "lpBaseAddress", "dwSize"),
}
# protection constants that allow execution
EXECUTABLE = {0x10: "PAGE_EXECUTE", 0x20: "PAGE_EXECUTE_READ", 0x40: "PAGE_EXECUTE_READWRITE",
              0x80: "PAGE_EXECUTE_WRITECOPY"}
PROTECT_ARGS = {"flNewProtect", "flProtect"}
SCN_MEM_EXECUTE, SCN_MEM_WRITE = 0x20000000, 0x80000000
EH_PROLOG = 0x5C441C
# instructions whose immediate is an operand of arithmetic, not an address
NOT_POINTERS = {x86.X86_INS_CALL, x86.X86_INS_AND, x86.X86_INS_OR, x86.X86_INS_XOR, x86.X86_INS_ADD, x86.X86_INS_SUB,
                x86.X86_INS_CMP, x86.X86_INS_TEST, x86.X86_INS_IMUL, x86.X86_INS_ADC, x86.X86_INS_SBB}


class Check:
    def __init__(self):
        self.bx = BinIndex()
        self.text = self.bx.sections[".text"]
        self.out = []
        self.findings = 0

    def say(self, line=""):
        self.out.append(line)

    def containing(self, addr):
        for fn in self.bx.functions.values():
            if fn.kind != "funclet" and fn.entry <= addr < fn.entry + fn.size:
                return fn
        return None

    def describe(self, v):
        s = self.bx.cstring(v, 80) if self.bx.section_of(v) in (".rdata", ".data") else None
        if s and len(s) >= 2 and all(32 <= ord(c) < 127 for c in s):
            return f"0x{v:x} {s!r}"
        return f"0x{v:x}"

    # -- 1 ----------------------------------------------------------------------------

    def sections(self):
        self.say("== Sections")
        for s in self.bx.pe.sections:
            name = s.Name.rstrip(b"\0").decode()
            flags = s.Characteristics
            wx = flags & SCN_MEM_EXECUTE and flags & SCN_MEM_WRITE
            self.say(f"{name:8} 0x{flags:08x} {'execute' if flags & SCN_MEM_EXECUTE else '-------'} "
                     f"{'write' if flags & SCN_MEM_WRITE else '-----'}{'  WRITABLE AND EXECUTABLE' if wx else ''}")
            if wx:
                self.findings += 1
        self.say()

    # -- 2 ----------------------------------------------------------------------------

    def call_sites(self):
        """(function, call instruction index, import name) for every call of a watched import."""
        slots = {a: n for a, n in self.bx.iat.items() if n in WATCHED}
        thunks = {fn.entry: self.bx.resolve_thunk(fn.entry) for fn in self.bx.functions.values() if fn.kind == "thunk"}
        out = []
        for fn in self.bx.functions.values():
            if fn.kind != "code":
                continue
            ins = self.bx.insns(fn)
            held = {}                  # reg -> import name loaded from the IAT
            for k, i in enumerate(ins):
                if i.id == x86.X86_INS_CALL:
                    t = self.bx.call_target(i)
                    if isinstance(t, int):
                        t = thunks.get(t, t)
                    op = i.operands[0]
                    if op.type == x86.X86_OP_REG and op.reg in held:
                        t = held[op.reg]
                    if t in WATCHED:
                        out.append((fn, k, t))
                    for r in (x86.X86_REG_EAX, x86.X86_REG_ECX, x86.X86_REG_EDX):
                        held.pop(r, None)
                    continue
                if i.id == x86.X86_INS_MOV and len(i.operands) == 2 and i.operands[0].type == x86.X86_OP_REG \
                        and i.operands[1].type == x86.X86_OP_MEM and i.operands[1].mem.base == 0 \
                        and i.operands[1].mem.disp in slots:
                    held[i.operands[0].reg] = slots[i.operands[1].mem.disp]
                    continue
                try:
                    for r in i.regs_access()[1]:
                        held.pop(r, None)
                except capstone.CsError:
                    pass
        return out

    def pushed_args(self, ins, k, n):
        """The operands of the n pushes before instruction k, first argument first."""
        args = []
        for p in reversed(ins[max(0, k - 24):k]):
            if p.id == x86.X86_INS_CALL:
                break
            if p.id == x86.X86_INS_PUSH:
                op = p.operands[0]
                args.append(self.describe(op.imm & 0xFFFFFFFF) if op.type == x86.X86_OP_IMM else p.op_str)
                if len(args) == n:
                    break
        return args + ["?"] * (n - len(args))

    def imports(self):
        self.say("== Calls to imports that can make memory executable or load code")
        sites = self.call_sites()
        by_import = defaultdict(list)
        for fn, k, name in sites:
            by_import[name].append((fn, k))
        for name in WATCHED:
            calls = by_import.get(name, [])
            imported = name in self.bx.iat.values()
            self.say(f"{name}: {'not imported' if not imported else f'{len(calls)} call sites'}")
            for fn, k in sorted(calls, key=lambda c: c[0].entry):
                ins = self.bx.insns(fn)
                args = self.pushed_args(ins, k, len(WATCHED[name]))
                flag = ""
                if name == "KERNEL32.DLL!VirtualProtect":
                    flag = "  CHANGES PROTECTION: review where lpAddress points"
                    self.findings += 1
                for an, av in zip(WATCHED[name], args):
                    if an in PROTECT_ARGS and av.startswith("0x") and " " not in av:
                        prot = int(av, 16)
                        if prot in EXECUTABLE:
                            flag = f"  EXECUTABLE ({EXECUTABLE[prot]})"
                            self.findings += 1
                desc = ", ".join(f"{an}={av}" for an, av in zip(WATCHED[name], args))
                self.say(f"  {ins[k].address:08x} in {fn.qualified} ({fn.entry:08x}): {desc}{flag}")
        self.say()

    # -- 3 and 4 ------------------------------------------------------------------------

    def text_refs(self):
        lo, hi = self.text
        entries = set(self.bx.functions)
        writes, inner = [], defaultdict(list)
        for fn in self.bx.functions.values():
            if fn.kind == "funclet":
                continue
            ins = self.bx.insns(fn)
            for k, i in enumerate(ins):
                for n, op in enumerate(i.operands):
                    if op.type == x86.X86_OP_MEM and op.mem.base == 0 and lo <= (op.mem.disp & 0xFFFFFFFF) < hi \
                            and op.access & capstone.CS_AC_WRITE:
                        writes.append((i, fn))
                    if op.type != x86.X86_OP_IMM:
                        continue
                    v = op.imm & 0xFFFFFFFF
                    if not lo <= v < hi or v in entries or i.group(capstone.CS_GRP_JUMP) or i.id in NOT_POINTERS:
                        continue
                    if i.id == x86.X86_INS_PUSH and (fn.entry <= v < fn.entry + fn.size or (
                            k > 0 and ins[k - 1].id == x86.X86_INS_PUSH and ins[k - 1].operands[0].type == x86.X86_OP_IMM
                            and ins[k - 1].operands[0].imm & 0xFFFFFFFF == 0xFFFFFFFF)):
                        continue  # an SEH scope table or C++ EH handler in the frame set-up
                    if i.id == x86.X86_INS_MOV and k + 1 < len(ins) and self.bx.call_target(ins[k + 1]) == EH_PROLOG:
                        continue  # mov eax, <C++ EH handler>; call _EH_prolog
                    inner[v].append((i, fn))
        self.say("== Instructions that write to an absolute address in .text")
        for i, fn in writes:
            self.say(f"  {i.address:08x} in {fn.qualified}: {i.mnemonic} {i.op_str}")
            self.findings += 1
        if not writes:
            self.say("  none")
        self.say()
        self.say("== Immediates pointing into .text that are not a function entry")
        self.say("   (left out: SEH scope tables and C++ EH handlers in a frame set-up or passed to")
        self.say("   _EH_prolog, and immediates of arithmetic and comparison instructions)")
        for v in sorted(inner):
            target = self.containing(v)
            where = f"inside {target.qualified} ({target.entry:08x})" if target else "outside every function"
            uses = ", ".join(f"{i.address:08x} {i.mnemonic} in {fn.qualified}" for i, fn in inner[v][:3])
            self.say(f"  0x{v:08x} {where}: {uses}{' ...' if len(inner[v]) > 3 else ''}")
        if not inner:
            self.say("  none")
        self.inner = inner
        self.say()

    def run(self, out):
        self.say("Ragexe.exe static runtime-code check (tools/runtime_code_check.py)")
        self.say()
        self.sections()
        self.imports()
        self.text_refs()
        self.say(f"findings: {self.findings} (writable and executable sections, VirtualProtect calls, executable "
                 f"allocations, absolute writes into .text); immediates into code to review: {len(self.inner)}")
        with open(out, "w", encoding="utf-8") as f:
            f.write("\n".join(self.out) + "\n")
        print("\n".join(self.out[-1:]))
        print(f"report: {out}")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--out", default=DEFAULT_OUT)
    Check().run(p.parse_args().out)
