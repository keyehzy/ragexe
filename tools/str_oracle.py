"""Compares the port's effect loader with the original (docs/formats/str.md).

usage: .venv/bin/python -I tools/str_oracle.py [--jobs J]

Runs CEZeffectRes::Load (00428b50) in the emulation oracle on every .str of data.grf and
on synthetic ones, and compares the header values, every layer's texture names (read
through the original's pointers) and keys with `ragres decode-strs`. It also checks that
every texture slot past a layer's count points at "1.bmp".
"""
import argparse
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

CEZEFFECTRES_CTOR, CEZEFFECTRES_LOAD = 0x428AA0, 0x428B50  # analysis/labels
CEZEFFECTRES_SIZE = 0xD3B4
ONE_BMP = 0x606044
SLOTS = 110


def fnv1a64(data, h=0xCBF29CE484222325):
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def effect(layers, seed, version=0x94, a=60, b=120):
    """layers: (texture count, key count) pairs."""
    rng = random.Random(seed)
    out = b"STRM" + struct.pack("<iiii", version, a, b, len(layers)) + bytes(16)
    for textures, keys in layers:
        out += struct.pack("<i", textures)
        for k in range(max(textures, 0)):
            out += (b"\xc0\xcc\xc6\xd1%02d.bmp" % k).ljust(128, b"\0")
        out += struct.pack("<i", keys)
        for k in range(max(keys, 0)):
            out += struct.pack("<ii", 3 * k, k % 2) + bytes(rng.randrange(256) for _ in range(29 * 4))
    return out


def synthetic():
    yield effect([(2, 3), (0, 1), (5, 0)], 1)
    yield effect([(1, 1)] * 60, 2, a=30, b=-5)  # every layer the array holds
    yield effect([(110, 2)], 3)  # every texture slot
    yield effect([(-3, -2), (1, 1)], 4)  # negative counts read nothing
    yield effect([], 5)
    yield effect([(1, 1)], 6, version=0x93)  # refused


def init():
    emu = Emulator(max_instructions=500_000_000).init_crt()
    return emu, emu.scratch(size=CEZEFFECTRES_SIZE)


def work(state, path):
    emu, obj = state
    with open(path, "rb") as f:
        data = f.read()
    mark = emu.heap_mark()
    emu.serve_data_files({"data\\t.str": data})
    emu.mem_write(obj, bytes(CEZEFFECTRES_SIZE))
    emu.call(CEZEFFECTRES_CTOR, this=obj)
    ok = emu.call(CEZEFFECTRES_LOAD, emu.cstr("t.str"), this=obj).eax & 0xFF
    if not ok:
        emu.heap_release(mark)
        return "failed"
    f110, f114, layers = struct.unpack("<iii", emu.mem_read(obj + 0x110, 12))
    lines = [f"ok {f110} {f114} {max(layers, 0)}"]
    for k in range(max(layers, 0)):
        entry = obj + 0x120 + k * 0x380
        count = struct.unpack("<i", emu.mem_read(entry, 4))[0]
        pointers = struct.unpack(f"<{SLOTS}I", emu.mem_read(entry + 0x1C0, SLOTS * 4))
        names = b"".join(emu.mem_read(pointers[t], 128).split(b"\0")[0] + b"\0"
                         for t in range(max(count, 0)))
        rest = [p for p in pointers[max(count, 0):] if p != ONE_BMP]
        if rest:
            emu.heap_release(mark)
            return f"layer {k}: a slot past the count does not point at 1.bmp"
        keys = struct.unpack("<i", emu.mem_read(entry + 0x378, 4))[0]
        key_bytes = emu.mem_read(emu.read_u32(entry + 0x37C), keys * 0x7C) if keys > 0 else b""
        lines.append(f"L {k} {count} {max(count, 0)}:{fnv1a64(names):016x} "
                     f"{max(keys, 0)}:{fnv1a64(key_bytes):016x}")
    emu.heap_release(mark)
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--archive", default=os.path.join(ROOT, "original", "install", "data.grf"))
    ap.add_argument("--ragres", default=os.path.join(ROOT, "build", "dev", "src", "ragres"))
    ap.add_argument("--jobs", type=int, default=0, help="worker processes (0: one per core but one)")
    args = ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        files_dir = os.path.join(tmp, "files")
        subprocess.run([args.ragres, "extract", args.archive, files_dir, "*.str"], check=True,
                       capture_output=True)
        files = sorted(os.path.join(d, n) for d, _, ns in os.walk(files_dir) for n in ns)
        for k, data in enumerate(synthetic()):
            path = os.path.join(tmp, f"synthetic{k}.str")
            with open(path, "wb") as f:
                f.write(data)
            files.append(path)
        out = subprocess.run([args.ragres, "decode-strs", *files], capture_output=True, text=True).stdout
        ours, current = {}, None
        for line in out.splitlines():
            if line.startswith("L "):
                ours[current].append(line)
                continue
            n, rest = line.split(" ", 1)
            current = int(n)
            ours[current] = ["failed"] if rest.startswith("failed") else [rest]
        ours = {n: "\n".join(lines) for n, lines in ours.items()}
        originals = oracle_pool.run(files, init, work, args.jobs or None, label="str")
        mismatches = []
        for n, (p, want) in enumerate(zip(files, originals)):
            got = ours.get(n)
            if got != want:
                diff = [f"    port     {a}\n    original {b}" for a, b in
                        zip((got or "").split("\n"), want.split("\n")) if a != b]
                mismatches.append(f"{os.path.relpath(p, tmp)}:\n" + "\n".join(diff[:4]))
    real = sum(1 for p in files if not os.path.basename(p).startswith("synthetic"))
    print(f"{real} effects and {len(files) - real} synthetic ones compared, {len(mismatches)} differ")
    for line in mismatches[:20]:
        print("MISMATCH", line)
    print("FAILED" if mismatches else "OK")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
