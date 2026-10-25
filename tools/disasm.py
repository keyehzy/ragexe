"""Disassemble functions with call targets, imports and string operands named.

usage: .venv/bin/python tools/disasm.py ADDRESS [ADDRESS ...]   (hex)
An address that is not a function entry is disassembled for 96 bytes.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from binindex import BinIndex  # noqa: E402


def main():
    bx = BinIndex()

    def name(t):
        if isinstance(t, str):
            return t
        fn = bx.functions.get(t)
        return fn.qualified if fn else f"?{t:08x}"

    for arg in sys.argv[1:]:
        a = int(arg, 16)
        fn = bx.functions.get(a)
        n = fn.size if fn else 96
        print(f"== {a:08x} {fn.qualified if fn else '(not a function entry)'} size={n}")
        off = a - bx.base
        for i in bx.md.disasm(bx.image[off:off + n], a):
            extra = ""
            t = bx.call_target(i) or bx.jump_target(i)
            if t is not None and i.mnemonic in ("call", "jmp"):
                extra = "  ; " + name(bx.resolve_thunk(t) if isinstance(t, int) else t)
            else:
                for v in bx.data_refs(i):
                    s = bx.cstring(v, 60) if bx.section_of(v) in (".rdata", ".data") else None
                    if s and len(s) >= 3 and all(32 <= ord(c) < 127 or c in "\r\n\t" for c in s):
                        extra = f"  ; {s!r}"
                    elif v in bx.functions:
                        extra = "  ; &" + name(v)
            print(f"  {i.address:08x}  {i.mnemonic:6} {i.op_str}{extra}")


if __name__ == "__main__":
    main()
