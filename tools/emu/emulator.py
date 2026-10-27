"""Emulation oracle: run one function of Ragexe.exe in Unicorn and read back its result.

The image is mapped at its preferred base with each section's protection, on a flat
32-bit segment set-up with FS pointing at a minimal TEB (MSVC SEH frames use fs:[0]).
None of the original's start-up runs unless asked for: init_crt() runs _ioinit and then
what _cinit does (the floating-point set-up through the _FPinit pointer and the C
initializer table: atexit table, stdio, the ANSI code page, the unhandled-exception
filter), which stdio and printf-style formatting of floats need. The C++ initializer table, and with it every game global's
constructor, never runs. The parts of the CRT that need its own start-up are replaced:

  - The heap: malloc, __nh_malloc (operator new), calloc, realloc, _msize and free are
    hooked by address and served from a bump allocator. Blocks are zero-filled and never
    reused, so results do not depend on allocation history.
  - Every import goes to a stub. Stubs that exist are in winapi.py; calling any other
    import stops the run with EmulationError naming it, so a missing stub is never
    silent.

  - DirectDraw 7 and Direct3D 7 are fakes that record every call (directx.py), reached
    through DirectDrawCreateEx.

call() pushes the arguments, sets ECX for __thiscall, returns to a sentinel address and
gives back EAX, EDX and ST(0). Memory helpers place inputs in a scratch area and read
outputs back.

A stub or fake method that has to call back into the client (an enumeration) is a
generator: each `yield (function, [args])` runs that __stdcall callback in the emulator
and resumes the generator with the callback's EAX, and the generator's return value is
the stub's result.
"""
import os
import struct
import sys
import types

import pefile
from unicorn import (UC_ARCH_X86, UC_HOOK_BLOCK, UC_HOOK_CODE, UC_HOOK_MEM_INVALID, UC_MODE_32,
                     UC_PROT_ALL, UC_PROT_EXEC, UC_PROT_READ, UC_PROT_WRITE, Uc, UcError)
from unicorn.x86_const import (UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_ECX,
                               UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ES, UC_X86_REG_ESP, UC_X86_REG_FPSW,
                               UC_X86_REG_FP0, UC_X86_REG_FS, UC_X86_REG_GDTR, UC_X86_REG_GS, UC_X86_REG_SS)

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from binindex import EXE, ROOT  # noqa: E402

from . import directx, winapi  # noqa: E402

GDT = 0x7FFC0000
TEB = 0x7FFD0000
STUBS = 0x7FF00000            # one 16-byte slot per import, then the return sentinel
STACK_BASE, STACK_SIZE = 0x10000000, 0x00100000
HEAP_BASE, HEAP_SIZE = 0x20000000, 0x04000000
# The archive DataFile reads through (analysis/labels: globals, vtables, CGPak methods).
G_CGPAK, G_PCGPAK, CGPAK_VFTABLE = 0x67CD00, 0x67CD84, 0x5E8E50
CGPAK_LOAD_FILE, CGPAK_FREE_FILE, CGPAK_EXISTS = 0x4DD9A0, 0x4DD9F0, 0x4DDA10
SCRATCH_BASE, SCRATCH_SIZE = 0x30000000, 0x01000000

IOINIT = 0x5C9365                         # the start-up step before _cinit; builds __pioinfo
FPINIT_POINTER = 0x61FE78                 # _cinit calls through it when it is non-zero
C_INIT_TABLE = (0x605344, 0x605358)       # _cinit runs it with _initterm
# CRT allocation entry points (analysis/labels/crt.functions.tsv)
MALLOC, NH_MALLOC, CALLOC, REALLOC, MSIZE, FREE = 0x5C4EA5, 0x5C4EB7, 0x5CB14B, 0x5C4FDF, 0x5C726C, 0x5C37C2

SCN_EXECUTE, SCN_READ, SCN_WRITE = 0x20000000, 0x40000000, 0x80000000


class EmulationError(Exception):
    pass


class Result:
    def __init__(self, eax, edx, st0, blocks):
        self.eax, self.edx, self.st0, self.blocks = eax, edx, st0, blocks

    @property
    def signed(self):
        return self.eax - (1 << 32) if self.eax & 0x80000000 else self.eax

    def __repr__(self):
        return f"Result(eax=0x{self.eax:08x}, edx=0x{self.edx:08x}, st0={self.st0!r})"


def _page_up(n):
    return (n + 0xFFF) & ~0xFFF


def _gdt_entry(base, limit, access, flags):
    return ((limit & 0xFFFF) | ((base & 0xFFFFFF) << 16) | (access << 40) | (((limit >> 16) & 0xF) << 48)
            | (flags << 52) | (((base >> 24) & 0xFF) << 56))


class Emulator:
    def __init__(self, exe=EXE, files_root=os.path.join(ROOT, "original", "install"), max_instructions=50_000_000):
        if not os.path.exists(exe):
            raise FileNotFoundError(f"{exe} (the original executable is not in the repository; see docs/PLAN.md, M0)")
        self.pe = pefile.PE(exe)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.max_instructions = max_instructions
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self._map_image()
        self._map_segments()
        self.uc.mem_map(STACK_BASE, STACK_SIZE, UC_PROT_READ | UC_PROT_WRITE)
        self.uc.mem_map(HEAP_BASE, HEAP_SIZE, UC_PROT_READ | UC_PROT_WRITE)
        self.uc.mem_map(SCRATCH_BASE, SCRATCH_SIZE, UC_PROT_READ | UC_PROT_WRITE)
        self.heap_next, self.blocks, self.freed = HEAP_BASE, {}, []
        self.scratch_next = SCRATCH_BASE
        self.api = winapi.WinApi(self, files_root)
        self.calls = []                   # (import name, args) in order, for callers that want a trace
        self._frames = []                 # stubs waiting for a callback: [generator, return, esp, done]
        self._map_imports()
        self.directx = directx.DirectX(self)
        self._hook_runtime()
        self._fault = None
        self._blocks, self.block_count = [], 0
        self.uc.hook_add(UC_HOOK_MEM_INVALID, self._on_invalid)
        self.uc.hook_add(UC_HOOK_BLOCK, self._on_block)

    # -- set-up ---------------------------------------------------------------------------

    def _map_image(self):
        image = self.pe.get_memory_mapped_image()
        size = _page_up(self.pe.OPTIONAL_HEADER.SizeOfImage)
        self.uc.mem_map(self.base, size, UC_PROT_ALL)
        self.uc.mem_write(self.base, image[:size])
        self.uc.mem_protect(self.base, _page_up(self.pe.OPTIONAL_HEADER.SizeOfHeaders), UC_PROT_READ)
        for s in self.pe.sections:
            flags = s.Characteristics
            prot = (UC_PROT_READ if flags & SCN_READ else 0) | (UC_PROT_WRITE if flags & SCN_WRITE else 0) \
                | (UC_PROT_EXEC if flags & SCN_EXECUTE else 0)
            self.uc.mem_protect(self.base + s.VirtualAddress, _page_up(s.Misc_VirtualSize), prot)

    def _map_segments(self):
        uc = self.uc
        uc.mem_map(GDT, 0x1000, UC_PROT_READ)
        uc.mem_map(TEB, 0x1000, UC_PROT_READ | UC_PROT_WRITE)
        entries = {1: _gdt_entry(0, 0xFFFFF, 0x9A, 0xC),      # flat code
                   2: _gdt_entry(0, 0xFFFFF, 0x92, 0xC),      # flat data
                   3: _gdt_entry(TEB, 0xFFF, 0x92, 0x4)}      # FS: the TEB
        for k, v in entries.items():
            uc.mem_write(GDT + 8 * k, struct.pack("<Q", v))
        uc.reg_write(UC_X86_REG_GDTR, (0, GDT, 8 * len(entries) + 7, 0))
        uc.reg_write(UC_X86_REG_CS, 1 << 3)
        for r in (UC_X86_REG_SS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_GS):
            uc.reg_write(r, 2 << 3)
        uc.reg_write(UC_X86_REG_FS, 3 << 3)
        self.write_u32(TEB + 0x00, 0xFFFFFFFF)                # SEH chain: end of list
        self.write_u32(TEB + 0x04, STACK_BASE + STACK_SIZE)   # stack base
        self.write_u32(TEB + 0x08, STACK_BASE)                # stack limit
        self.write_u32(TEB + 0x18, TEB)                       # self
        self.write_u32(TEB + 0x20, winapi.PROCESS_ID)
        self.write_u32(TEB + 0x24, winapi.THREAD_ID)

    def _map_imports(self):
        self.stub_names = {}
        slots = []
        for d in self.pe.DIRECTORY_ENTRY_IMPORT:
            for imp in d.imports:
                name = imp.name.decode() if imp.name else f"#{imp.ordinal}"
                slots.append((imp.address, f"{d.dll.decode().upper()}!{name}"))
        self.sentinel = STUBS + 16 * len(slots)
        self.callback_return = self.sentinel + 16
        self.uc.mem_map(STUBS, _page_up(16 * len(slots) + 32), UC_PROT_READ | UC_PROT_EXEC)
        self.uc.mem_write(STUBS, b"\xc3" * _page_up(16 * len(slots) + 32))
        for k, (iat, name) in enumerate(slots):
            stub = STUBS + 16 * k
            self.stub_names[stub] = name
            self.uc.mem_protect(iat & ~0xFFF, 0x1000, UC_PROT_READ | UC_PROT_WRITE)
            self.write_u32(iat, stub)
            self.uc.mem_protect(iat & ~0xFFF, 0x1000, UC_PROT_READ)
        self.uc.hook_add(UC_HOOK_CODE, self._on_stub, begin=STUBS, end=self.sentinel - 1)
        self.uc.hook_add(UC_HOOK_CODE, self._on_callback_return, begin=self.callback_return,
                         end=self.callback_return)

    def _hook_runtime(self):
        """The CRT heap, by address; each handler is cdecl and returns EAX."""
        def malloc(size):
            return self.alloc(size)

        def calloc(count, size):
            return self.alloc(count * size)

        def realloc(block, size):
            new = self.alloc(size)
            if block:
                old = self.blocks.get(block, 0)
                self.mem_write(new, self.mem_read(block, min(old, size)))
                self.freed.append(block)
            return new

        hooks = {
            MALLOC: (1, malloc),
            NH_MALLOC: (2, lambda size, flag: self.alloc(size)),
            CALLOC: (2, calloc),
            REALLOC: (2, realloc),
            MSIZE: (1, lambda block: self.blocks.get(block, 0)),
            FREE: (1, lambda block: self.freed.append(block) or 0),
        }
        for addr, (nargs, fn) in hooks.items():
            self.uc.hook_add(UC_HOOK_CODE, self._runtime_hook(nargs, fn), begin=addr, end=addr)

    def _runtime_hook(self, nargs, fn):
        def hook(uc, address, size, _):
            esp = uc.reg_read(UC_X86_REG_ESP)
            ret = self.read_u32(esp)
            args = [self.read_u32(esp + 4 + 4 * k) for k in range(nargs)]
            uc.reg_write(UC_X86_REG_EAX, fn(*args) & 0xFFFFFFFF)
            uc.reg_write(UC_X86_REG_ESP, esp + 4)                # cdecl: the caller pops the arguments
            uc.reg_write(UC_X86_REG_EIP, ret)
        return hook

    # -- hooks ----------------------------------------------------------------------------

    def _on_stub(self, uc, address, size, _):
        name = self.stub_names.get(address)
        if name is None:
            return
        esp = uc.reg_read(UC_X86_REG_ESP)
        ret = self.read_u32(esp)
        stub = self.api.lookup(name)
        if stub is None:
            self._fault = f"call to the unstubbed import {name} from 0x{ret:08x}"
            uc.emu_stop()
            return
        args = [self.read_u32(esp + 4 + 4 * k) for k in range(stub.nargs)]
        self.calls.append((name, args))
        self.complete(stub.fn(*args), ret, esp + 4 + (4 * stub.nargs if stub.stdcall else 0))

    def complete(self, result, ret, esp, done=None):
        """Return from a stub to ret with the stack at esp. result is EAX, (EAX, EDX), or a
        generator that yields callbacks to run first (see the module docstring). done, if
        given, is called with the final EAX."""
        if isinstance(result, types.GeneratorType):
            self._frames.append([result, ret, esp, done])
            self._resume(None)
            return
        if isinstance(result, tuple):
            eax, edx = result
            self.uc.reg_write(UC_X86_REG_EDX, edx & 0xFFFFFFFF)
        else:
            eax = result
        if done:
            done(eax or 0)
        self.uc.reg_write(UC_X86_REG_EAX, (eax or 0) & 0xFFFFFFFF)
        self.uc.reg_write(UC_X86_REG_ESP, esp)
        self.uc.reg_write(UC_X86_REG_EIP, ret)

    def _resume(self, value):
        gen, ret, esp, done = self._frames[-1]
        try:
            fn, args = gen.send(value)
        except StopIteration as stop:
            self._frames.pop()
            self.complete(stop.value, ret, esp, done)
            return
        # the callback's frame goes below the stub's caller; __stdcall pops the arguments
        sp = esp - 0x40 - 4 * len(args)
        for k, a in enumerate(args):
            self.write_u32(sp + 4 * k, a)
        sp -= 4
        self.write_u32(sp, self.callback_return)
        self.uc.reg_write(UC_X86_REG_ESP, sp)
        self.uc.reg_write(UC_X86_REG_EIP, fn)

    def _on_callback_return(self, uc, address, size, _):
        if not self._frames:
            self.fault("return to the callback trampoline with no callback pending")
            return
        self._resume(uc.reg_read(UC_X86_REG_EAX))

    def replace_thiscall(self, address, nargs, fn):
        """Run fn(this, *args) in place of the __thiscall function at address: its result
        goes to EAX and the callee pops nargs arguments, as the function would."""
        def hook(uc, _address, _size, _):
            esp = uc.reg_read(UC_X86_REG_ESP)
            ret = self.read_u32(esp)
            this = uc.reg_read(UC_X86_REG_ECX)
            args = [self.read_u32(esp + 4 + 4 * k) for k in range(nargs)]
            self.complete(fn(this, *args), ret, esp + 4 + 4 * nargs)
        self.uc.hook_add(UC_HOOK_CODE, hook, begin=address, end=address)

    def serve_data_files(self, files):
        """Stand in for data.grf: DataFile reads go through g_pCGPak to CGPak::LoadFile,
        answered here from files, a dict of lower-cased archive paths
        ("data\\sprite\\x.spr") to bytes. Later calls replace the dict."""
        first = not hasattr(self, "data_files")
        self.data_files = {name.lower(): data for name, data in files.items()}
        if not first:
            return
        self.write_u32(G_CGPAK, CGPAK_VFTABLE)
        self.write_u32(G_PCGPAK, G_CGPAK)

        def load_file(this, key, size_out):
            data = self.data_files.get(self.read_cstr(key + 4, 256))
            if data is None:
                self.write_u32(size_out, 0)
                return 0
            buffer = self.alloc(len(data) + 8)
            self.mem_write(buffer, data)
            self.write_u32(size_out, len(data))
            return buffer

        self.replace_thiscall(CGPAK_LOAD_FILE, 2, load_file)
        self.replace_thiscall(CGPAK_FREE_FILE, 1, lambda this, buffer: 0)
        self.replace_thiscall(CGPAK_EXISTS, 1, lambda this, key: int(self.read_cstr(key + 4, 256) in self.data_files))

    def hook_code(self, begin, end, fn):
        self.uc.hook_add(UC_HOOK_CODE, fn, begin=begin, end=end)

    def fault(self, message):
        """Stop the run; call() raises EmulationError with message."""
        self._fault = message
        self.uc.emu_stop()

    def esp(self):
        return self.uc.reg_read(UC_X86_REG_ESP)

    def _on_invalid(self, uc, access, address, size, value, _):
        self._fault = f"invalid memory access (type {access}) at 0x{address:08x}, size {size}, " \
                      f"eip 0x{uc.reg_read(UC_X86_REG_EIP):08x}"
        return False

    def _on_block(self, uc, address, size, _):
        self.block_count += 1
        self._blocks.append(address)
        if len(self._blocks) > 64:
            del self._blocks[:32]

    # -- calling --------------------------------------------------------------------------

    def init_crt(self):
        """Run _ioinit, then the C part of _cinit: the _FPinit pointer and the C initializer table."""
        self.call(IOINIT)
        fpinit = self.read_u32(FPINIT_POINTER)
        if fpinit:
            self.call(fpinit)
        for slot in range(*C_INIT_TABLE, 4):
            fn = self.read_u32(slot)
            if fn:
                self.call(fn)
        return self

    def call(self, addr, *args, this=None):
        """Call the function at addr. args are 32-bit values pushed right to left; this,
        when given, goes in ECX (__thiscall). Returns a Result."""
        uc = self.uc
        esp = STACK_BASE + STACK_SIZE - 0x100
        for a in reversed(args):
            esp -= 4
            self.write_u32(esp, a & 0xFFFFFFFF)
        esp -= 4
        self.write_u32(esp, self.sentinel)
        uc.reg_write(UC_X86_REG_ESP, esp)
        uc.reg_write(UC_X86_REG_EBP, 0)
        uc.reg_write(UC_X86_REG_ECX, (this or 0) & 0xFFFFFFFF)
        self._fault, self._blocks, self.block_count, self._frames = None, [], 0, []
        try:
            uc.emu_start(addr, self.sentinel, count=self.max_instructions)
        except UcError as e:
            raise EmulationError(f"{self._fault or e} while running 0x{addr:08x}; recent blocks "
                                 f"{', '.join(f'0x{b:08x}' for b in self._blocks[-8:])}") from None
        if self._fault:
            raise EmulationError(f"{self._fault} while running 0x{addr:08x}")
        pc = uc.reg_read(UC_X86_REG_EIP)
        if pc != self.sentinel:
            raise EmulationError(f"0x{addr:08x} did not return within {self.max_instructions} instructions "
                                 f"(stopped at 0x{pc:08x})")
        return Result(uc.reg_read(UC_X86_REG_EAX), uc.reg_read(UC_X86_REG_EDX), self.st0(), self.block_count)

    def st0(self):
        """ST(0) as a Python float: the x87 register at the TOP field of the status word."""
        top = (self.uc.reg_read(UC_X86_REG_FPSW) >> 11) & 7
        mantissa, exponent = self.uc.reg_read(UC_X86_REG_FP0 + top)
        if mantissa == 0 and exponent & 0x7FFF == 0:
            return 0.0
        sign = -1.0 if exponent & 0x8000 else 1.0
        return sign * mantissa * 2.0 ** ((exponent & 0x7FFF) - 16383 - 63)

    # -- memory ---------------------------------------------------------------------------

    def alloc(self, size, align=16):
        """A zero-filled block from the emulated heap."""
        addr = (self.heap_next + align - 1) & ~(align - 1)
        if addr + max(size, 1) > HEAP_BASE + HEAP_SIZE:
            raise EmulationError("emulated heap exhausted")
        self.heap_next = addr + max(size, 1)
        self.blocks[addr] = size
        return addr

    def heap_mark(self):
        """The heap and scratch positions, for heap_release."""
        return self.heap_next, self.scratch_next

    def heap_release(self, mark):
        """Give back, zeroed, every heap block and scratch area placed since mark: for
        batch runs that call a function many times and keep nothing between calls."""
        heap, scratch = mark
        for base, top in ((heap, self.heap_next), (scratch, self.scratch_next)):
            if top > base:
                self.mem_write(base, bytes(top - base))
        self.heap_next, self.scratch_next = heap, scratch
        self.blocks = {a: n for a, n in self.blocks.items() if a < heap}
        self.freed = [a for a in self.freed if a < heap]

    def scratch(self, data=b"", size=None, align=16):
        """Place bytes in the scratch area (inputs and output buffers); returns the address."""
        n = max(len(data), size or 0, 1)
        addr = (self.scratch_next + align - 1) & ~(align - 1)
        if addr + n > SCRATCH_BASE + SCRATCH_SIZE:
            raise EmulationError("scratch area exhausted")
        self.scratch_next = addr + n
        if data:
            self.mem_write(addr, data)
        return addr

    def cstr(self, s, encoding="cp1252"):
        """A NUL-terminated string in the scratch area."""
        return self.scratch((s.encode(encoding) if isinstance(s, str) else s) + b"\0")

    def mem_read(self, addr, n):
        return bytes(self.uc.mem_read(addr, n))

    def mem_write(self, addr, data):
        self.uc.mem_write(addr, bytes(data))

    def read_u32(self, addr):
        return struct.unpack("<I", self.mem_read(addr, 4))[0]

    def write_u32(self, addr, v):
        self.mem_write(addr, struct.pack("<I", v & 0xFFFFFFFF))

    def read_cstr(self, addr, limit=4096, encoding="cp1252"):
        data = self.mem_read(addr, limit)
        end = data.find(b"\0")
        return (data if end < 0 else data[:end]).decode(encoding)
