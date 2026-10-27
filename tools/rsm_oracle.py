"""Compares the port's model loader with the original (docs/formats/rsm.md).

usage: .venv/bin/python -I tools/rsm_oracle.py [--jobs J]

Runs C3dModelRes::Load (0042be00) in the emulation oracle on every .rsm of data.grf and on
synthetic models of every version, with TextureCache::GetTexture replaced by a stand-in
that records the name, and compares the header, every node (names, links, textures,
placement, keys) and every mesh (vertices, texture coordinates, the sorted faces, face and
corner normals), and the volumes with `ragres decode-rsms`.
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

C3DMODELRES_CTOR, C3DMODELRES_LOAD = 0x42BB00, 0x42BE00  # analysis/labels
C3DMODELRES_SIZE = 0x1A8
TEXTURECACHE_GET_TEXTURE = 0x40CC00


def fnv1a64(data, h=0xCBF29CE484222325):
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def hashed(count, data):
    return f"{count}:{fnv1a64(data):016x}"


def name40(s):
    return s.ljust(40, b"\0")


def node(minor, name, parent, textures, rng, vertices=8, faces=24, degenerate=False,
         keys=(0, 0)):
    out = name40(name) + name40(parent)
    out += struct.pack(f"<i{len(textures)}i", len(textures), *textures)
    m = [rng.uniform(-2, 2) for _ in range(9)] + [rng.uniform(-50, 50) for _ in range(3)]
    out += struct.pack("<12f", *m)
    out += struct.pack("<3f", 1.5, -2.25, 3.0) + struct.pack("<f", 0.75)
    out += struct.pack("<3f", 0.0, 1.0, 0.0) + struct.pack("<3f", 1.0, 2.0, 0.5)
    points = [[rng.uniform(-20, 20) for _ in range(3)] for _ in range(vertices)]
    if degenerate:
        points[1] = list(points[0])  # faces on vertices 0, 1, 2 have no area
    out += struct.pack("<i", vertices) + b"".join(struct.pack("<3f", *p) for p in points)
    out += struct.pack("<i", vertices)
    for k in range(vertices):
        uv = (k / vertices, 1 - k / vertices)
        out += struct.pack("<2f", *uv) if minor == 1 else struct.pack("<I2f", 0x80FFFFFF - k, *uv)
    out += struct.pack("<i", faces)
    for k in range(faces):
        v = [rng.randrange(vertices) for _ in range(3)]
        if k % 7 == 0:
            v[2] = v[0]  # a vertex used twice
        if degenerate and k < 3:
            v = [0, 1, 2]
        t = [rng.randrange(vertices) for _ in range(3)]
        tex = rng.randrange(len(textures)) if textures else 0
        if minor == 1:
            out += struct.pack("<7H6x", *v, *t, tex)
        else:
            out += struct.pack("<7HHii", *v, *t, tex, 0xBEEF - k, k * 3 + 1, rng.choice((0, 1, 2, 31)))
    if minor >= 5:
        out += struct.pack("<i", keys[0])
        out += b"".join(struct.pack("<i3f", 10 * k, k, -k, 0.5 * k) for k in range(keys[0]))
    out += struct.pack("<i", keys[1])
    out += b"".join(struct.pack("<i4f", 10 * k, 0.0, 0.0, 0.1 * k, 1.0) for k in range(keys[1]))
    return out


def model(minor, seed, degenerate=False, major=1, magic=b"GRSM"):
    rng = random.Random(seed)
    textures = [b"tex%d.bmp" % k for k in range(5)] + [b"\xb3\xaa\xb9\xab.bmp"]
    out = magic + bytes((major, minor)) + struct.pack("<ii", 120 + seed, seed % 3)
    if minor >= 4:
        out += bytes((200,))
    out += bytes(16) + struct.pack("<i", len(textures)) + b"".join(name40(t) for t in textures)
    out += name40(b"trunk")
    nodes = [node(minor, b"branch", b"trunk", [1, 2, 3], rng, keys=(2, 1)),
             node(minor, b"trunk", b"", [0, 5, 4, 2], rng, vertices=12, faces=60,
                  degenerate=degenerate, keys=(3, 2)),
             node(minor, b"leaf", b"branch", [3], rng, vertices=3, faces=1),
             node(minor, b"twig", b"trunk", [4, 4], rng, faces=17)]
    out += struct.pack("<i", len(nodes)) + b"".join(nodes)
    if minor < 5:
        out += struct.pack("<i", 2) + struct.pack("<i3f", 0, 1, 2, 3) + struct.pack("<i3f", 5, 4, 5, 6)
    out += struct.pack("<i", 3)
    for k in range(3):
        values = struct.pack("<9f", *(k + i * 0.5 for i in range(9)))
        if minor >= 3:
            out += values + struct.pack("<i", k % 2)
        elif minor == 2:
            out += values
    return out


def synthetic():
    for minor in range(1, 6):
        yield model(minor, minor)
        yield model(minor, 10 + minor, degenerate=True)
    yield model(6, 0)  # unsupported
    yield model(4, 0, major=2)  # unsupported
    yield model(4, 0, magic=b"GRSX")  # not a model


def init():
    emu = Emulator(max_instructions=2_000_000_000).init_crt()

    def get_texture(this, name, flag):
        texture = emu.alloc(0x130)
        emu.mem_write(texture, emu.mem_read(name, 256).split(b"\0")[0] + b"\0")
        return texture

    emu.replace_thiscall(TEXTURECACHE_GET_TEXTURE, 2, get_texture)
    return emu, emu.scratch(size=C3DMODELRES_SIZE)


def list_values(emu, head):
    node, values = emu.read_u32(head), []
    while node != head:
        values.append(emu.read_u32(node + 8))
        node = emu.read_u32(node)
    return values


def cstring(emu, at, limit=256):
    return emu.mem_read(at, limit).split(b"\0")[0]


def work(state, path):
    emu, obj = state
    with open(path, "rb") as f:
        data = f.read()
    mark = emu.heap_mark()
    emu.serve_data_files({"data\\t.rsm": data})
    emu.mem_write(obj, bytes(C3DMODELRES_SIZE))
    emu.call(C3DMODELRES_CTOR, this=obj)
    ok = emu.call(C3DMODELRES_LOAD, emu.cstr("t.rsm"), this=obj).eax & 0xFF
    if not ok:
        emu.heap_release(mark)
        return "failed"
    length, field_190 = struct.unpack("<ii", emu.mem_read(obj + 0x194, 4) + emu.mem_read(obj + 0x190, 4))
    alpha = emu.mem_read(obj + 0x1A4, 1)[0]
    roots = list_values(emu, emu.read_u32(obj + 0x128))  # list +0x124: node + 8 holds a string
    root_node = emu.read_u32(emu.read_u32(obj + 0x128))
    root = emu.mem_read(emu.read_u32(root_node + 0xC), emu.read_u32(root_node + 0x10)) if roots else b""
    nodes = list_values(emu, emu.read_u32(obj + 0x11C))
    index = {p: k for k, p in enumerate(nodes)}
    volumes = b""
    volume_list = list_values(emu, emu.read_u32(obj + 0x19C))
    for v in volume_list:
        volumes += emu.mem_read(v, 36) + emu.mem_read(v + 0xE4, 4)
    lines = [f"ok {length} {field_190} {alpha} {root.hex() or '-'} {len(nodes)} "
             f"{hashed(len(volume_list), volumes)}"]
    for k, p in enumerate(nodes):
        name = cstring(emu, p, 0x80)
        parent = index.get(emu.read_u32(p + 0x104), -1)
        children = ",".join(str(index[c]) for c in list_values(emu, emu.read_u32(p + 0x10C))) or "-"
        count = emu.read_u32(p + 0x118)
        array = emu.read_u32(p + 0x11C)
        names = b"".join(cstring(emu, emu.read_u32(array + 4 * t)) + b"\0" for t in range(count))
        fields = emu.mem_read(p + 0x120, 12) + emu.mem_read(p + 0x12C, 12) + \
            emu.mem_read(p + 0x138, 4) + emu.mem_read(p + 0x13C, 12) + emu.mem_read(p + 0x148, 1)
        mesh = emu.read_u32(p + 0x114)
        nv, nf, nt = struct.unpack("<iii", emu.mem_read(mesh, 12))
        vertices, normals, corners, tcs, faces = (emu.read_u32(mesh + o) for o in (0xC, 0x10, 0x14, 0x18, 0x1C))

        def keys(at, size):
            begin, end = emu.read_u32(p + at + 4), emu.read_u32(p + at + 8)
            n = (end - begin) // size if begin else 0
            return hashed(n, emu.mem_read(begin, n * size) if n else b"")

        def block(at, count, size):
            return hashed(count, emu.mem_read(at, count * size) if count else b"")

        lines.append(" ".join([
            "N", str(k), name.hex() or "-", str(parent), children, hashed(count, names),
            hashed(1, fields), block(vertices, nv, 12), block(tcs, nt, 12), block(faces, nf, 24),
            block(normals, nf, 12), block(corners, nf, 36), keys(0x14C, 16), keys(0x15C, 20)]))
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
        subprocess.run([args.ragres, "extract", args.archive, files_dir, "*.rsm"], check=True,
                       capture_output=True)
        files = sorted(os.path.join(d, n) for d, _, ns in os.walk(files_dir) for n in ns)
        for k, data in enumerate(synthetic()):
            path = os.path.join(tmp, f"synthetic{k}.rsm")
            with open(path, "wb") as f:
                f.write(data)
            files.append(path)
        out = subprocess.run([args.ragres, "decode-rsms", *files], capture_output=True,
                             text=True).stdout
        ours = {}
        current = None
        for line in out.splitlines():
            if line.startswith("N "):
                ours[current].append(line)
                continue
            n, rest = line.split(" ", 1)
            current = int(n)
            ours[current] = ["failed"] if rest.startswith("failed") else [rest]
        ours = {n: "\n".join(lines) for n, lines in ours.items()}
        originals = oracle_pool.run(files, init, work, args.jobs or None, label="rsm")
        mismatches = []
        for n, (p, want) in enumerate(zip(files, originals)):
            got = ours.get(n)
            if got != want:
                diff = [f"    port     {a}\n    original {b}" for a, b in
                        zip((got or "").split("\n"), want.split("\n")) if a != b]
                mismatches.append(f"{os.path.relpath(p, tmp)}:\n" + "\n".join(diff[:4]))
    real = sum(1 for p in files if not os.path.basename(p).startswith("synthetic"))
    print(f"{real} models and {len(files) - real} synthetic ones compared, {len(mismatches)} differ")
    for line in mismatches[:20]:
        print("MISMATCH", line)
    print("FAILED" if mismatches else "OK")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
