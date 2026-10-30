"""Compares the port's loaded worlds with the original's (docs/render/model.md#loading).

usage: .venv/bin/python -I tools/world_oracle.py [--sample N] [--jobs J] [--raw] [MAP...]

CWorld::LoadWorld (004c3ab0) runs in the emulation oracle on a map's files from the install
(data.grf and its patches, through `ragres extract`), the player in the centre of the
attribute map, then CWorld::UpdateGameObjects puts the late models in the scene graph. The
x87 runs at single precision, as the main thread does after FPUSETUP, and at double while
LazyModelLoadThread runs, as a new thread does; CreateThread answers 0, so LoadWorld runs
the lazy loader itself. Real: the resource manager (its types registered by their static
initializers), every loader, the ground's build, the models' placement and lighting, the
scene graph. Stood in for: TextureCache::GetTexture and texture creation, the effects' and
sounds' makers, the player, the UI's progress, Present, and Direct3D's lights.

Each map is dumped as `ragres load-world` dumps the port's: per model of CWorld+0x48 its
.rsw record and hashes of its fields and nodes, and per scene graph node its box, cells and
models. --sample takes the smallest maps and a spread of the rest; 0 takes every map.
"""
import argparse
import os
import re
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import oracle_pool  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_FPCW  # noqa: E402

from emu import Emulator  # noqa: E402
from emu.winapi import Stub  # noqa: E402
from model_vectors import (C3DDEVICE_CTOR, C3DDEVICE_INIT, G_C3DDEVICE, G_PSCENE_RENDERER,  # noqa: E402
                           IID_HAL_DEVICE, SCENE_RENDERER_CTOR, SCENE_RENDERER_SIZE, SET_SIZE, WINDOW)

# The resource types' static initializers, in the order they run (docs/formats/resources.md).
TYPE_INITIALIZERS = (0x4207A0, 0x421B10, 0x426A00, 0x4275F0, 0x427740, 0x427790, 0x428340, 0x428770,
                     0x428EA0, 0x4299A0, 0x42B600, 0x42DA00, 0x42E900, 0x431F70, 0x433EE0, 0x435300,
                     0x435810)
GET_RESOURCE_MANAGER, GET_RESOURCE = 0x4DFA10, 0x4DFF60
GET_TEXTURE, CREATE_TEXTURE = 0x40CC00, 0x405350
LOAD_WORLD, UPDATE_GAME_OBJECTS, CWORLD_CTOR = 0x4C3AB0, 0x4C4A10, 0x4C35C0
LAZY_LOADER, JOIN_LOADER = 0x4C33A0, 0x4C5200
SEND_MSG, GET_GAME_MODE, PLAYER_CELL = 0x51D990, 0x4CBF30, 0x686C90
STOOD_IN = ((0x40EF30, 3), (0x4B3970, 4), (0x4A8740, 0), (0x410C50, 1), (0x5B1050, 0), (0x4C3890, 0),
            (0x4C4E60, 0))
SINGLE, DOUBLE = 0x007F, 0x027F
RAW = "--raw" in sys.argv


def fnv1a64(values):
    h = 0xCBF29CE484222325
    for b in struct.pack(f"<{len(values)}I", *values):
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def archive_name(raw):
    """A CP949 name as `ragres extract` writes it: bytes above 0x7f as %XX."""
    return "".join(chr(c) if c < 0x80 else f"%{c:02X}" for c in raw)


def served(directory):
    """The files `ragres extract` wrote, keyed by their archive paths' bytes as Latin-1."""
    files = {}
    for root, _, names in os.walk(directory):
        for name in names:
            path = os.path.join(root, name)
            relative = os.path.relpath(path, directory).replace(os.sep, "\\")
            with open(path, "rb") as f:
                files[re.sub(r"%([0-9A-F]{2})", lambda m: chr(int(m.group(1), 16)), relative)] = f.read()
    return files


def extract(ragres, install, directory, names):
    """`ragres extract` of the archive names (bytes), from data.grf and the patches."""
    archives = [os.path.join(install, "data.grf")] + sorted(
        os.path.join(install, n) for n in os.listdir(install) if n.lower().endswith(".gpf"))
    for archive in archives:
        for name in names:
            subprocess.run([ragres.encode(), b"extract", archive.encode(), directory.encode(), name],
                           capture_output=True)


def original(item):
    ragres, install, mapname = item
    with tempfile.TemporaryDirectory() as tmp:
        base = mapname.encode()
        extract(ragres, install, tmp, [b"data\\" + base + ext for ext in (b".rsw", b".gnd", b".gat")])
        emu = Emulator(max_instructions=8_000_000_000).init_crt()
        emu.uc.reg_write(UC_X86_REG_FPCW, SINGLE)
        emu.call(C3DDEVICE_CTOR, this=G_C3DDEVICE)
        assert emu.call(C3DDEVICE_INIT, WINDOW, 0, IID_HAL_DEVICE, 0, 6, this=G_C3DDEVICE).eax == 0
        renderer = emu.scratch(size=SCENE_RENDERER_SIZE)
        emu.call(SCENE_RENDERER_CTOR, this=renderer)
        emu.write_u32(renderer + 0x78, emu.read_u32(G_C3DDEVICE + 0x3C))
        emu.call(SET_SIZE, 800, 600, this=renderer)
        emu.write_u32(G_PSCENE_RENDERER, renderer)
        for initializer in TYPE_INITIALIZERS:
            emu.call(initializer)
        emu.serve_data_files(served(tmp))
        manager = emu.call(GET_RESOURCE_MANAGER, emu.cstr("x")).eax
        world_res = emu.call(GET_RESOURCE, emu.cstr(f"{mapname}.rsw"), this=manager).eax
        if not world_res:
            return "failed"
        head = emu.read_u32(world_res + 0x114)
        node, records = emu.read_u32(head), []
        while node != head:
            records.append(emu.read_u32(node + 8))
            node = emu.read_u32(node)
        files = sorted({emu.mem_read(r + 0x28, 80).split(b"\0")[0] for r in records})
        extract(ragres, install, tmp, [b"data\\model\\" + f for f in files])
        emu.serve_data_files(served(tmp))
        return load(emu, manager, mapname, records)


def load(emu, manager, mapname, records):
    textures = {}

    def get_texture(this, name, flag):
        text = emu.mem_read(name, 256).split(b"\0")[0]
        if text not in textures:
            textures[text] = emu.alloc(0x130)
        return textures[text]

    emu.replace_thiscall(GET_TEXTURE, 2, get_texture)
    emu.replace_thiscall(CREATE_TEXTURE, 5, lambda this, w, h, pixels, fmt, flag: emu.alloc(0x130))
    for address, nargs in STOOD_IN:
        emu.replace_thiscall(address, nargs, lambda this, *args: 0)
    emu.api.stubs["KERNEL32.DLL!CreateThread"] = Stub(6, lambda *args: 0)
    emu.api.stubs["KERNEL32.DLL!SetThreadPriority"] = Stub(2, lambda *args: 1)
    # A new thread starts at the CRT's precision; the main thread is back at FPUSETUP's.
    emu.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, d: uc.reg_write(UC_X86_REG_FPCW, DOUBLE),
                    begin=LAZY_LOADER, end=LAZY_LOADER)
    emu.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, d: uc.reg_write(UC_X86_REG_FPCW, SINGLE),
                    begin=JOIN_LOADER, end=JOIN_LOADER)
    name = emu.cstr(f"{mapname}.rsw")
    emu.replace_thiscall(SEND_MSG, 4, lambda this, message, a, b, c: name)
    world = emu.alloc(0x100)
    emu.call(CWORLD_CTOR, this=world)
    mode = emu.scratch(size=0x400)
    emu.write_u32(mode, emu.scratch(struct.pack("<6I", 0, 0, 0, 0, 0, SEND_MSG)))
    emu.write_u32(world + 4, mode)
    emu.write_u32(mode + 0xB4, world)
    emu.replace_thiscall(GET_GAME_MODE, 0, lambda this: mode)
    attr = emu.call(GET_RESOURCE, emu.cstr(f"{mapname}.gat"), this=manager).eax
    cell = (emu.read_u32(attr + 0x110) // 2, emu.read_u32(attr + 0x114) // 2)
    emu.mem_write(PLAYER_CELL, struct.pack("<ii", *cell))
    emu.call(LOAD_WORLD, this=world)
    near = emu.read_u32(world + 0x54)
    emu.call(UPDATE_GAME_OBJECTS, this=world)
    return dump(emu, world, records, near), cell


def floats(emu, address, count):
    return list(struct.unpack(f"<{count}I", emu.mem_read(address, 4 * count))) if count else []


def dump(emu, world, records, near):
    u = emu.read_u32
    begin, end = u(world + 0x48), u(world + 0x4C)
    instances = [u(p) for p in range(begin, end, 4)]
    keys = [emu.mem_read(r, 40).split(b"\0")[0] + emu.mem_read(r + 0xC8, 36) for r in records]
    used = set()
    lines = [f"instances {len(instances)}", f"near {near}"]
    for k, m in enumerate(instances):
        key = emu.mem_read(m + 0x2C, 40).split(b"\0")[0] + emu.mem_read(m, 36)
        record = next(i for i, r in enumerate(keys) if r == key and i not in used)
        used.add(record)
        nodes = []

        def visit(node):
            faces = u(u(node + 0x9C) + 4)
            nodes.extend(floats(emu, node + 0xA0, 1) + floats(emu, node + 0xB4, 12) +
                         floats(emu, node + 0x128, 6) + floats(emu, u(node + 0x118), faces * 9) +
                         floats(emu, u(node + 0x144), faces * 9))
            head = u(node + 0xAC)
            child = u(head)
            while child != head:
                visit(u(child + 8))
                child = u(child)

        visit(u(m + 0x28))
        fields = floats(emu, m + 0xC0, 73)
        lines.append(f"I {k} {record} " + (" ".join(f"{v:08x}" for v in fields) if RAW else
                                            f"{fnv1a64(fields):016x} {fnv1a64(nodes):016x}"))
    graph = []

    def walk(n, depth):
        graph.append(n)
        if depth < 5:
            for k in range(4):
                walk(u(n + 4 + 4 * k), depth + 1)

    walk(world + 0x68, 0)
    lines.append(f"graph {len(graph)}")
    for k, n in enumerate(graph):
        cells = struct.unpack("<4i", emu.mem_read(n + 0x5C, 16))
        attr_cells = struct.unpack("<4i", emu.mem_read(n + 0x70, 16))
        members = ",".join(str(instances.index(u(p))) for p in range(u(n + 0x4C), u(n + 0x50), 4)) or "-"
        lines.append(f"G {k} {fnv1a64(floats(emu, n + 0x14, 12)):016x} {u(n + 0x44)} "
                     f"{' '.join(map(str, cells))} {' '.join(map(str, attr_cells))} {members}")
    return "\n".join(lines)


def init():
    pass


def work(_, item):
    return original(item)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("maps", nargs="*")
    ap.add_argument("--install", default=os.path.join(ROOT, "original", "install"))
    ap.add_argument("--ragres", default=os.path.join(ROOT, "build", "dev", "src", "ragres"))
    ap.add_argument("--sample", type=int, default=12, help="maps to compare (0: every map)")
    ap.add_argument("--jobs", type=int, default=0, help="worker processes (0: one per core but one)")
    ap.add_argument("--raw", action="store_true", help="each model's fields, not their hash")
    ap.add_argument("--show", type=int, default=0, help="differing lines to print for each map")
    args = ap.parse_args()
    maps = args.maps
    if not maps:
        listing = subprocess.run([args.ragres, "list", os.path.join(args.install, "data.grf")],
                                 capture_output=True, text=True).stdout
        sized = sorted((int(line.split("\t")[0]), line.split("\t")[4][5:-4]) for line in listing.splitlines()
                       if line.endswith(".rsw") and line.split("\t")[4].count("\\") == 1)
        names = [n for _, n in sized]
        if args.sample and args.sample < len(names):
            half = args.sample // 2
            step = max(1, (len(names) - half) // (args.sample - half))
            names = names[:half] + names[half::step][:args.sample - half]
        maps = names
    results = oracle_pool.run([(args.ragres, args.install, m) for m in maps], init, work, args.jobs or None,
                              label="worlds")
    mismatches = 0
    for mapname, result in zip(maps, results):
        if result == "failed":
            print(f"{mapname}: the original does not load it")
            continue
        want, cell = result
        got = subprocess.run([args.ragres, "load-world", *(["--raw"] if args.raw else []), args.install, mapname,
                              str(cell[0]), str(cell[1])], capture_output=True, text=True).stdout.strip()
        print(f"{mapname}: {summary(got, want)}")
        if got != want:
            mismatches += 1
            diff = [f"    port     {a}\n    original {b}" for a, b in zip(got.splitlines(), want.splitlines())
                    if a != b]
            print("\n".join(diff[:args.show]))
    print("FAILED" if mismatches else "OK")


def summary(got, want):
    """How many of the near models, the far models and the scene graph's nodes differ."""
    if got == want:
        return "equal (" + ", ".join(want.splitlines()[:2]) + ")"
    a, b = got.splitlines(), want.splitlines()
    if a[:2] != b[:2]:
        return f"port {a[:2]}, original {b[:2]}"
    near = int(b[1].split()[1])
    models = [(x, y) for x, y in zip(a, b) if y.startswith("I ")]
    records = sum(1 for x, y in models if x.split()[:3] != y.split()[:3])
    near_diff = sum(1 for k, (x, y) in enumerate(models) if k < near and x != y)
    far_diff = sum(1 for k, (x, y) in enumerate(models) if k >= near and x != y)
    graph = [(x, y) for x, y in zip(a, b) if y.startswith("G ")]
    return (f"records {records} differ; near {near_diff} of {near} differ; far {far_diff} of "
            f"{len(models) - near} differ; graph {sum(1 for x, y in graph if x != y)} of {len(graph)} differ")


if __name__ == "__main__":
    main()
