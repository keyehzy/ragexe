"""Compares the port's attribute-map loader with the original (docs/formats/gat.md).

usage: .venv/bin/python -I tools/gat_oracle.py [--jobs J]

Runs C3dAttr::Load (00426ec0) in the emulation oracle on every .gat of data.grf and
compares the version, size, cell size and every cell with `ragres decode-gats`.
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

C3DATTR_CTOR, C3DATTR_LOAD = 0x426BA0, 0x426EC0  # analysis/labels


def fnv1a64(data, h=0xCBF29CE484222325):
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def init():
    emu = Emulator(max_instructions=500_000_000).init_crt()
    return emu, emu.scratch(size=0x12C)


def work(state, path):
    emu, obj = state
    with open(path, "rb") as f:
        data = f.read()
    mark = emu.heap_mark()
    emu.serve_data_files({"data\\t.gat": data})
    emu.mem_write(obj, bytes(0x12C))
    emu.call(C3DATTR_CTOR, this=obj)
    ok = emu.call(C3DATTR_LOAD, emu.cstr("t.gat"), this=obj).eax & 0xFF
    if ok:
        width, height, cell_size = struct.unpack("<iii", emu.mem_read(obj + 0x110, 12))
        cells = emu.mem_read(emu.read_u32(obj + 0x120), width * height * 20) if width * height else b""
        line = f"ok {data[4]} {data[5]} {width} {height} {cell_size} {fnv1a64(cells):016x}"
    else:
        line = "failed"
    emu.heap_release(mark)
    return line


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--archive", default=os.path.join(ROOT, "original", "install", "data.grf"))
    ap.add_argument("--ragres", default=os.path.join(ROOT, "build", "dev", "src", "ragres"))
    ap.add_argument("--jobs", type=int, default=0, help="worker processes (0: one per core but one)")
    args = ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([args.ragres, "extract", args.archive, tmp, "*.gat"], check=True, capture_output=True)
        files = sorted(os.path.join(d, n) for d, _, ns in os.walk(tmp) for n in ns)
        out = subprocess.run([args.ragres, "decode-gats", *files], capture_output=True, text=True).stdout
        ours = {}
        for line in out.splitlines():
            n, rest = line.split(" ", 1)
            ours[int(n)] = "failed" if rest.startswith("failed") else rest
        originals = oracle_pool.run(files, init, work, args.jobs or None, label="gat")
        mismatches = [f"{os.path.relpath(p, tmp)}: port {ours.get(n)!r}, original {want!r}"
                      for n, (p, want) in enumerate(zip(files, originals)) if ours.get(n) != want]
    print(f"{len(files)} attribute maps compared, {len(mismatches)} differ")
    for line in mismatches[:20]:
        print("MISMATCH", line)
    print("FAILED" if mismatches else "OK")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
