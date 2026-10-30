"""Test vectors for the order a world's models are loaded in (docs/render/model.md#loading),
taken from the original in the emulation oracle.

usage: .venv/bin/python -I tools/world_vectors.py > tests/unit/world_vectors.inc

CWorld::LoadWorld (004c3ab0) runs on synthetic worlds: models scattered around the player's
cell (00686c90, 00686c94), some at exactly 200 from its centre, some at equal distances, some
with no .rsm or no node, on attribute maps of assorted sizes. CreateThread answers 0, so
LoadWorld runs LazyModelLoadThread (004c33a0) itself, as it does when no thread starts: the
far models' sort (004c7410) and CWorld::AddLoadedModel run as the original's, and so does
CGameMode::method_00511de0, the cell's centre. Stood in for:
  - the resources (GetResource answering the world, its .gat and .gnd, and each .rsm or
    none), C3dModelRes::FindNode, and the map name (CGameMode::SendMsg 9);
  - what places, lights and draws (the ModelInstance constructor, CopyVolumes, Setup and
    Light, recorded; the ground's build and slot 10, SetWaterParams, BuildSceneGraph,
    CreateEffectAndSoundMakers, CreatePlayer), the UI's progress, Present and PumpMessages.
Recorded: the records the instances at CWorld+0x48 come from, in order, and the cell's centre.
The x87 runs at single precision, as DirectDraw's FPUSETUP leaves it in the client.
"""
import math
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from unicorn.x86_const import UC_X86_REG_FPCW  # noqa: E402

from emu import Emulator  # noqa: E402
from emu.winapi import Stub  # noqa: E402

LOAD_WORLD, CELL_CENTRE = 0x4C3AB0, 0x511DE0
GET_RESOURCE_MANAGER, GET_RESOURCE, UNLOAD_RESOURCE, FIND_NODE = 0x4DFA10, 0x4DFF60, 0x4E0280, 0x42CBD0
SEND_MSG, GET_GAME_MODE = 0x51D990, 0x4CBF30
INSTANCE_CTOR, COPY_VOLUMES, SETUP, LIGHT = 0x4B9340, 0x4B9550, 0x4B9820, 0x4BA270
GROUND_CTOR, GROUND_BUILD, GROUND_VF10, SET_WATER = 0x4BC220, 0x4BCC30, 0x4BFDB0, 0x4BC410
BUILD_SCENE_GRAPH, MAKERS, CREATE_PLAYER = 0x4C4F10, 0x4C3890, 0x4C4E60
UI_MESSAGE, UI_DRAW, PRESENT, PUMP, SET_LIGHT, DEBUG_LOG = 0x4B3970, 0x4A8740, 0x410C50, 0x5B1050, 0x40EF30, 0x4060F0
G_PSCENE_RENDERER, PLAYER_CELL = 0x625E98, 0x686C90


def bits(f):
    return struct.unpack("<I", struct.pack("<f", f))[0]


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def hexs(values):
    return ", ".join(f"0x{v:08x}" for v in values)


class Oracle:
    def __init__(self):
        emu = self.emu = Emulator().init_crt()
        emu.uc.reg_write(UC_X86_REG_FPCW, 0x007F)
        emu.write_u32(G_PSCENE_RENDERER, emu.scratch(size=0x300))
        self.resources = {}
        self.no_node = set()
        self.setups = []
        emu.replace_thiscall(GET_RESOURCE_MANAGER, 0, lambda this: 0x1234)
        emu.replace_thiscall(GET_RESOURCE, 1, lambda this, name: self.resources.get(
            emu.read_cstr(name, 128, encoding="latin-1"), 0))
        emu.replace_thiscall(UNLOAD_RESOURCE, 1, lambda this, res: 0)
        emu.replace_thiscall(FIND_NODE, 1, lambda this, name: 0 if this in self.no_node else 1)
        emu.replace_thiscall(SEND_MSG, 4, lambda this, message, a, b, c: self.map_name)
        self.mode = emu.scratch(size=0x400)
        emu.replace_thiscall(GET_GAME_MODE, 0, lambda this: self.mode)
        emu.replace_thiscall(INSTANCE_CTOR, 0, lambda this: this)
        emu.replace_thiscall(COPY_VOLUMES, 1, lambda this, volumes: 0)
        emu.replace_thiscall(SETUP, 1, lambda this, res: self.setups.append(this) or 0)
        emu.replace_thiscall(LIGHT, 3, lambda this, d, f, a: 0)
        emu.replace_thiscall(GROUND_CTOR, 0, lambda this: this)
        emu.replace_thiscall(GROUND_BUILD, 4, lambda this, res, d, f, a: 0)
        emu.replace_thiscall(GROUND_VF10, 0, lambda this: 0)
        emu.replace_thiscall(SET_WATER, 6, lambda this, *args: 0)
        emu.replace_thiscall(BUILD_SCENE_GRAPH, 0, lambda this: 0)
        emu.replace_thiscall(MAKERS, 0, lambda this: 0)
        emu.replace_thiscall(CREATE_PLAYER, 0, lambda this: 0)
        emu.replace_thiscall(UI_MESSAGE, 4, lambda this, *args: 0)
        emu.replace_thiscall(UI_DRAW, 0, lambda this: 0)
        emu.replace_thiscall(PRESENT, 1, lambda this, wait: 0)
        emu.replace_thiscall(PUMP, 0, lambda this: 0)
        emu.replace_thiscall(SET_LIGHT, 3, lambda this, d, f, a: 0)
        emu.replace_thiscall(DEBUG_LOG, 0, lambda this: 0)
        emu.api.stubs["KERNEL32.DLL!CreateThread"] = Stub(6, lambda *args: 0)
        emu.api.stubs["KERNEL32.DLL!SetThreadPriority"] = Stub(2, lambda thread, priority: 1)
        self.map_name = emu.cstr("w.rsw")

    def string(self, address, text):
        """An MSVC 6 std::string at address: its buffer (+4) and length (+8)."""
        emu = self.emu
        buffer = emu.scratch(b"\0" + text.encode("latin-1") + b"\0") + 1
        emu.mem_write(address + 4, struct.pack("<II", buffer, len(text)))

    def load(self, models, width, height, cell):
        """LoadWorld on a world of models (name, file, position, has .rsm, has node); the
        records the instances come from, in order, and the cell's centre."""
        emu = self.emu
        self.resources = {}
        self.no_node = set()
        self.setups = []
        world = emu.scratch(size=0x1C0)
        self.string(world + 0x148, "w.gat")
        self.string(world + 0x138, "w.gnd")
        self.resources["w.rsw"] = world
        attr = emu.scratch(size=0x130)
        # LoadWorld sets the cell size (+0x118) to 5 itself.
        emu.mem_write(attr + 0x110, struct.pack("<3i", width, height, 7))
        self.resources["w.gat"] = attr
        gnd = emu.scratch(size=0x130)
        emu.write_u32(gnd + 0x110, 1)
        self.resources["w.gnd"] = gnd
        records = []
        for k, (name, file, position, has_file, has_node) in enumerate(models):
            record = emu.scratch(size=0x100)
            emu.mem_write(record, name.encode() + b"\0")
            emu.mem_write(record + 0x28, file.encode() + b"\0")
            emu.mem_write(record + 0xC8, struct.pack("<3f", *position))
            emu.mem_write(record + 0xE0, struct.pack("<3f", 1.0, 1.0, 1.0))
            emu.mem_write(record + 0xF4, struct.pack("<f", 1.0))
            records.append(record)
            if has_file and file not in self.resources:
                res = emu.scratch(size=0x200)
                # FindNode failing leaves the node list's first entry to decide: 0 here.
                head = emu.scratch(size=0x10)
                emu.write_u32(head, emu.scratch(size=0x10))
                emu.write_u32(res + 0x11C, head)
                self.resources[file] = res
                if not has_node:
                    self.no_node.add(res)
        # The std::list of records at +0x114: a head node, then one node each.
        head = emu.scratch(size=0xC)
        nodes = [emu.scratch(size=0xC) for _ in records]
        ring = [head] + nodes
        for k, node in enumerate(ring):
            emu.mem_write(node, struct.pack("<II", ring[(k + 1) % len(ring)], ring[k - 1]))
        for node, record in zip(nodes, records):
            emu.write_u32(node + 8, record)
        emu.write_u32(world + 0x114, head)
        emu.write_u32(world + 0x118, len(records))
        cworld = emu.scratch(size=0x100)
        mode_vtable = emu.scratch(struct.pack("<6I", 0, 0, 0, 0, 0, SEND_MSG))
        game = emu.scratch(size=0x10)
        emu.write_u32(game, mode_vtable)
        emu.write_u32(cworld + 4, game)
        emu.write_u32(self.mode + 0xB4, cworld)
        emu.mem_write(PLAYER_CELL, struct.pack("<ii", *cell))
        emu.call(LOAD_WORLD, this=cworld)
        begin, end = emu.read_u32(cworld + 0x48), emu.read_u32(cworld + 0x4C)
        instances = struct.unpack(f"<{(end - begin) // 4}I", emu.mem_read(begin, end - begin))
        names = [emu.read_cstr(i + 0x2C, 40) for i in instances]
        order = [int(n[1:]) for n in names]
        assert sorted(instances) == sorted(self.setups)
        out = emu.scratch(size=8)
        emu.call(CELL_CENTRE, cell[0], cell[1], out, out + 4, this=self.mode)
        return order, struct.unpack("<2I", emu.mem_read(out, 8))


def straddler(rng, cx, cz):
    """A point about 200 away whose distance, summed in the original's order ((dx² + dy²)
    + dz², each step in single precision), is at most 200, but summed as dx² + (dy² + dz²)
    is not: it tells the order apart."""
    while True:
        dx, dy, dz = (f32(rng.uniform(-200, 200)) for _ in range(3))
        scale = 200.0 / math.sqrt(dx * dx + dy * dy + dz * dz)
        p = (f32(cx + dx * scale), f32(dy * scale), f32(cz + dz * scale))
        ex, ey, ez = f32(p[0] - cx), p[1], f32(p[2] - cz)
        sx, sy, sz = f32(ex * ex), f32(ey * ey), f32(ez * ez)
        original = f32(math.sqrt(f32(f32(sx + sy) + sz)))
        other = f32(math.sqrt(f32(sx + f32(sy + sz))))
        if (original <= 200.0) != (other <= 200.0):
            return p


def main():
    rng = random.Random(71)
    o = Oracle()
    lines = ["// Generated by tools/world_vectors.py from the original in the emulation oracle."]
    cases = []
    for n in range(150):
        width, height = rng.choice([(40, 40), (200, 200), (300, 260), (11, 13), (rng.randrange(1, 500),
                                                                                  rng.randrange(1, 500))])
        cell = (rng.randrange(-5, width + 5), rng.randrange(-5, height + 5))
        if n % 15 == 4:
            # Past 2**24, where rounding the cell to a float first would differ.
            cell = (rng.randrange(1 << 24, 1 << 25) | 1, rng.randrange(1 << 24, 1 << 25) | 1)
        cx = (cell[0] - width * 0.5) * 5 + 2.5
        cz = (cell[1] - height * 0.5) * 5 + 2.5
        models = []
        for k in range(rng.randrange(1, 40)):
            r = rng.random()
            if r < 0.1:
                # Exactly 200 away along x or z, at height 0.
                position = rng.choice([(cx + 200.0, 0.0, cz), (cx, 0.0, cz - 200.0), (cx - 200.0, 0.0, cz)])
            elif r < 0.25 and models:
                # The same point as an earlier model: an equal distance.
                position = models[rng.randrange(len(models))][2]
            elif r < 0.3:
                position = (f32(cx + 120.0), f32(-160.0), f32(cz))
            elif r < 0.33:
                # A NaN distance, which counts as near.
                position = (math.nan, 0.0, cz)
            elif r < 0.38:
                # A float step either side of 200 along x.
                step = math.ulp(f32(200.0)) * rng.choice([-1, 1]) * 2
                position = (f32(cx) + f32(200.0) + step, 0.0, f32(cz))
            elif r < 0.44:
                position = straddler(rng, f32(cx), f32(cz))
            else:
                spread = rng.choice([150, 400, 2000])
                position = (cx + rng.uniform(-spread, spread), rng.uniform(-60, 20), cz + rng.uniform(-spread, spread))
            position = tuple(f32(p) for p in position)
            has_file = rng.random() > 0.08
            has_node = rng.random() > 0.08
            models.append((f"m{k}", f"f{rng.randrange(8)}.rsm", position, has_file, has_node))
        # A file's resource is made once, by the first model naming it.
        firsts = {}
        for m in models:
            firsts.setdefault(m[1], m)
        models = [(nm, f, p, firsts[f][3], firsts[f][4]) for nm, f, p, _, _ in models]
        order, centre = o.load(models, width, height, cell)
        name = f"kWorld{n}"
        lines.append(f"static const ModelVector {name}Models[] = {{")
        for nm, f, p, has_file, has_node in models:
            lines.append(f"    {{{{{hexs(map(bits, p))}}}, {int(has_file and has_node)}}},")
        lines.append("};")
        lines.append(f"static const int {name}Order[] = {{{', '.join(map(str, order)) or '-1'}}};")
        cases.append(f"    {{{width}, {height}, {{{cell[0]}, {cell[1]}}}, {{{hexs(centre)}}}, {name}Models, "
                     f"{len(models)}, {name}Order, {len(order)}}},")
    lines.append("// {the attribute map's width and height, the player's cell, its centre's x and z; the")
    lines.append("// models {position, whether it loads}; the records the instances come from, in order}. The")
    lines.append("// cell size LoadWorld sets is 5.")
    lines.append("static const WorldVector kWorlds[] = {")
    lines += cases
    lines.append("};")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
