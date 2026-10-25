"""Call one function of Ragexe.exe in the emulator and print its result.

usage: python -m emu [--crt] [--this VALUE] ADDRESS [ARG ...]   (run from tools/)

Each ARG is an integer (decimal or 0x hex), s:TEXT for a pointer to a NUL-terminated
string, or buf:N for a pointer to N zero bytes. Buffers and strings are printed after
the call, so outputs written through a pointer can be read. --crt runs init_crt() first.
Calls made to the DirectDraw/Direct3D fakes are listed after the result.
"""
import argparse

from .emulator import Emulator


def parse(emu, arg):
    if arg.startswith("s:"):
        return emu.cstr(arg[2:]), ("string", arg[2:])
    if arg.startswith("buf:"):
        return emu.scratch(size=int(arg[4:], 0)), ("buffer", int(arg[4:], 0))
    return int(arg, 0), None


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--crt", action="store_true")
    p.add_argument("--this", type=lambda v: int(v, 0))
    p.add_argument("address", type=lambda v: int(v, 16))
    p.add_argument("args", nargs="*")
    a = p.parse_args()
    emu = Emulator()
    if a.crt:
        emu.init_crt()
        emu.calls.clear()
    values, shown = [], []
    for arg in a.args:
        v, kind = parse(emu, arg)
        values.append(v)
        shown.append((v, kind))
    r = emu.call(a.address, *values, this=a.this)
    print(f"eax 0x{r.eax:08x} ({r.signed})  edx 0x{r.edx:08x}  st0 {r.st0!r}  blocks {r.blocks}")
    for k, (v, kind) in enumerate(shown):
        if kind:
            print(f"arg {k} ({kind[0]} at 0x{v:08x}): {emu.read_cstr(v)!r}")
    imports = [n for n, _ in emu.calls]
    if imports:
        print("imports called:", ", ".join(sorted(set(imports))))
    for c in emu.directx.calls:
        print(f"  {c}  {c.detail}" if c.detail else f"  {c}")


if __name__ == "__main__":
    main()
