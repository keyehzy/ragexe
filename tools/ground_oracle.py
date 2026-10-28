"""Compares the port's ground builder with the original on the maps of data.grf
(docs/render/ground.md).

usage: .venv/bin/python -I tools/ground_oracle.py [--sample N] [--jobs J]

Each .gnd goes through the original CGndRes::Load and C3dGround15::Build in the
emulation oracle (tools/ground_vectors.py's set-up: the texture cache and texture
creation replaced, the x87 at single precision), with ISLIGHTMAP off and on, and is
hashed as `ragres build-grounds` hashes the port's: the cells, the surfaces with their
vertices, the atlas blocks and pages. --sample takes the smallest maps and a spread of
the rest; 0 takes every map.
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
from ground_vectors import PAGE_BASE, TEXTURE_BASE, build, fnv1a64  # noqa: E402

LIGHT = ((0.4, -0.8, 0.45), (0.95, 0.9, 0.85), (0.35, 0.3, 0.3))


def original(item):
    path, lightmaps = item
    with open(path, "rb") as f:
        data = f.read()
    emu, g, pages = build(data, lightmaps, LIGHT)
    u = emu.read_u32
    width, height = u(g + 8), u(g + 0xC)
    surfaces_begin = u(g + 0xA0)
    surface_count = (u(g + 0xA4) - surfaces_begin) // 0x20
    blocks_begin, block_count = u(g + 0x14 + 8), u(g + 0x14)
    cells = emu.mem_read(u(g + 0xC0), width * height * 0x54)

    def index(p):
        return 0xFFFFFFFF if p == 0 else (p - surfaces_begin) // 0x20

    out, used = [], set()
    buf = bytearray()
    for c in range(width * height):
        rec = cells[c * 0x54:(c + 1) * 0x54]
        top, east, north = struct.unpack_from("<3I", rec, 0x40)
        for s in (top, north, east):
            buf += struct.pack("<I", index(s))
            if s:
                used.add(index(s))
        buf += rec[0x50:0x53]
    out.append(f"{width * height}:{fnv1a64(buf):016x}")
    buf = bytearray()
    for s in range(surface_count):
        rec = surfaces_begin + s * 0x20
        buf.append(1 if s in used else 0)
        if s not in used:
            continue
        texture = (u(rec + 8) - TEXTURE_BASE) // 0x10
        block = (u(rec + 0x10) - blocks_begin) // 0x128 if lightmaps else -1
        buf += struct.pack("<iiI", texture, block, u(rec + 0x14))
        for v in range(4):
            vals = list(struct.unpack("<8I", emu.mem_read(u(rec) + v * 0x20, 0x20)))
            if not lightmaps:
                vals[6] = vals[7] = 0
            buf += struct.pack("<8I", *vals)
    out.append(f"{surface_count}:{fnv1a64(buf):016x}")
    buf = bytearray()
    blocks = block_count if lightmaps else 0
    for b in range(blocks):
        rec = blocks_begin + b * 0x128
        buf += struct.pack("<I", (u(rec + 0x104) - PAGE_BASE) // 0x10)
        buf += emu.mem_read(rec + 0x100, 3) + emu.mem_read(rec + 0x108, 0x20)
    out.append(f"{blocks}:{fnv1a64(buf):016x}")
    for p, (_, _, _, px) in enumerate(pages):
        if p == 0 and blocks < 1024:
            px = b"".join(px[((i // 32 * 8 + y) * 256 + i % 32 * 8) * 4:][:32]
                          for i in range(blocks) for y in range(8))
        out.append(f"{p}:{fnv1a64(px):016x}")
    return "ok " + " ".join(out)


def init():
    return None


def work(_, item):
    return original(item)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--archive", default=os.path.join(ROOT, "original", "install", "data.grf"))
    ap.add_argument("--ragres", default=os.path.join(ROOT, "build", "dev", "src", "ragres"))
    ap.add_argument("--sample", type=int, default=0)
    ap.add_argument("--jobs", type=int, default=0, help="worker processes (0: one per core but one)")
    args = ap.parse_args()
    listing = subprocess.run([args.ragres, "list", args.archive], capture_output=True, text=True).stdout
    grounds = sorted((int(line.split("\t")[0]), line.split("\t")[4]) for line in listing.splitlines()
                     if line.lower().endswith(".gnd"))
    if args.sample and args.sample < len(grounds):
        step = len(grounds) / (args.sample - 4)
        picked = grounds[:4] + [grounds[int(k * step)] for k in range(1, args.sample - 4)]
        grounds = sorted(set(picked))
    with tempfile.TemporaryDirectory() as tmp:
        paths = []
        for _, name in grounds:
            subprocess.run([args.ragres, "extract", args.archive, tmp, name], capture_output=True)
            paths.append(os.path.join(tmp, *name.split("\\")))
        items = [(p, lm) for p in paths for lm in (0, 1)]
        originals = oracle_pool.run(items, init, work, args.jobs or None, label="grounds")
        light = [str(x) for vec in LIGHT for x in vec]
        mismatches = []
        for (path, lm), want in zip(items, originals):
            command = [args.ragres, "build-grounds"] + (["--lightmaps"] if lm else []) + light + [path]
            got = subprocess.run(command, capture_output=True, text=True).stdout.strip()
            got = got.split(" ", 1)[1] if " " in got else got
            if got != want:
                mismatches.append(f"{os.path.basename(path)} (ISLIGHTMAP {lm}):\n    port     {got}\n"
                                  f"    original {want}")
    print(f"{len(items)} builds of {len(paths)} maps compared, {len(mismatches)} differ")
    for line in mismatches[:20]:
        print("MISMATCH", line)
    print("FAILED" if mismatches else "OK")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
