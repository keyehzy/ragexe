"""Compares the port's .imf loader with the original (docs/formats/imf.md).

usage: .venv/bin/python -I tools/imf_oracle.py [--jobs J]

Runs CImfRes::Load (00429c40) in the emulation oracle on every .imf of data.grf and on
synthetic ones, and compares every layer, action and frame with `ragres decode-imfs`.
"""
import argparse
import math
import os
import random
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import oracle_pool  # noqa: E402
from emu import Emulator  # noqa: E402

CIMFRES_CTOR, CIMFRES_LOAD = 0x429BB0, 0x429C40  # analysis/labels
CIMFRES_SIZE = 0x19BC


def fnv1a64(data, h=0xCBF29CE484222325):
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def imf(version, layers, seed, field_4=0):
    """layers: a list of per-layer action frame counts; None for a negative count."""
    rng = random.Random(seed)
    out = struct.pack("<fIi", version, field_4, len(layers) - 1)
    for actions in layers:
        if actions is None:
            out += struct.pack("<i", -3)
            continue
        out += struct.pack("<i", len(actions))
        for frames in actions:
            out += struct.pack("<i", frames)
            for _ in range(max(frames, 0)):
                out += struct.pack("<3i", rng.randrange(2), rng.randrange(-40, 40), rng.randrange(-40, 40))
    return out


def synthetic():
    yield imf(1.01, [[3, 0, 2], [1]], 1)
    yield imf(2.5, [[4] * 104, [2] * 7, [], [1, -2, 3]], 2, field_4=0xDEADBEEF)
    yield imf(1.01, [[1]] * 15, 3)  # as many layers as the tables hold
    yield imf(1.01, [None, [2]], 4)  # a negative action count
    yield struct.pack("<fIi", 1.01, 0, -1)  # no layers
    yield struct.pack("<fIi", 1.01, 0, -7)
    yield imf(1.0, [[1]], 5)  # an old version
    yield imf(math.nan, [[1]], 6)


def init():
    emu = Emulator(max_instructions=500_000_000).init_crt()
    return emu, emu.scratch(size=CIMFRES_SIZE)


def vector(emu, at, size):
    begin, end = emu.read_u32(at + 4), emu.read_u32(at + 8)
    return [begin + k * size for k in range((end - begin) // size)] if begin else []


def work(state, path):
    emu, obj = state
    with open(path, "rb") as f:
        data = f.read()
    mark = emu.heap_mark()
    emu.serve_data_files({"data\\t.imf": data})
    emu.mem_write(obj, bytes(CIMFRES_SIZE))
    emu.call(CIMFRES_CTOR, this=obj)
    ok = emu.call(CIMFRES_LOAD, emu.cstr("t.imf"), this=obj).eax & 0xFF
    if not ok:
        emu.heap_release(mark)
        return "failed"
    values, actions, frames = [], 0, 0
    layers = vector(emu, obj + 0x110, 16)
    for layer in layers:
        records = vector(emu, layer, 0x30)
        values.append(len(records))
        actions += len(records)
        for record in records:
            order, x, y = (vector(emu, record + o, 4) for o in (0, 0x10, 0x20))
            values.append(len(order))
            frames += len(order)
            for a, b, c in zip(order, x, y):
                values += [emu.read_u32(a), emu.read_u32(b), emu.read_u32(c)]
    version, field_4 = struct.unpack("<II", data[:8])
    emu.heap_release(mark)
    h = fnv1a64(struct.pack(f"<{len(values)}I", *values))
    return f"ok {version:08x} {field_4} {len(layers)} {actions} {frames} {h:016x}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--archive", default=os.path.join(ROOT, "original", "install", "data.grf"))
    ap.add_argument("--ragres", default=os.path.join(ROOT, "build", "dev", "src", "ragres"))
    ap.add_argument("--jobs", type=int, default=0, help="worker processes (0: one per core but one)")
    args = ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        files_dir = os.path.join(tmp, "files")
        subprocess.run([args.ragres, "extract", args.archive, files_dir, "*.imf"], check=True,
                       capture_output=True)
        files = sorted(os.path.join(d, n) for d, _, ns in os.walk(files_dir) for n in ns)
        for k, data in enumerate(synthetic()):
            path = os.path.join(tmp, f"synthetic{k}.imf")
            with open(path, "wb") as f:
                f.write(data)
            files.append(path)
        out = subprocess.run([args.ragres, "decode-imfs", *files], capture_output=True, text=True).stdout
        ours = {}
        for line in out.splitlines():
            n, rest = line.split(" ", 1)
            ours[int(n)] = "failed" if rest.startswith("failed") else rest
        originals = oracle_pool.run(files, init, work, args.jobs or None, label="imf")
        mismatches = [f"{os.path.relpath(p, tmp)}: port {ours.get(n)!r}, original {want!r}"
                      for n, (p, want) in enumerate(zip(files, originals)) if ours.get(n) != want]
    real = sum(1 for p in files if not os.path.basename(p).startswith("synthetic"))
    print(f"{real} .imf files and {len(files) - real} synthetic ones compared, {len(mismatches)} differ")
    for line in mismatches[:20]:
        print("MISMATCH", line)
    print("FAILED" if mismatches else "OK")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
