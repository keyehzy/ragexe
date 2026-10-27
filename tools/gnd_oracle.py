"""Compares the port's ground loader with the original (docs/formats/gnd.md).

usage: .venv/bin/python -I tools/gnd_oracle.py [--jobs J]

Runs CGndRes::Load (00429200) in the emulation oracle on every .gnd of data.grf and on
synthetic grounds of every minor version, and compares the header, the texture names, the
lightmaps, the surfaces and every cell with `ragres decode-gnds`. Cells come from the
original GetCell (004295f0): for every cell of a small ground, and for the first and last
of a large one, whose other cells are read from the record array GetCell indexes.
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

CGNDRES_CTOR, CGNDRES_LOAD, CGNDRES_GET_CELL = 0x429170, 0x429200, 0x4295F0  # analysis/labels
CGNDRES_SIZE = 0x1E8
SMALL = 64  # grounds of at most this many cells go through GetCell cell by cell


def fnv1a64(data, h=0xCBF29CE484222325):
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def u32s(values):
    return struct.pack(f"<{len(values)}I", *(v & 0xFFFFFFFF for v in values))


def cell_record(minor, data, index):
    """GetCell's conversion of one record, as seven dwords."""
    if minor == 7:
        return list(struct.unpack_from("<7I", data, index * 0x1C))
    *heights, a, b, c = struct.unpack_from("<4I3h", data, index * 0x18)
    return heights + [v & 0xFFFFFFFF for v in (a, b, c)]


def ground(minor, major=1, width=3, height=2, names=(b"a.bmp", b"\xb9\xd9\xb4\xda.bmp"),
           name_size=80, lightmaps=2, surfaces=3):
    """A ground file of the given version, with every field distinct."""
    out = b"GRGN" + bytes((major, minor))
    out += struct.pack("<iifii", width, height, 12.5, len(names), name_size)
    out += b"".join(n.ljust(name_size, b"\0") for n in names)
    out += struct.pack("<iiii", lightmaps, 8, 8, 1)
    out += bytes((k * 7 + i) & 0xFF for k in range(lightmaps) for i in range(256))
    out += struct.pack("<i", surfaces)
    for k in range(surfaces):
        out += struct.pack("<8f", *(k + i / 8 for i in range(8)))
        out += struct.pack("<hHI", -1 if k == 0 else k % len(names), 0xFFFF - k, 0x80C0E0F0 + k)
    indices = [-1, 0, 1, 2, -2, 0x7FFF, -0x8000]
    for i in range(width * height):
        out += struct.pack("<4f", i, -i, i / 3, 1e30 * i)
        picks = [indices[(i + k) % len(indices)] for k in range(3)]
        out += struct.pack("<3i", *picks) if minor == 7 else struct.pack("<3h2x", *picks)
    return out


def synthetic():
    yield from (ground(minor) for minor in range(8))
    yield ground(7, major=9)  # the major version is not checked
    yield ground(8)  # fails in both
    yield ground(7, width=0, height=0, lightmaps=0, surfaces=0, names=())
    yield ground(6, width=12, height=9)  # through the record array, not cell by cell


def init():
    emu = Emulator(max_instructions=500_000_000).init_crt()
    return emu, emu.scratch(size=CGNDRES_SIZE), emu.scratch(size=0x1C)


def work(state, path):
    emu, obj, cell = state
    with open(path, "rb") as f:
        data = f.read()
    mark = emu.heap_mark()
    emu.serve_data_files({"data\\t.gnd": data})
    emu.mem_write(obj, bytes(CGNDRES_SIZE))
    emu.call(CGNDRES_CTOR, this=obj)
    ok = emu.call(CGNDRES_LOAD, emu.cstr("t.gnd"), this=obj).eax & 0xFF
    if not ok:
        emu.heap_release(mark)
        return "failed"
    major, minor = emu.mem_read(obj + 0x114, 2)
    width, height, size_bits, textures, surfaces, lightmaps = struct.unpack(
        "<iiIiii", emu.mem_read(obj + 0x118, 24))
    begin, end = emu.read_u32(obj + 0x134), emu.read_u32(obj + 0x138)
    names = b"".join(emu.mem_read(emu.read_u32(p), 256).split(b"\0")[0] + b"\0"
                     for p in range(begin, end, 4))
    lightmap_bytes = emu.mem_read(emu.read_u32(obj + 0x140), lightmaps * 256) if lightmaps else b""
    surface_values = []
    raw = emu.mem_read(emu.read_u32(obj + 0x144), surfaces * 40) if surfaces else b""
    for k in range(surfaces):
        *uv, texture, lightmap, color = struct.unpack_from("<8IhHI", raw, k * 40)
        surface_values += uv + [texture, lightmap, color]

    count = width * height
    record = 0x1C if minor == 7 else 0x18
    array = emu.read_u32(obj + (0x150 if minor == 7 else 0x14C if minor == 6 else 0x148))
    records = emu.mem_read(array, count * record) if count else b""

    def get_cell(index):
        emu.call(CGNDRES_GET_CELL, cell, index, this=obj)
        return list(struct.unpack("<7I", emu.mem_read(cell, 0x1C)))

    if count <= SMALL:
        cells = [get_cell(i) for i in range(count)]
    else:
        cells = [cell_record(minor, records, i) for i in range(count)]
        for i in (0, count - 1):
            if get_cell(i) != cells[i]:
                emu.heap_release(mark)
                return f"GetCell({i}) differs from the record array"
    cell_values = [v for c in cells for v in c]
    emu.heap_release(mark)
    return (f"ok {major} {minor} {width} {height} {size_bits:08x} "
            f"{(end - begin) // 4}:{fnv1a64(names):016x} "
            f"{lightmaps}:{fnv1a64(lightmap_bytes):016x} "
            f"{surfaces}:{fnv1a64(u32s(surface_values)):016x} "
            f"{count}:{fnv1a64(u32s(cell_values)):016x}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--archive", default=os.path.join(ROOT, "original", "install", "data.grf"))
    ap.add_argument("--ragres", default=os.path.join(ROOT, "build", "dev", "src", "ragres"))
    ap.add_argument("--jobs", type=int, default=0, help="worker processes (0: one per core but one)")
    args = ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        files_dir = os.path.join(tmp, "files")
        subprocess.run([args.ragres, "extract", args.archive, files_dir, "*.gnd"], check=True,
                       capture_output=True)
        files = sorted(os.path.join(d, n) for d, _, ns in os.walk(files_dir) for n in ns)
        for k, data in enumerate(synthetic()):
            path = os.path.join(tmp, f"synthetic{k}.gnd")
            with open(path, "wb") as f:
                f.write(data)
            files.append(path)
        out = subprocess.run([args.ragres, "decode-gnds", *files], capture_output=True, text=True).stdout
        ours = {}
        for line in out.splitlines():
            n, rest = line.split(" ", 1)
            ours[int(n)] = "failed" if rest.startswith("failed") else rest
        originals = oracle_pool.run(files, init, work, args.jobs or None, label="gnd")
        mismatches = [f"{os.path.relpath(p, tmp)}: port {ours.get(n)!r}, original {want!r}"
                      for n, (p, want) in enumerate(zip(files, originals)) if ours.get(n) != want]
    real = sum(1 for p in files if not os.path.basename(p).startswith("synthetic"))
    print(f"{real} grounds and {len(files) - real} synthetic ones compared, {len(mismatches)} differ")
    for line in mismatches[:20]:
        print("MISMATCH", line)
    print("FAILED" if mismatches else "OK")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
