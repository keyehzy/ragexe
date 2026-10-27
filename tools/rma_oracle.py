"""Compares the port's .rma loader with the original (docs/formats/rma.md).

usage: .venv/bin/python -I tools/rma_oracle.py [--jobs J]

The game data has no .rma, so this runs CRmaRes::Load (004220d0) in the emulation oracle on
synthetic files that exercise the tokenizer and every keyword, and compares the names,
the actions, the interior and shadow values and the number of messages shown with
`ragres decode-rmas`.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import oracle_pool  # noqa: E402
from emu import Emulator  # noqa: E402

CRMARES_CTOR, CRMARES_LOAD = 0x421C90, 0x4220D0  # analysis/labels
CRMARES_SIZE = 0x158


def fnv1a64(data, h=0xCBF29CE484222325):
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def synthetic():
    yield b""
    yield b"#rsxname door.rsx\n#action 0 1 2 3 4 5 6\n#action 2 9 8 7\t6\n"
    yield (b"#action 1 4 1 2 9\n"  # before any name: index -1
           b"#rsxname a.rsx\r\n#rsxname \"b c.rsx\"\r\n#action 3 1 2 3\r\n"
           b"#action 3 5 6 7 8 9 10 11\n#action 3 1 1 1\n")  # a duplicate keeps its list
    yield (b"// a comment\n   // indented\n#interiorzoom 1.5 // trailing\n#shadowzoom .25\n"
           b"#interioroffsetx -4\n#interioroffsety +12\n#interiorframes 3 7\n")
    yield b"#interiorzoom -2.25e1\n#shadowzoom 3d2\n#interioroffsetx 4294967297\n"
    yield b"#interiorzoom 1e\n#shadowzoom abc\n#interioroffsetx \"  15\"\n#interioroffsety -0\n"
    yield b"#unknown 1 2\n\n\n#rsxname x.rsx\n#bogus\n#action 4 1 2 3 9//cut\n"
    yield b"#action 0 1 2 3 4\n\"\" ignored line\n#action 1 ab\"7\" 8 9 10\n"
    yield "#rsxname 돌문.rsx\n#action 0 1 2 3 4\n".encode("cp949")
    yield b"#rsxname a.rsx\n#action 0 1 2 3\x00#action 1 1 2 3 4\n"  # text ends at the NUL
    yield b"#rsxname a.rsx\n#action 0 1 2 3 4"  # no final newline
    long_list = b"#action 5 1 2 3 " + b"9 " * 600  # longer than the 1023-byte buffer
    yield b"#rsxname a.rsx\n" + long_list + b"\n#action 6 1 2 3 4\n"
    exact = b"#action 7 1 2 3 "
    exact += b"1" * (1023 - len(exact))  # exactly fills the buffer
    yield exact + b"\n#action 8 1 2 3 4\n"
    yield b"#action 0 1 2 3 4 \"unterminated 5\n#action 1 1 2 3 4 \"\"\n"


def init():
    emu = Emulator(max_instructions=500_000_000).init_crt()
    obj = emu.scratch(size=CRMARES_SIZE)
    # The first read through an istream builds the C locale's ctype facet on the heap and
    # keeps it in a static. Load once before work() marks the heap, so that releasing a
    # file's allocations does not free it.
    emu.serve_data_files({"data\\t.rma": b"#shadowzoom 1\n"})
    emu.call(CRMARES_CTOR, this=obj)
    emu.call(CRMARES_LOAD, emu.cstr("t.rma"), this=obj)
    return emu, obj


def vector(emu, at, size):
    begin, end = emu.read_u32(at + 4), emu.read_u32(at + 8)
    return [begin + k * size for k in range((end - begin) // size)] if begin else []


def work(state, path):
    emu, obj = state
    with open(path, "rb") as f:
        data = f.read()
    mark = emu.heap_mark()
    emu.serve_data_files({"data\\t.rma": data})
    emu.api.messages.clear()
    emu.mem_write(obj, bytes(CRMARES_SIZE))
    emu.call(CRMARES_CTOR, this=obj)
    ok = emu.call(CRMARES_LOAD, emu.cstr("t.rma"), this=obj).eax & 0xFF
    if not ok:
        emu.heap_release(mark)
        return "failed"
    names = b""
    name_list = vector(emu, obj + 0x120, 16)
    for s in name_list:
        buffer, length = emu.read_u32(s + 4), emu.read_u32(s + 8)
        names += (emu.mem_read(buffer, length) if buffer and length else b"") + b"\0"
    rsx = [struct.unpack("<i", emu.mem_read(p, 4))[0] for p in vector(emu, obj + 0x130, 4)]
    values = []
    records = vector(emu, obj + 0x110, 0x1C)
    for k, r in enumerate(records):
        v0, v4, v8 = struct.unpack("<iii", emu.mem_read(r, 12))
        items = [struct.unpack("<i", emu.mem_read(p, 4))[0] for p in vector(emu, r + 0xC, 4)]
        values += [v0, v4, v8, len(items), *items, rsx[k]]
    zoom, shadow = struct.unpack("<II", emu.mem_read(obj + 0x140, 8))
    ints = struct.unpack("<iiii", emu.mem_read(obj + 0x148, 16))
    messages = len(emu.api.messages)
    emu.heap_release(mark)
    h = fnv1a64(struct.pack(f"<{len(values)}I", *(v & 0xFFFFFFFF for v in values)))
    return (f"ok {zoom:08x} {shadow:08x} {' '.join(str(v) for v in ints)} {messages} "
            f"{len(name_list)}:{fnv1a64(names):016x} {len(records)}:{h:016x}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ragres", default=os.path.join(ROOT, "build", "dev", "src", "ragres"))
    ap.add_argument("--jobs", type=int, default=0, help="worker processes (0: one per core but one)")
    args = ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        files = []
        for k, data in enumerate(synthetic()):
            path = os.path.join(tmp, f"synthetic{k}.rma")
            with open(path, "wb") as f:
                f.write(data)
            files.append(path)
        out = subprocess.run([args.ragres, "decode-rmas", *files], capture_output=True, text=True).stdout
        ours = {}
        for line in out.splitlines():
            n, rest = line.split(" ", 1)
            ours[int(n)] = "failed" if rest.startswith("failed") else rest
        originals = oracle_pool.run(files, init, work, args.jobs or None, label="rma")
        mismatches = [f"{os.path.basename(p)}: port {ours.get(n)!r}, original {want!r}"
                      for n, (p, want) in enumerate(zip(files, originals)) if ours.get(n) != want]
    print(f"{len(files)} synthetic .rma files compared, {len(mismatches)} differ")
    for line in mismatches[:20]:
        print("MISMATCH", line)
    print("FAILED" if mismatches else "OK")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
