"""Compares the port's world loader with the original (docs/formats/rsw.md).

usage: .venv/bin/python -I tools/rsw_oracle.py [--jobs J]

Runs C3dWorldRes::Load (00435dd0) in the emulation oracle on every .rsw of data.grf and on
synthetic worlds of every version, with the ISLIGHTMAP option (0061ec60) on and off, and
compares the names, the water and light values, the models, the sound and effect records
and the scene-graph bounds with `ragres decode-rsws`.
"""
import argparse
import math
import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import oracle_pool  # noqa: E402
from emu import Emulator  # noqa: E402

C3DWORLDRES_CTOR, C3DWORLDRES_LOAD = 0x435B90, 0x435DD0  # analysis/labels
C3DWORLDRES_SIZE = 0x1C0
ISLIGHTMAP = 0x61EC60


def fnv1a64(data, h=0xCBF29CE484222325):
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def name40(s):
    return s.ljust(40, b"\0")


def world(major, minor, objects=(), angles=(30, 60), speeds=(1.0,), names=None):
    """A world file of the given version; objects are (type, payload) pairs."""
    def at_least(ma, mi):
        return major > ma or (major == ma and minor >= mi)
    out = b"GRSW" + bytes((major, minor))
    names = names or [b"tool.ini", b"map.gnd", b"map.gat", b"map.src"]
    out += name40(names[0]) + name40(names[1])
    if at_least(1, 4):
        out += name40(names[2])
    out += name40(names[3])
    if at_least(1, 3):
        out += struct.pack("<f", -12.5)
    if at_least(1, 8):
        out += struct.pack("<ifff", 3, 0.75, 7.9, 40.2)
    if at_least(1, 9):
        out += struct.pack("<i", 6)
    if at_least(1, 5):
        out += struct.pack("<ii6f", *angles, 0.9, 0.8, 0.7, 0.6, 0.75, 0.3)
    if at_least(1, 7):
        out += struct.pack("<f", 0.5)
    if at_least(1, 6):
        out += struct.pack("<4i", -1, 2, -3, 4)
    out += struct.pack("<i", len(objects))
    for kind, payload in objects:
        out += struct.pack("<i", kind) + payload
    if (major, minor) == (2, 1):
        out += b"".join(struct.pack("<12f", *(k + i / 16 for i in range(12))) for k in range(1365))
    return out


def model(major, minor, k, speed=1.0):
    extra = b""
    if major > 1 or (major == 1 and minor >= 3):
        extra = name40(b"model%d" % k) + struct.pack("<ifI", 7 - k, speed, 0x01020304 + k)
    return extra + (b"prop%d.rsm" % k).ljust(80, b"\0") + b"node".ljust(80, b"\0") + \
        struct.pack("<9f", *(k * 9 + i + 0.25 for i in range(9)))


def sound(major, k):
    long_name = b"s" * 79 + b"x"  # 80 characters with no NUL: strncpy keeps 79
    body = long_name + (b"wav%d.wav" % k).ljust(80, b"\0") + struct.pack("<7I", *range(k, k + 7))
    return body + (struct.pack("<f", 2.5) if major >= 2 else b"")


def synthetic():
    yield world(2, 2)  # unsupported
    yield world(3, 0)  # unsupported
    yield world(0, 9, [(1, model(0, 9, 1))])
    for minor in range(2, 10):
        objects = [(1, model(1, minor, 1)), (2, bytes(range(0x6C))), (3, sound(1, minor)),
                   (9, b""), (1, model(1, minor, 2))]
        yield world(1, minor, objects)
    for minor in (0, 1):
        objects = [(1, model(2, minor, k, s)) for k, s in
                   enumerate((150.0, -1.0, 0.0, 100.0, 1e-30, math.nan))]
        objects += [(3, sound(2, 1)), (4, bytes(range(0x74))), (4, bytes(range(1, 0x75)))]
        yield world(2, minor, objects)
    for angles in ((0, 0), (90, -90), (180, 270), (-45, 359), (720, 12345), (100000, -77777)):
        yield world(2, 0, angles=angles)


def init():
    emu = Emulator(max_instructions=2_000_000_000).init_crt()
    return emu, emu.scratch(size=C3DWORLDRES_SIZE)


def read_string(emu, at):
    buffer, length = emu.read_u32(at + 4), emu.read_u32(at + 8)
    return emu.mem_read(buffer, length) if buffer and length else b""


def list_values(emu, at):
    head = emu.read_u32(at + 4)
    node, values = emu.read_u32(head), []
    while node != head:
        values.append(emu.read_u32(node + 8))
        node = emu.read_u32(node)
    return values


def cstring(emu, at, limit):
    return emu.mem_read(at, limit).split(b"\0")[0]


def work(state, item):
    emu, obj = state
    path, lightmaps = item
    with open(path, "rb") as f:
        data = f.read()
    mark = emu.heap_mark()
    emu.serve_data_files({"data\\t.rsw": data})
    emu.write_u32(ISLIGHTMAP, lightmaps)
    emu.mem_write(obj, bytes(C3DWORLDRES_SIZE))
    emu.call(C3DWORLDRES_CTOR, this=obj)
    ok = emu.call(C3DWORLDRES_LOAD, emu.cstr("t.rsw"), this=obj).eax & 0xFF
    if not ok:
        emu.heap_release(mark)
        return "failed"
    major, minor = emu.mem_read(obj + 0x1AC, 2)
    hexes = [read_string(emu, obj + off).hex() or "-" for off in (0x138, 0x148, 0x158)]
    w = struct.unpack("<IiIIIiii", emu.mem_read(obj + 0x168, 32))
    floats = struct.unpack("<9I", emu.mem_read(obj + 0x188, 36))
    area = struct.unpack("<4i", emu.mem_read(obj + 0x1B0, 16))
    models = b""
    model_list = list_values(emu, obj + 0x110)
    for m in model_list:
        models += cstring(emu, m, 40) + b"\0" + cstring(emu, m + 0x28, 80) + b"\0" + \
            cstring(emu, m + 0x78, 80) + b"\0" + emu.mem_read(m + 0xC8, 0x30)
    effects = b"".join(emu.mem_read(p, 0x74) for p in list_values(emu, obj + 0x11C))
    sound_list = list_values(emu, obj + 0x128)
    sounds = b"".join(emu.mem_read(p, 0xC0) for p in sound_list)
    bounds, count = b"", 0
    root = emu.read_u32(obj + 0x134)
    stack = [root] if root else []
    while stack:
        node = stack.pop()
        b = emu.mem_read(node, 0x44)
        bounds += b[0x20:0x2C] + b[0x14:0x20] + b[0x38:0x44] + b[0x2C:0x38]
        count += 1
        stack += reversed([struct.unpack_from("<I", b, 4 + 4 * k)[0] for k in range(4)
                           if struct.unpack_from("<I", b, 4 + 4 * k)[0]])
    emu.heap_release(mark)
    return (f"ok {major} {minor} {' '.join(hexes)} {w[0]:08x} {w[1]} {w[2]:08x} {w[3]:08x} "
            f"{w[4]:08x} {w[5]} {w[6]} {w[7]} " + " ".join(f"{v:08x}" for v in floats) + " " +
            " ".join(str(v) for v in area) +
            f" {len(model_list)}:{fnv1a64(models):016x} {len(sound_list)}:{fnv1a64(sounds):016x}"
            f" {len(effects) // 0x74}:{fnv1a64(effects):016x} {count}:{fnv1a64(bounds):016x}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--archive", default=os.path.join(ROOT, "original", "install", "data.grf"))
    ap.add_argument("--ragres", default=os.path.join(ROOT, "build", "dev", "src", "ragres"))
    ap.add_argument("--jobs", type=int, default=0, help="worker processes (0: one per core but one)")
    args = ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        files_dir = os.path.join(tmp, "files")
        subprocess.run([args.ragres, "extract", args.archive, files_dir, "*.rsw"], check=True,
                       capture_output=True)
        files = sorted(os.path.join(d, n) for d, _, ns in os.walk(files_dir) for n in ns)
        for k, data in enumerate(synthetic()):
            path = os.path.join(tmp, f"synthetic{k}.rsw")
            with open(path, "wb") as f:
                f.write(data)
            files.append(path)
        mismatches = []
        for lightmaps in (1, 0):
            flags = [] if lightmaps else ["--no-lightmaps"]
            out = subprocess.run([args.ragres, "decode-rsws", *flags, *files], capture_output=True,
                                 text=True).stdout
            ours = {}
            for line in out.splitlines():
                n, rest = line.split(" ", 1)
                ours[int(n)] = "failed" if rest.startswith("failed") else rest
            originals = oracle_pool.run([(p, lightmaps) for p in files], init, work,
                                        args.jobs or None, label=f"rsw, lightmaps {lightmaps}")
            mismatches += [f"{os.path.relpath(p, tmp)} (lightmaps {lightmaps}): port "
                           f"{ours.get(n)!r}, original {want!r}"
                           for n, (p, want) in enumerate(zip(files, originals)) if ours.get(n) != want]
    real = sum(1 for p in files if not os.path.basename(p).startswith("synthetic"))
    print(f"{real} worlds and {len(files) - real} synthetic ones compared at both lightmap "
          f"settings, {len(mismatches)} differ")
    for line in mismatches[:20]:
        print("MISMATCH", line)
    print("FAILED" if mismatches else "OK")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
