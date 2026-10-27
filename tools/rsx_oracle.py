"""Compares the port's animated-model loader with the original (docs/formats/rsx.md).

usage: .venv/bin/python -I tools/rsx_oracle.py [--jobs J]

Runs C3dAnimModelRes::Load (0042df00) in the emulation oracle on the .rsx of data.grf and
on synthetic ones of every version, with TextureCache::GetTexture replaced by a stand-in
that records the names asked for, and compares the header, the textures, the root name
and every node (names, links, textures, the vertices and texture coordinates of every
frame, the faces) with `ragres decode-rsxs`.
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

C3DANIMMODELRES_CTOR, C3DANIMMODELRES_LOAD = 0x42DBB0, 0x42DF00  # analysis/labels
C3DANIMMODELRES_SIZE = 0x164
TEXTURECACHE_GET_TEXTURE = 0x40CC00
MARKER = 0x12345678


def fnv1a64(data, h=0xCBF29CE484222325):
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def hashed(count, data):
    return f"{count}:{fnv1a64(data):016x}"


def name40(s):
    return s.ljust(40, b"\0")


def anim_model(major, minor, frames, shared, seed, end_marker=MARKER):
    rng = random.Random(seed)
    out = b"GRSX" + bytes((major, minor))
    if major == 1 and minor > 1:
        out += struct.pack("<f3f", 0.5, 1.0, 2.0, 3.0)
        if minor == 3:
            out += struct.pack("<i", 200)
    out += struct.pack("<iii", 1, frames, 160) + bytes(16)
    textures = [b"gate01.bmp", b"gate02.bmp", b"\xb9\xae.bmp"]
    out += struct.pack("<i", len(textures)) + b"".join(name40(t) for t in textures)
    out += name40(b"base")
    nodes = [(b"base", b""), (b"arm", b"base"), (b"hand", b"arm"), (b"flag", b"base")]
    out += struct.pack("<i", len(nodes))
    for k, (name, parent) in enumerate(nodes):
        out += struct.pack("<I", MARKER) + name40(name) + name40(parent)
        ids = [rng.randrange(len(textures)) for _ in range(k % 3 + 1)]
        out += struct.pack(f"<i{len(ids)}i", len(ids), *ids)
        vertices = 3 + k
        out += struct.pack("<i", vertices)
        out += b"".join(struct.pack("<3f", *(rng.uniform(-9, 9) for _ in range(3)))
                        for _ in range(frames * vertices))
        tcs = 2 + k
        node_shared = shared if k != 1 else 1 - shared
        out += struct.pack("<ii", tcs, node_shared)
        out += b"".join(struct.pack("<I2f", rng.choice((0, 1, 0xFFFFFFFF)), rng.random(), rng.random())
                        for _ in range(tcs * (1 if node_shared else frames)))
        faces = k + 1
        out += struct.pack("<i", faces)
        out += b"".join(struct.pack("<7HHii", *(rng.randrange(3) for _ in range(6)), 0, 0, k, 1)
                        for _ in range(faces))
        out += struct.pack("<I", end_marker if k == len(nodes) - 1 else MARKER)
    return out


def synthetic():
    for minor in range(4):
        yield anim_model(1, minor, 2, minor % 2, minor)
    yield anim_model(2, 3, 3, 0, 9)  # the 1.x header fields are not read
    yield anim_model(1, 3, 1, 1, 10)
    yield anim_model(1, 3, 0, 0, 11)
    yield anim_model(1, 3, 2, 0, 12, end_marker=0x12345677)  # "data corrupt"


def init():
    emu = Emulator(max_instructions=2_000_000_000).init_crt()
    asked = []

    def get_texture(this, name, flag):
        texture = emu.alloc(0x130)
        text = emu.mem_read(name, 256).split(b"\0")[0]
        asked.append(text)
        emu.mem_write(texture, text + b"\0")
        return texture

    emu.replace_thiscall(TEXTURECACHE_GET_TEXTURE, 2, get_texture)
    return emu, emu.scratch(size=C3DANIMMODELRES_SIZE), asked


def list_values(emu, head):
    node, values = emu.read_u32(head), []
    while node != head:
        values.append(emu.read_u32(node + 8))
        node = emu.read_u32(node)
    return values


def string(emu, at):
    buffer, length = emu.read_u32(at + 4), emu.read_u32(at + 8)
    return emu.mem_read(buffer, length) if buffer and length else b""


def work(state, path):
    emu, obj, asked = state
    with open(path, "rb") as f:
        data = f.read()
    mark = emu.heap_mark()
    asked.clear()
    emu.serve_data_files({"data\\t.rsx": data})
    emu.mem_write(obj, bytes(C3DANIMMODELRES_SIZE))
    emu.call(C3DANIMMODELRES_CTOR, this=obj)
    ok = emu.call(C3DANIMMODELRES_LOAD, emu.cstr("t.rsx"), this=obj).eax & 0xFF
    if not ok:
        emu.heap_release(mark)
        return "failed"
    f140, frames, f148, f14c, a, b, c = struct.unpack("<iiiIIII", emu.mem_read(obj + 0x140, 12) +
                                                     emu.mem_read(obj + 0x14C, 4) +
                                                     emu.mem_read(obj + 0x154, 12))
    f160 = struct.unpack("<i", emu.mem_read(obj + 0x160, 4))[0]
    roots = list_values(emu, emu.read_u32(obj + 0x128))
    root_node = emu.read_u32(emu.read_u32(obj + 0x128))
    root = string(emu, root_node + 8) if roots else b""
    nodes = list_values(emu, emu.read_u32(obj + 0x11C))
    index = {p: k for k, p in enumerate(nodes)}
    node_textures = sum(emu.read_u32(p + 0x40) for p in nodes)
    preload = asked[:len(asked) - node_textures]  # each name once, before the nodes
    all_names = b"".join(n + b"\0" for n in preload)
    lines = [f"ok {f140} {frames} {f148} {f14c:08x} {a:08x} {b:08x} {c:08x} {f160} "
             f"{hashed(len(preload), all_names)} {root.hex() or '-'} {len(nodes)}"]
    for k, p in enumerate(nodes):
        name = string(emu, p)
        parent = index.get(emu.read_u32(p + 0x24), -1)
        children = ",".join(str(index[q]) for q in list_values(emu, emu.read_u32(p + 0x2C))) or "-"
        nv, nt, nf, ntex = struct.unpack("<iiii", emu.mem_read(p + 0x34, 16))
        entries, faces, textures = (emu.read_u32(p + o) for o in (0x48, 0x4C, 0x50))
        shared = struct.unpack("<i", emu.mem_read(p + 0x54, 4))[0]
        names = b"".join(emu.mem_read(emu.read_u32(textures + 4 * t), 256).split(b"\0")[0] + b"\0"
                         for t in range(ntex))
        vertices = b"".join(emu.mem_read(emu.read_u32(entries + 8 * f), nv * 12)
                            for f in range(frames) if nv)
        sets = 1 if shared else frames
        tcs = b"".join(emu.mem_read(emu.read_u32(entries + 8 * f + 4), nt * 12)
                       for f in range(sets) if nt)
        lines.append(" ".join([
            "N", str(k), name.hex() or "-", str(parent), children, hashed(ntex, names), str(nv),
            hashed(frames * nv, vertices), str(nt), str(shared), hashed(sets * nt, tcs),
            hashed(nf, emu.mem_read(faces, nf * 24) if nf else b"")]))
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
        subprocess.run([args.ragres, "extract", args.archive, files_dir, "*.rsx"], check=True,
                       capture_output=True)
        files = sorted(os.path.join(d, n) for d, _, ns in os.walk(files_dir) for n in ns)
        for k, data in enumerate(synthetic()):
            path = os.path.join(tmp, f"synthetic{k}.rsx")
            with open(path, "wb") as f:
                f.write(data)
            files.append(path)
        out = subprocess.run([args.ragres, "decode-rsxs", *files], capture_output=True,
                             text=True).stdout
        ours, current = {}, None
        for line in out.splitlines():
            if line.startswith("N "):
                ours[current].append(line)
                continue
            n, rest = line.split(" ", 1)
            current = int(n)
            ours[current] = ["failed"] if rest.startswith("failed") else [rest]
        ours = {n: "\n".join(lines) for n, lines in ours.items()}
        originals = oracle_pool.run(files, init, work, args.jobs or None, label="rsx")
        mismatches = []
        for n, (p, want) in enumerate(zip(files, originals)):
            got = ours.get(n)
            if got != want:
                diff = [f"    port     {a}\n    original {b}" for a, b in
                        zip((got or "").split("\n"), want.split("\n")) if a != b]
                mismatches.append(f"{os.path.relpath(p, tmp)}:\n" + "\n".join(diff[:4]))
    real = sum(1 for p in files if not os.path.basename(p).startswith("synthetic"))
    print(f"{real} animated models and {len(files) - real} synthetic ones compared, "
          f"{len(mismatches)} differ")
    for line in mismatches[:20]:
        print("MISMATCH", line)
    print("FAILED" if mismatches else "OK")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
