"""Compares the port's name-pair tables with the original (docs/formats/client_tables.md).

usage: .venv/bin/python -I tools/name_pairs_oracle.py [--jobs J]

ClientState loads mapNameTable.txt (005979d0), mapObjLightTable.txt (00597e10),
mp3NameTable.txt (00598290) and skillNameTable.txt (00593c10) with the same code into
vectors of string pairs, sorted. This runs each loader in the emulation oracle on its
table from data.grf and from each .gpf patch, and runs the mapNameTable loader on
synthetic tables, comparing every pair in order with `ragres decode-name-pairs`.
"""
import argparse
import os
import random
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import oracle_pool  # noqa: E402
from emu import Emulator  # noqa: E402

# Each loader, the table it opens, and where in ClientState it keeps the pairs.
LOADERS = {
    "mapNameTable.txt": (0x5979D0, 0x1670),
    "mapObjLightTable.txt": (0x597E10, 0x1718),
    "mp3NameTable.txt": (0x598290, 0x1680),
    "skillNameTable.txt": (0x593C10, 0x400),
}
CLIENT_STATE_SIZE = 0x1800


def fnv1a64(data, h=0xCBF29CE484222325):
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def synthetic():
    yield b"prontera.rsw#Prontera#\r\ngeffen.rsw#Geffen#\r\n"
    yield b"b#2#\r\na#1#\r\nc#3#\r\na#0#\r\n"                     # sorted, with a duplicate
    yield b"key#value\r\n#lost#\r\nnext#kept#"                   # a line break empties the value
    yield b"k##v#x#y#\r\n#empty first#\r\n##\r\n"                # empty names, a NUL in the second
    yield b"\xc7\xc1\xb7\xd0#\xb0\xa1#\r\nA#B#\r\na#b#"           # bytes above 0x7f sort after ASCII
    yield b"no hash at all\r\n" + b"only#one\n"
    rng = random.Random(1)
    names = [bytes(rng.choice(b"abcAB_.\xb0\xc0") for _ in range(rng.randrange(0, 4))) for _ in range(300)]
    yield b"".join(n + b"#" + bytes([48 + k % 10]) + b"#\r\n" for k, n in enumerate(names))  # many ties
    yield bytes(rng.choice(b"ab#\r\n\0") for _ in range(4000))


def init():
    emu = Emulator(max_instructions=500_000_000).init_crt()
    emu.serve_data_files({})
    return emu, emu.scratch(size=CLIENT_STATE_SIZE)


def read_string(emu, p):
    data, length = emu.read_u32(p + 4), emu.read_u32(p + 8)
    return emu.mem_read(data, length) if data and length else b""


def work(state, item):
    emu, obj = state
    table, path = item
    loader, offset = LOADERS[table]
    with open(path, "rb") as f:
        data = f.read()
    mark = emu.heap_mark()
    emu.serve_data_files({"data\\" + table.lower(): data})
    emu.mem_write(obj, bytes(CLIENT_STATE_SIZE))
    emu.call(loader, this=obj)
    first, last = emu.read_u32(obj + offset + 4), emu.read_u32(obj + offset + 8)
    lines = [f"ok {(last - first) // 32 if first else 0}"]
    for p in range(first, last, 32) if first else []:
        lines.append(f"P {fnv1a64(read_string(emu, p)):016x} {fnv1a64(read_string(emu, p + 16)):016x}")
    emu.heap_release(mark)
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--install", default=os.path.join(ROOT, "original", "install"))
    ap.add_argument("--ragres", default=os.path.join(ROOT, "build", "dev", "src", "ragres"))
    ap.add_argument("--jobs", type=int, default=0, help="worker processes (0: one per core but one)")
    args = ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        items, names = [], []
        archives = ["data.grf"] + sorted(n for n in os.listdir(args.install) if n.endswith(".gpf"))
        for archive in archives:
            out = os.path.join(tmp, archive)
            for table in LOADERS:
                subprocess.run([args.ragres, "extract", os.path.join(args.install, archive), out,
                                f"data\\{table}"], capture_output=True)
                path = os.path.join(out, "data", table.lower())
                if os.path.exists(path):
                    items.append((table, path))
                    names.append(f"{archive}: {table}")
        real = len(items)
        for k, data in enumerate(synthetic()):
            path = os.path.join(tmp, f"synthetic{k}.txt")
            with open(path, "wb") as f:
                f.write(data)
            items.append(("mapNameTable.txt", path))
            names.append(f"synthetic {k}")
        out = subprocess.run([args.ragres, "decode-name-pairs", *[p for _, p in items]],
                             capture_output=True, text=True).stdout
        ours, current = {}, None
        for line in out.splitlines():
            if line.startswith("P "):
                ours[current].append(line)
            else:
                n, rest = line.split(" ", 1)
                current = int(n)
                ours[current] = [rest]
        originals = oracle_pool.run(items, init, work, args.jobs or None, label="name pairs")
        mismatches = []
        for n, want in enumerate(originals):
            got = "\n".join(ours.get(n, []))
            if got != want:
                diff = [f"    port     {a}\n    original {b}" for a, b in zip(got.split("\n"), want.split("\n"))
                        if a != b]
                mismatches.append(f"{names[n]}:\n" + "\n".join(diff[:4]))
    print(f"{real} real tables and {len(items) - real} synthetic ones compared, {len(mismatches)} differ")
    for line in mismatches[:20]:
        print("MISMATCH", line)
    print("FAILED" if mismatches else "OK")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
