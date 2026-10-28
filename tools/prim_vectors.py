"""Test vectors for the port's effect table and screen-space primitives
(docs/render/effect-prim.md), taken from the original in the emulation oracle.

usage: .venv/bin/python -I tools/prim_vectors.py > tests/unit/prim_vectors.inc

- The table: CRagEffect::InitEffect (0054eee0) on a constructed CRagEffect for every effect
  id the switch at 0054d4e1 knows, both ways of g_ClientState+0x1840 (00687eb0) and a
  seeded CRT rand (srand 005c2546): the .str name it hands LoadStrFile (0054fae0, recorded
  in place) and the duration it leaves at +0xc0.
- Synthetic primitives: CEffectPrim::CEffectPrim (0057cfa0) and InitPrimitive (00586380)
  for ids 0, 1 and 2, every field those cases read set at random (flags, speeds, keys,
  colours, limits), then each frame vf01 (0057dce0) and vf03 (00586070) under a moving
  owner, until vf01 answers false.
- The hit spark, effect id 1: InitEffect, then each frame CRagEffect::vf01 (0054d4c0) and
  vf01 of every object in the world's list (CWorld::UpdateGameObjects, 004c4a10, in list
  order, so the primitives it adds run the same frame), then vf03 of each; an effect that
  ends tells its primitives (vf02 message 0x18, as ~CRagEffect 0054d370 does).

AddPrimitive (00412550), TextureCache::GetTexture (0040cc00) and PlayWave (004352a0) are
recorded in place. The x87 runs at single precision, as DirectDraw's FPUSETUP leaves it.
"""
import math
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from actor_vectors import (ADD_SPRITE_QUAD, GET_FRAME_COUNT, GET_GAME_MODE, SCALES,  # noqa: E402
                           Oracle, bits, f32)

RAG_EFFECT_CTOR, INIT_EFFECT, RAG_EFFECT_VF01 = 0x54D220, 0x54EEE0, 0x54D4C0
LOAD_STR_FILE = 0x54FAE0
PRIM_CTOR, INIT_PRIMITIVE, PRIM_VF01, PRIM_VF02, PRIM_VF03 = 0x57CFA0, 0x586380, 0x57DCE0, 0x586050, 0x586070
ADD_PRIMITIVE, GET_TEXTURE, PLAY_WAVE = 0x412550, 0x40CC00, 0x4352A0
SET_SPRITE, SET_ACT, JOB_NAMES = 0x5018A0, 0x501910, 0x686DC8
RESET_HISTORY = 0x57D4B0
SLOPE_DOWN = SCALES["slope_down"]
SRAND = 0x5C2546
MINIMAL_EFFECTS = 0x687EB0
RAG_EFFECT_SIZE, PRIM_SIZE = 0x17BE8, 0x15990
EFFECT_TYPES = 0x152
TEXTURE_BASE = 0x7000
SENTINEL = 0x38D1B717  # 0.0001, "no change"

# The fields a primitive of ids 0 to 2 keeps, recorded after each frame.
INT_FIELDS = [0x28, 0x124, 0x134, 0x138, 0x13C, 0x140, 0x19C, 0x1A0, 0x1A4, 0xC0]
FLOAT_FIELDS = [0x4, 0x8, 0x1EC, 0x1F0, 0x204, 0x23C, 0x244, 0x254, 0x258, 0x260, 0x268,
                0x278, 0x27C, 0x284, 0x288, 0x290, 0x294, 0x2EC, 0x2F0, 0x2F8, 0x2FC]
STATE = INT_FIELDS + FLOAT_FIELDS
# Those of ids 5, 6 and 0xd, then the matrix +0xc4 and the history records (0x8c each).
STATE3 = [0x4, 0x8, 0xC, 0x28, 0x124, 0x134, 0x13C, 0x140, 0x1EC, 0x21C, 0x224, 0x228, 0x22C,
          0x230, 0x23C, 0x240, 0x244, 0x248, 0x24C, 0x254, 0x258, 0x260, 0x264, 0x290, 0x294,
          0x2E0, 0x2E4, 0x2E8, 0x2EC, 0x2F0, 0x2F4, 0x2F8, 0x2FC, 0x300, 0x310, 0x318, 0xC0]
# The particle stand-in: images {width, height, halved x, halved y}, 8-bit then 32-bit, and
# two actions of frames of items {x, y, image, flags, r, g, b, a, scale x, scale y, angle, type}.
IMAGES8 = [(12, 9, 0, 0), (7, 15, 1, 0), (20, 6, 0, 1)]
IMAGES32 = [(5, 5, 0, 0), (9, 4, 1, 1)]
ACTIONS = [
    [[(-6, -5, 0, 0, 255, 255, 255, 255, 1.0, 1.0, 0, 0)],
     [(-4, -9, 1, 1, 200, 100, 50, 180, 1.5, 0.75, 30, 0), (3, 2, 0, 0, 255, 0, 255, 255, 1.0, 1.0, 0, 1)],
     [(-10, -3, 2, 0, 255, 255, 255, 128, 0.5, 2.0, -45, 0)]],
    [[(-2, -2, 1, 1, 10, 20, 30, 40, 1.25, 1.25, 90, 1)],
     [(-5, -5, 0, 0, 255, 255, 255, 255, 2.0, 2.0, 0, 0)]],
]


class PrimOracle(Oracle):
    def __init__(self):
        super().__init__()
        emu = self.emu
        self.mode = emu.call(GET_GAME_MODE).eax
        emu.write_u32(self.mode + 0xB8, self.view)
        self.world = emu.read_u32(self.mode + 0xB4)
        player = emu.scratch(size=0x100)
        emu.write_u32(self.world + 0x3C, player)
        self.added, self.loaded, self.textures = [], [], {}

        def add(this, prim, flags):
            vertices = emu.read_u32(prim)
            self.added.append((list(struct.unpack("<24I", emu.mem_read(vertices, 0x60))),
                               emu.read_u32(prim + 0xC), emu.read_u32(prim + 0x14),
                               emu.read_u32(prim + 0x18), emu.read_u32(prim + 0x1C), flags))
            return 0

        def texture(this, name, flag):
            return self.textures.setdefault(emu.read_cstr(name, 64, encoding="latin-1"),
                                            TEXTURE_BASE + len(self.textures))

        emu.replace_thiscall(ADD_PRIMITIVE, 2, add)
        emu.replace_thiscall(GET_TEXTURE, 2, texture)
        emu.replace_thiscall(PLAY_WAVE, 0, lambda this: 0)
        # CRenderObject's constructor names an NPC sprite from the job names and loads it.
        names = emu.scratch(size=0x400)
        for i in range(0x100):
            emu.write_u32(names + 4 * i, emu.scratch(b"x\0"))
        emu.write_u32(JOB_NAMES, names)
        self.spr = self.sprite(IMAGES8, IMAGES32)
        self.act_res = self.act(ACTIONS)
        self.records = {}
        for t, images in ((0, IMAGES8), (1, IMAGES32)):
            table = emu.read_u32(self.spr + 0x514 + 0x10 * t)
            for i in range(len(images)):
                self.records[emu.read_u32(table + 4 * i)] = (t, i)
        emu.replace_thiscall(SET_SPRITE, 1, lambda this, name: emu.write_u32(this + 0x98, self.spr) or 0)
        emu.replace_thiscall(SET_ACT, 1, lambda this, name: emu.write_u32(this + 0x9C, self.act_res) or 0)
        emu.replace_thiscall(GET_FRAME_COUNT, 1, lambda this, a: len(ACTIONS[a]) if len(ACTIONS[a]) >= 2 else 1)
        self.quads = []

        def quad(this, desc, palette, flags):
            d = list(struct.unpack("<12I", emu.mem_read(desc, 48)))
            self.quads.append((d[:8], *self.records[d[11]], flags))
            return 0

        emu.replace_thiscall(ADD_SPRITE_QUAD, 3, quad)
        emu.replace_thiscall(LOAD_STR_FILE, 1, lambda this, name: self.loaded.append(emu.read_cstr(name, 64, encoding="latin-1")) or 1)

    def reset_world(self):
        emu = self.emu
        head = emu.alloc(0xC)
        emu.write_u32(head, head)
        emu.write_u32(head + 4, head)
        emu.write_u32(self.world + 0xC, head)
        emu.write_u32(self.world + 0x10, 0)
        return head

    def world_objects(self):
        emu = self.emu
        head = emu.read_u32(self.world + 0xC)
        node, out = emu.read_u32(head), []
        while node != head:
            out.append((node, emu.read_u32(node + 8)))
            node = emu.read_u32(node)
        return out

    def unlink(self, node):
        emu = self.emu
        nxt, prev = emu.read_u32(node), emu.read_u32(node + 4)
        emu.write_u32(prev, nxt)
        emu.write_u32(nxt + 4, prev)

    def rag_effect(self, actor):
        effect = self.emu.alloc(RAG_EFFECT_SIZE)
        self.emu.call(RAG_EFFECT_CTOR, this=effect)
        self.emu.write_u32(effect + 0xA0, actor)
        return effect

    def state(self, prim):
        emu = self.emu
        return [emu.read_u32(prim + f) for f in STATE]

    def draw(self, prim):
        self.added.clear()
        self.quads.clear()
        self.emu.call(PRIM_VF03, self.view + 0x94, this=prim)
        return list(self.added)

    def state3(self, prim):
        emu = self.emu
        n = emu.read_u32(prim + 0x154)
        return ([emu.read_u32(prim + f) for f in STATE3] + list(struct.unpack("<12I", emu.mem_read(prim + 0xC4, 48))) +
                list(struct.unpack(f"<{35 * n}I", emu.mem_read(prim + 0x358, 0x8C * n))))

    def scene(self, rng):
        at = (rng.uniform(-30, 30), rng.uniform(-10, 0), rng.uniform(-30, 30))
        self.build_view(f32(rng.choice([-45.0, -35.0, -55.0])), f32(rng.uniform(0, 360)),
                        f32(rng.choice([300.0, 250.0, 150.0])), at)
        emu = self.emu
        return (list(struct.unpack("<12I", emu.mem_read(self.view + 0x94, 48))),
                emu.read_u32(self.view + 0x20), emu.read_u32(SLOPE_DOWN), emu.read_u32(self.view + 0x1C))


def hexs(values):
    """Words as C initialisers, unpadded: most of the records are zeros."""
    return ", ".join("0" if v == 0 else f"0x{v & 0xFFFFFFFF:x}" for v in values)


def c_literal(name):
    """A name read as latin-1 (its CP949 bytes) as a C string's contents, octal escapes past
    ASCII and for quotes and backslashes."""
    return "".join(f"\\{c:03o}" if c >= 0x80 or c in (0x22, 0x5C) else chr(c) for c in name.encode("latin-1"))


def quads_cpp(quads):
    return "{" + ", ".join(f"{{{{{hexs(d)}}}, {t}, {i}, 0x{fl:x}}}" for d, t, i, fl in quads) + "}" if quads else "{{}}"


def scene_cpp(view, yaw, slope, pitch):
    return f"{{{{{hexs(view)}}}, 0x{yaw:08x}, 0x{slope:08x}, 0x{pitch:08x}}}"


def triangles_cpp(added):
    return "{" + ", ".join(f"{{{{{hexs(v)}}}, 0x{t:x}, {sb}, {db}, {md}, 0x{fl:x}}}"
                           for v, t, sb, db, md, fl in added) + "}" if added else "{{}}"


def table(o, lines):
    emu = o.emu
    actor = emu.scratch(size=0x100)
    rows = []
    for t in range(EFFECT_TYPES):
        for minimal in (0, 1):
            seed = t * 2 + minimal + 1
            emu.write_u32(MINIMAL_EFFECTS, minimal)
            emu.call(SRAND, seed)
            mark = emu.heap_mark()
            effect = o.rag_effect(actor)
            o.loaded.clear()
            emu.call(INIT_EFFECT, actor, t, 0, 0, 0, this=effect)
            name = o.loaded[0] if o.loaded else ""
            assert len(o.loaded) <= 1
            rows.append(f'    {{{t}, {minimal}, {seed}, {emu.read_u32(effect + 0xC0)}, "{c_literal(name)}"}},')
            emu.heap_release(mark)
    emu.write_u32(MINIMAL_EFFECTS, 0)
    lines.append("static const TableVector kTable[] = {")
    lines += rows
    lines.append("};")


def random_prim(rng, o, owner, pid):
    emu = o.emu
    prim = emu.alloc(PRIM_SIZE)
    emu.call(PRIM_CTOR, this=prim)
    offset = [f32(rng.uniform(-30, 30)) for _ in range(3)]
    emu.call(INIT_PRIMITIVE, owner, pid, *(bits(v) for v in offset), this=prim)
    w = lambda f, v: emu.write_u32(prim + f, v & 0xFFFFFFFF)  # noqa: E731
    wf = lambda f, v: emu.write_u32(prim + f, bits(f32(v)))  # noqa: E731
    sets = {}
    flags = 0
    for bit in (1, 2, 4, 8, 0x80):
        if rng.random() < 0.35:
            flags |= bit
    w(0x13C, flags)
    sets[0x13C] = flags
    life = rng.randrange(8, 40)
    ints = {0x144: life, 0x148: rng.choice([1, 1, 2, 3, 4]), 0x134: rng.randrange(1, 15),
            0x150: rng.choice([-1, rng.randrange(0, life)]), 0x160: rng.choice([-1, rng.randrange(0, 6)]),
            0x1B4: rng.choice([0, 1, 1]),
            0x19C: rng.randrange(-20, 280), 0x1A0: rng.randrange(-20, 280), 0x1A4: rng.randrange(-20, 280),
            0x1A8: rng.choice([0, 0, 9, -11, 40]), 0x1AC: rng.choice([0, 0, 7, -13, 50]),
            0x1B0: rng.choice([0, 0, 5, -9, 60])}
    frames = rng.randrange(1, 5)
    ints[0x164] = frames
    textures = emu.alloc(4 * frames)
    for i in range(frames):
        emu.write_u32(textures + 4 * i, TEXTURE_BASE + 0x100 + i)
    w(0xA8, textures)
    for f, v in ints.items():
        w(f, v)
        sets[f] = v
    floats = {0x1EC: rng.uniform(-3, 6), 0x1F0: rng.uniform(-0.3, 0.3), 0x23C: rng.uniform(-400, 400),
              0x244: rng.uniform(-400, 400), 0x254: rng.uniform(0, 255), 0x258: rng.uniform(-20, 20),
              0x25C: rng.choice([1.0, 0.5, 2.0]), 0x204: rng.choice([254.0, 200.0, 128.0]),
              0x208: rng.choice([0.0, 0.0, 30.0]), 0x268: rng.uniform(-8, 8), 0x274: rng.uniform(-0.5, 0.5),
              0x284: rng.uniform(0, 40), 0x288: rng.uniform(-2, 2), 0x28C: rng.uniform(-0.2, 0.2),
              0x290: rng.uniform(0, 60), 0x294: rng.uniform(-2, 2), 0x298: rng.uniform(-0.2, 0.2),
              0x260: rng.uniform(-5, 5), 0x26C: rng.uniform(-0.5, 0.5), 0x278: rng.uniform(0, 20),
              0x27C: rng.uniform(-2, 2), 0x280: rng.uniform(-0.3, 0.3)}
    for f in (0x1CC, 0x1D0, 0x1F4, 0x1F8, 0x1FC, 0x200):
        if rng.random() < 0.4:
            floats[f] = rng.uniform(-2, 2)
    for f, v in floats.items():
        wf(f, v)
        sets[f] = bits(f32(v))
    # alpha keys: +0xb4 steps the speed, +0xb8 sets the alpha and its limit
    keys = rng.choice([0, 0, 1, 2])
    if keys:
        n = rng.randrange(1, 5)
        at = sorted(rng.sample(range(0, life), min(n, life)))
        n = len(at)
        for base, values in ((0x2A4, [float(a) for a in at]), (0x2B4, [rng.uniform(-20, 255) for _ in at]),
                             (0x2C4, [rng.choice([254.0, 180.0]) for _ in at])):
            buf = emu.alloc(4 * (n + 1))
            emu.mem_write(buf, struct.pack(f"<{n + 1}f", *values, 1e9))
            w(base, buf)
            w(base + 4, buf + 4 * n)
            w(base + 8, buf + 4 * n)
            sets[base] = (n, [bits(f32(v)) for v in values])
        w(0xB4 if keys == 1 else 0xB8, 1)
        sets[0xB4 if keys == 1 else 0xB8] = 1
        w(0x138, rng.choice([0, 0, -1, n + 2]) if keys == 2 else 0)
        sets[0x138] = emu.read_u32(prim + 0x138)
    return prim, offset, sets


def synthetic(o, rng, lines):
    emu = o.emu
    actor = emu.scratch(size=0x100)
    rows = []
    for n in range(36):
        pid = n % 3
        mark = emu.heap_mark()
        effect = o.rag_effect(actor)
        pos = [f32(rng.uniform(-50, 50)) for _ in range(3)]
        emu.mem_write(effect + 4, struct.pack("<3f", *pos))
        duration = rng.choice([10, 30, 60, 300])
        emu.write_u32(effect + 0xC0, duration)
        emu.write_u32(effect + 0xBC, rng.randrange(0, 5))
        prim, offset, sets = random_prim(rng, o, effect, pid)
        emu.call(SRAND, 500 + n)
        visible = emu.mem_read(prim + 0x84, 1)[0]
        name = f"kPrim{n}"
        set_rows = []
        for f, v in sorted(sets.items()):
            if isinstance(v, tuple):
                set_rows.append(f"{{0x{f:x}, {v[0]}, {{{hexs(v[1] + [0] * (4 - len(v[1])))}}}}}")
            else:
                set_rows.append(f"{{0x{f:x}, 0x{v & 0xFFFFFFFF:08x}, {{}}}}")
        lines.append(f"static const FieldVector {name}Fields[] = {{{', '.join(set_rows)}}};")
        frames = []
        for frame in range(80):
            sx, sy = f32(rng.uniform(100, 700)), f32(rng.uniform(100, 500))
            emu.mem_write(effect + 0xDC, struct.pack("<2f", sx, sy))
            more = emu.call(PRIM_VF01, this=prim).eax & 0xFF
            state = o.state(prim)
            added = o.draw(prim) if more else []
            lines.append(f"static const TriangleVector {name}Frame{frame}[] = {triangles_cpp(added)};")
            frames.append(f"    {{{{{hexs([bits(sx), bits(sy)])}}}, {more}, {{{hexs(state)}}}, {name}Frame{frame}, {len(added)}}},")
            emu.write_u32(effect + 0xBC, emu.read_u32(effect + 0xBC) + 1)
            if not more:
                break
        lines.append(f"static const PrimFrameVector {name}Frames[] = {{")
        lines += frames
        lines.append("};")
        rows.append(f"    {{{pid}, {visible}, {{{hexs(map(bits, pos))}}}, {{{hexs(map(bits, offset))}}}, {duration}, "
                    f"{emu.read_u32(effect + 0xBC) - len(frames)}, {name}Fields, {len(sets)}, {name}Frames, {len(frames)}}},")
        emu.heap_release(mark)
    lines.append("static const PrimVector kPrims[] = {")
    lines += rows
    lines.append("};")


def hit_sparks(o, rng, lines):
    emu = o.emu
    rows = []
    for run in range(4):
        seed = 1000 + run
        o.reset_world()
        actor = emu.scratch(size=0x100)
        pos = (f32(rng.uniform(-30, 30)), f32(rng.uniform(-10, 0)), f32(rng.uniform(-30, 30)))
        emu.mem_write(actor + 4, struct.pack("<3f", *pos))
        emu.call(SRAND, seed)
        effect = o.rag_effect(actor)
        emu.call(INIT_EFFECT, actor, 1, 0, 0, 0, this=effect)
        duration = [None, 12, None, 6][run]
        if duration is not None:
            emu.write_u32(effect + 0xC0, duration)
        duration = emu.read_u32(effect + 0xC0)
        alive, prims, frames, views, screen = True, [], [], [], [0, 0]
        name = f"kSpark{run}"
        for frame in range(70):
            if frame % 20 == 0:
                at = (rng.uniform(-30, 30), rng.uniform(-10, 0), rng.uniform(-30, 30))
                o.build_view(f32(rng.choice([-45.0, -35.0, -55.0])), f32(rng.uniform(0, 360)),
                             f32(rng.choice([300.0, 250.0])), at)
                views.append((frame, list(struct.unpack("<12I", emu.mem_read(o.view + 0x94, 48)))))
            more = 0
            if alive:
                more = emu.call(RAG_EFFECT_VF01, this=effect).eax & 0xFF
                screen = list(struct.unpack("<2I", emu.mem_read(effect + 0xDC, 8)))
                if not more:
                    alive = False
                    for _, p in o.world_objects():
                        emu.call(PRIM_VF02, 0, 0x18, 0, 0, 0, this=p)
            for node, p in o.world_objects():
                if p not in prims:
                    prims.append(p)
                if not emu.call(PRIM_VF01, this=p).eax & 0xFF:
                    o.unlink(node)
            states, added = [], []
            for _, p in o.world_objects():
                states.append(f"{{{prims.index(p)}, {{{hexs(o.state(p))}}}}}")
                added += o.draw(p)
            lines.append(f"static const SparkPrimVector {name}Frame{frame}Prims[] = {{{', '.join(states) or '{}'}}};")
            lines.append(f"static const TriangleVector {name}Frame{frame}Triangles[] = {triangles_cpp(added)};")
            frames.append(f"    {{{more}, {{{hexs(screen)}}}, {name}Frame{frame}Prims, {len(states)}, "
                          f"{name}Frame{frame}Triangles, {len(added)}}},")
            if not alive and not o.world_objects():
                break
        lines.append(f"static const SparkFrameVector {name}Frames[] = {{")
        lines += frames
        lines.append("};")
        lines.append(f"static const ViewVector {name}Views[] = {{")
        lines += [f"    {{{f}, {{{hexs(v)}}}}}," for f, v in views]
        lines.append("};")
        rows.append(f"    {{{seed}, {{{hexs(map(bits, pos))}}}, {duration}, {name}Frames, {len(frames)}, "
                    f"{name}Views, {len(views)}}},")
    lines.append("static const SparkVector kSparks[] = {")
    lines += rows
    lines.append("};")


def random_prim3(rng, o, owner, pid):
    """A primitive of id 5, 6 or 0xd with the fields its update and drawing read set at
    random; returns it and the fields set {offset: dword, or (count, dwords)}."""
    emu = o.emu
    prim = emu.alloc(PRIM_SIZE)
    emu.call(PRIM_CTOR, this=prim)
    offset = [f32(rng.uniform(-20, 20)) for _ in range(3)]
    emu.call(INIT_PRIMITIVE, owner, pid, *(bits(v) for v in offset), this=prim)
    sets = {}

    def w(f, v):
        emu.write_u32(prim + f, v & 0xFFFFFFFF)
        sets[f] = v & 0xFFFFFFFF

    def wf(f, v):
        w(f, bits(f32(v)))

    life = rng.randrange(8, 40)
    w(0x144, life)
    w(0x148, rng.choice([1, 2, 3, 4]))
    w(0x150, rng.choice([-1, 0, rng.randrange(0, life)]))
    w(0x1B4, rng.choice([0, 1, 1]))
    w(0x19C, rng.randrange(0, 256))
    w(0x1A0, rng.randrange(0, 256))
    w(0x1A4, rng.randrange(0, 256))
    wf(0x254, rng.uniform(60, 254))
    wf(0x258, rng.uniform(-15, 15))
    for f in (0x1EC, 0x1F0, 0x23C, 0x240, 0x244, 0x268):
        wf(f, rng.uniform(-3, 3) if f in (0x1EC, 0x1F0, 0x268) else rng.uniform(-200, 200))
    matrix = [rng.uniform(-1, 1) for _ in range(9)] + [rng.uniform(-0.5, 0.5) for _ in range(3)]
    for i, v in enumerate(matrix):
        wf(0xC4 + 4 * i, v)
    if pid in (5, 6):
        flags = 0
        bitset = (1, 2, 4, 8, 0x20, 0x100, 0x200, 0x400, 0x80) if pid == 5 else (0x200, 0x80)
        for bit in bitset:
            if rng.random() < 0.3:
                flags |= bit
        w(0x13C, flags)
        w(0x134, rng.randrange(1, 9))
        w(0x14C, rng.choice([-1, -1, rng.randrange(0, 6)]))
        w(0x160, rng.choice([-1, rng.randrange(0, 6)]))
        for f in range(0x1D4, 0x1EC, 4):
            wf(f, rng.uniform(-1.5, 1.5))
        wf(0x248, rng.uniform(0.3, 2.0))
        wf(0x24C, rng.uniform(-0.05, 0.05))
        wf(0x250, rng.uniform(-0.01, 0.01))
        wf(0x38, rng.uniform(0, 360))
        w(0x24, rng.randrange(0, len(ACTIONS)))
        w(0x28, rng.randrange(0, len(ACTIONS[emu.read_u32(prim + 0x24)])))
        w(0x128, rng.choice([1, 1, 0x101]))
        w(0x168, rng.choice([0, 1]))
        wf(0x21C, rng.uniform(-3, 1))
        wf(0x220, rng.uniform(0.02, 0.4))
        w(0x154, rng.randrange(1, 7))
        emu.write_u32(prim + 0x98, o.spr)
        emu.write_u32(prim + 0x9C, o.act_res)
    else:
        # vf01 ignores whether a cylinder's images ran out, so one that does not loop would
        # read past its textures.
        w(0x1B4, 1)
        w(0x13C, rng.choice([0, 1, 1, 0x80]))
        frames = rng.randrange(1, 4)
        w(0x164, frames)
        textures = emu.alloc(4 * frames)
        for i in range(frames):
            emu.write_u32(textures + 4 * i, TEXTURE_BASE + 0x100 + i)
        emu.write_u32(prim + 0xA8, textures)
        for f in (0x224, 0x228, 0x290):
            wf(f, rng.uniform(1, 25))
        for f in (0x22C, 0x230, 0x294, 0x260, 0x264):
            wf(f, rng.uniform(-1.5, 1.5))
        for f in (0x234, 0x238, 0x298, 0x26C, 0x270):
            wf(f, rng.uniform(-0.1, 0.1))
        wf(0x29C, rng.choice([100.0, 30.0, 12.0]))
        wf(0x1B8, rng.choice([36.0, 30.0, 45.0, 90.0, 22.5]))
    emu.call(RESET_HISTORY, this=prim)
    return prim, offset, sets


def particle_cpp(lines):
    lines.append("// The particle stand-in's images {width, height, halved x, halved y} and items")
    lines.append("// {action, frame, x, y, image, flags, r, g, b, a, scale x, scale y, angle, type}.")
    for name, images in (("kImages8", IMAGES8), ("kImages32", IMAGES32)):
        lines.append(f"static const ImageVector {name}[] = {{" + ", ".join(
            f"{{{w}, {h}, {hx}, {hy}}}" for w, h, hx, hy in images) + "};")
    items = []
    for a, frames in enumerate(ACTIONS):
        for f, its in enumerate(frames):
            for x, y, image, fl, r, g, b, al, sx, sy, angle, t in its:
                items.append(f"{{{a}, {f}, {x}, {y}, {image}, {fl}, {r}, {g}, {b}, {al}, 0x{bits(sx):08x}, "
                             f"0x{bits(sy):08x}, {angle}, {t}}}")
    lines.append("static const ItemVector kItems[] = {" + ", ".join(items) + "};")
    lines.append(f"static const int kActionFrames[] = {{{', '.join(str(len(f)) for f in ACTIONS)}}};")


def synthetic3(o, rng, lines):
    emu = o.emu
    particle_cpp(lines)
    actor = emu.scratch(size=0x100)
    rows = []
    for n in range(30):
        pid = (5, 6, 0xD)[n % 3]
        mark = emu.heap_mark()
        effect = o.rag_effect(actor)
        pos = [f32(rng.uniform(-30, 30)), f32(rng.uniform(-8, 0)), f32(rng.uniform(-30, 30))]
        emu.mem_write(effect + 4, struct.pack("<3f", *pos))
        target = [f32(rng.uniform(-30, 30)), 0.0, f32(rng.uniform(-30, 30))]
        emu.mem_write(effect + 0xE8, struct.pack("<3f", *target))
        duration = rng.choice([12, 40, 300])
        emu.write_u32(effect + 0xC0, duration)
        emu.write_u32(effect + 0xBC, 0)
        prim, offset, sets = random_prim3(rng, o, effect, pid)
        emu.call(SRAND, 700 + n)
        view, yaw, slope, pitch = o.scene(rng)
        name = f"kShape{n}"
        lines.append(f"static const FieldVector {name}Fields[] = {{" + ", ".join(
            f"{{0x{f:x}, 0x{v:08x}, {{}}}}" for f, v in sorted(sets.items())) + "};")
        frames = []
        for frame in range(40):
            more = emu.call(PRIM_VF01, this=prim).eax & 0xFF
            state = o.state3(prim)
            added = o.draw(prim) if more else []
            quads = list(o.quads) if more else []
            lines.append(f"static const std::uint32_t {name}Frame{frame}State[] = {{{hexs(state)}}};")
            lines.append(f"static const TriangleVector {name}Frame{frame}Triangles[] = {triangles_cpp(added)};")
            lines.append(f"static const QuadVector {name}Frame{frame}Quads[] = {quads_cpp(quads)};")
            frames.append(f"    {{{more}, {name}Frame{frame}State, {len(state)}, {name}Frame{frame}Triangles, "
                          f"{len(added)}, {name}Frame{frame}Quads, {len(quads)}}},")
            emu.write_u32(effect + 0xBC, emu.read_u32(effect + 0xBC) + 1)
            if not more:
                break
        lines.append(f"static const ShapeFrameVector {name}Frames[] = {{")
        lines += frames
        lines.append("};")
        rows.append(f"    {{{pid}, {{{hexs(map(bits, pos))}}}, {{{hexs(map(bits, target))}}}, "
                    f"{{{hexs(map(bits, offset))}}}, {duration}, {scene_cpp(view, yaw, slope, pitch)}, {name}Fields, "
                    f"{len(sets)}, {name}Frames, {len(frames)}}},")
        emu.heap_release(mark)
    lines.append("static const ShapeVector kShapes[] = {")
    lines += rows
    lines.append("};")


def bursts(o, rng, lines):
    """Effect ids 0 (both ways of minimal effects) and 2 through the world list."""
    emu = o.emu
    rows = []
    for run, (eid, minimal) in enumerate([(0, 0), (0, 1), (2, 0), (0, 0), (2, 0)]):
        seed = 2000 + run
        o.reset_world()
        actor = emu.scratch(size=0x100)
        pos = (f32(rng.uniform(-30, 30)), f32(rng.uniform(-10, 0)), f32(rng.uniform(-30, 30)))
        facing = f32(rng.choice([0.0, 90.0, 200.0, 315.0, rng.uniform(0, 360)]))
        emu.mem_write(actor + 4, struct.pack("<3f", *pos))
        emu.mem_write(actor + 0x38, struct.pack("<f", facing))
        emu.write_u32(MINIMAL_EFFECTS, minimal)
        emu.call(SRAND, seed)
        effect = o.rag_effect(actor)
        emu.call(INIT_EFFECT, actor, eid, 0, 0, 0, this=effect)
        duration = emu.read_u32(effect + 0xC0)
        alive, prims, frames, scenes = True, [], [], []
        name = f"kBurst{run}"
        for frame in range(70):
            if frame % 15 == 0:
                scenes.append((frame, *o.scene(rng)))
            more = 0
            if alive:
                more = emu.call(RAG_EFFECT_VF01, this=effect).eax & 0xFF
                if not more:
                    alive = False
                    for _, p in o.world_objects():
                        emu.call(PRIM_VF02, 0, 0x18, 0, 0, 0, this=p)
            for node, p in o.world_objects():
                if p not in prims:
                    prims.append(p)
                if not emu.call(PRIM_VF01, this=p).eax & 0xFF:
                    o.unlink(node)
            states, added, quads = [], [], []
            for _, p in o.world_objects():
                state = o.state3(p)
                lines.append(f"static const std::uint32_t {name}Frame{frame}Prim{len(states)}[] = {{{hexs(state)}}};")
                states.append(f"{{{prims.index(p)}, {emu.read_u32(p + 0x130)}, {name}Frame{frame}Prim{len(states)}, "
                              f"{len(state)}}}")
                added += o.draw(p)
                quads += o.quads
            lines.append(f"static const BurstPrimVector {name}Frame{frame}Prims[] = {{{', '.join(states) or '{}'}}};")
            lines.append(f"static const TriangleVector {name}Frame{frame}Triangles[] = {triangles_cpp(added)};")
            lines.append(f"static const QuadVector {name}Frame{frame}Quads[] = {quads_cpp(quads)};")
            frames.append(f"    {{{more}, {name}Frame{frame}Prims, {len(states)}, {name}Frame{frame}Triangles, "
                          f"{len(added)}, {name}Frame{frame}Quads, {len(quads)}}},")
            if not alive and not o.world_objects():
                break
        emu.write_u32(MINIMAL_EFFECTS, 0)
        lines.append(f"static const BurstFrameVector {name}Frames[] = {{")
        lines += frames
        lines.append("};")
        lines.append(f"static const SceneVector {name}Scenes[] = {{")
        lines += [f"    {{{f}, {scene_cpp(v, y, sl, pi)}}}," for f, v, y, sl, pi in scenes]
        lines.append("};")
        rows.append(f"    {{{eid}, {minimal}, {seed}, {{{hexs(map(bits, pos))}}}, 0x{bits(facing):08x}, {duration}, "
                    f"{name}Frames, {len(frames)}, {name}Scenes, {len(scenes)}}},")
    lines.append("static const BurstVector kBursts[] = {")
    lines += rows
    lines.append("};")


RING = 0x156B0
RING_SIZE = 0xB8
RING_STATE = [0x4, 0x8, 0xC, 0x140]


def ring_state(o, prim):
    emu = o.emu
    out = [emu.read_u32(prim + f) for f in RING_STATE]
    for k in range(4):
        out += list(struct.unpack("<34I", emu.mem_read(prim + RING + RING_SIZE * k, 0x88)))
    return out


def ring_textures(o, prim, count=1):
    emu = o.emu
    textures = emu.alloc(4 * count)
    for i in range(count):
        emu.write_u32(textures + 4 * i, TEXTURE_BASE + 0x100 + i)
    emu.write_u32(prim + 0xA8, textures)


def synthetic_rings(o, rng, lines):
    emu = o.emu
    actor = emu.scratch(size=0x100)
    rows = []
    ids = [0x17, 0x18, 0x19, 0x1C, 0x1D, 0x22, 0x27, 0x30, 0x36, 0x39, 0x3C, 0x3E]
    for n in range(2 * len(ids)):
        pid = ids[n % len(ids)]
        mark = emu.heap_mark()
        effect = o.rag_effect(actor)
        pos = [f32(rng.uniform(-30, 30)), f32(rng.uniform(-8, 0)), f32(rng.uniform(-30, 30))]
        emu.mem_write(effect + 4, struct.pack("<3f", *pos))
        emu.write_u32(effect + 0xC0, 300)
        prim = emu.alloc(PRIM_SIZE)
        emu.call(PRIM_CTOR, this=prim)
        emu.call(INIT_PRIMITIVE, effect, pid, 0, 0, 0, this=prim)
        sets = {}

        def w(f, v):
            emu.write_u32(prim + f, v & 0xFFFFFFFF)
            sets[f] = v & 0xFFFFFFFF

        w(0x144, rng.randrange(10, 40))
        w(0x128, rng.choice([5, 5, 1, 4, 0x105]))
        w(0x248, bits(f32(rng.choice([0.0, 1.0, 2.5, 3.0, 3.0, 4.0, 5.9, 7.0, 10.5, 11.0, 12.0, rng.uniform(0, 12)]))))
        w(0x1F0, rng.choice([0x42C60000, 0, bits(f32(1.5))]))
        ring_textures(o, prim)
        for k in range(4):
            base = RING + RING_SIZE * k
            span = rng.choice([360, 360, 180, 90, 210, 21])
            height = rng.choice([25.0, 120.0, 6.001, rng.uniform(2, 60)])
            # Inputs inside the tables the updates index: no negative ages, the 0x36 ring's
            # doubled age within 360, the 0x30 kind an angle, the 0x3e radius at most 10.
            ages = [0, 5, 9, 10, 11, 30, 195, 240, 250, 251, 285, 300, 795, 799, 803, 1395, 125]
            if pid == 0x36:
                ages = [0, 5, 60, 66, 88, 89, 90, 120, 170]
            age = rng.choice(ages)
            kind = rng.choice([-1, 0, 1, 1, 2, 60, 3080])
            if pid == 0x30:
                kind = rng.randrange(0, 360)
            radius = rng.uniform(0.5, 9.9 if pid == 0x3E else 15)
            flags = [rng.choice([0, 0, 1, 1, 2]) for _ in range(21)]
            if pid == 0x36:
                flags[1] = rng.choice([100, 70, 40, 15, 0xF0])
            rec = struct.pack("<BBhfIhhhH", rng.random() < 0.85, 0, span, f32(height), age,
                              rng.randrange(0, 360), rng.randrange(-5, 255), kind, 0)
            rec += struct.pack("<21f", *(f32(rng.uniform(-5, 40)) for _ in range(21)))
            rec += struct.pack("<hHf", rng.randrange(20, 90), 0, f32(radius))
            rec += bytes(flags) + bytes(3)
            emu.mem_write(prim + base, rec)
            for i in range(0, 0x88, 4):
                sets[base + i] = emu.read_u32(prim + base + i)
        view, yaw, slope, pitch = o.scene(rng)
        name = f"kRing{n}"
        lines.append(f"static const FieldVector {name}Fields[] = {{" + ", ".join(
            f"{{0x{f:x}, 0x{v:08x}, {{}}}}" for f, v in sorted(sets.items())) + "};")
        frames = []
        for frame in range(12):
            more = emu.call(PRIM_VF01, this=prim).eax & 0xFF
            state = ring_state(o, prim)
            added = o.draw(prim) if more else []
            lines.append(f"static const std::uint32_t {name}Frame{frame}State[] = {{{hexs(state)}}};")
            if frame in (0, 10):
                lines.append(f"static const TriangleVector {name}Frame{frame}Triangles[] = {triangles_cpp(added)};")
                triangles = f"{name}Frame{frame}Triangles, {len(added)}"
            else:
                triangles = "nullptr, kUnchecked"
            frames.append(f"    {{{more}, {name}Frame{frame}State, {len(state)}, {triangles}, nullptr, 0}},")
            emu.write_u32(effect + 0xBC, emu.read_u32(effect + 0xBC) + 1)
            if not more:
                break
        lines.append(f"static const ShapeFrameVector {name}Frames[] = {{")
        lines += frames
        lines.append("};")
        rows.append(f"    {{{pid}, {{{hexs(map(bits, pos))}}}, {{0, 0, 0}}, {{0, 0, 0}}, 300, "
                    f"{scene_cpp(view, yaw, slope, pitch)}, {name}Fields, {len(sets)}, {name}Frames, {len(frames)}}},")
        emu.heap_release(mark)
    lines.append("static const ShapeVector kRings[] = {")
    lines += rows
    lines.append("};")


RING_EFFECTS = [0xE1, 0xE7, 0xEC, 0xED, 0xEE, 0xF7, 0xF8, 0x103, 0x104, 0x105, 0x107,
                0xC8, 0xDE, 0xFC, 0xEF, 0xF0, 0xF1, 0x118, 0x11D, 0x130, 0x13A, 0x13B, 0x13C]


def ring_effects(o, rng, lines):
    emu = o.emu
    rows = []
    for run, eid in enumerate(RING_EFFECTS):
        o.reset_world()
        actor = emu.scratch(size=0x100)
        pos = (f32(rng.uniform(-30, 30)), f32(rng.uniform(-10, 0)), f32(rng.uniform(-30, 30)))
        emu.mem_write(actor + 4, struct.pack("<3f", *pos))
        emu.call(SRAND, 3000 + run)
        effect = o.rag_effect(actor)
        emu.call(INIT_EFFECT, actor, eid, 0, 0, 0, this=effect)
        duration = emu.read_u32(effect + 0xC0)
        alive, prims, frames, scenes = True, [], [], []
        name = f"kRingEffect{run}"
        for frame in range(24):
            if frame % 8 == 0:
                scenes.append((frame, *o.scene(rng)))
            more = 0
            if alive:
                more = emu.call(RAG_EFFECT_VF01, this=effect).eax & 0xFF
                if not more:
                    alive = False
                    for _, p in o.world_objects():
                        emu.call(PRIM_VF02, 0, 0x18, 0, 0, 0, this=p)
            for node, p in o.world_objects():
                if p not in prims:
                    prims.append(p)
                if not emu.call(PRIM_VF01, this=p).eax & 0xFF:
                    o.unlink(node)
            states, added = [], []
            for _, p in o.world_objects():
                state = ring_state(o, p)
                lines.append(f"static const std::uint32_t {name}Frame{frame}Prim{len(states)}[] = {{{hexs(state)}}};")
                states.append(f"{{{prims.index(p)}, {emu.read_u32(p + 0x130)}, {name}Frame{frame}Prim{len(states)}, "
                              f"{len(state)}}}")
                added += o.draw(p)
            lines.append(f"static const BurstPrimVector {name}Frame{frame}Prims[] = {{{', '.join(states) or '{}'}}};")
            if frame in (0, 12, 23):
                lines.append(f"static const TriangleVector {name}Frame{frame}Triangles[] = {triangles_cpp(added)};")
                triangles = f"{name}Frame{frame}Triangles, {len(added)}"
            else:
                triangles = "nullptr, kUnchecked"
            frames.append(f"    {{{more}, {name}Frame{frame}Prims, {len(states)}, {triangles}, nullptr, 0}},")
            if not alive and not o.world_objects():
                break
        lines.append(f"static const BurstFrameVector {name}Frames[] = {{")
        lines += frames
        lines.append("};")
        lines.append(f"static const SceneVector {name}Scenes[] = {{")
        lines += [f"    {{{f}, {scene_cpp(v, y, sl, pi)}}}," for f, v, y, sl, pi in scenes]
        lines.append("};")
        rows.append(f"    {{{eid}, 0, {3000 + run}, {{{hexs(map(bits, pos))}}}, 0, {duration}, "
                    f"{name}Frames, {len(frames)}, {name}Scenes, {len(scenes)}}},")
    lines.append("static const BurstVector kRingEffects[] = {")
    lines += rows
    lines.append("};")


SQUARE_IDS = [0x1F, 0x28, 0x2B, 0x2D, 0x2F, 0x33, 0x37, 0x38]
SQUARE_STATE = 0xA8  # the bytes of each record the facing squares use


def square_state(o, prim):
    emu = o.emu
    out = [emu.read_u32(prim + f) for f in RING_STATE]
    for k in range(4):
        out += list(struct.unpack(f"<{SQUARE_STATE // 4}I", emu.mem_read(prim + RING + RING_SIZE * k, SQUARE_STATE)))
    return out


def prim_state(o, prim):
    return square_state(o, prim) if o.emu.read_u32(prim + 0x130) in SQUARE_IDS else ring_state(o, prim)


def toward(rng, at, near, far):
    """A point near to far out from at along the ground, between 40 and 320 degrees round
    (0x2b's spread of 30 either way stays within the angle tables), up to 10 off in height."""
    angle = math.radians(rng.uniform(40, 320))
    r = rng.uniform(near, far)
    return (f32(at[0] + r * math.cos(angle)), f32(at[1] + rng.uniform(-10, 10)), f32(at[2] + r * math.sin(angle)))


def random_square(rng, pid, k):
    rec = bytearray(RING_SIZE)
    struct.pack_into("<Bxh", rec, 0, rng.random() < 0.9, rng.randrange(0, 600 if pid == 0x33 else 360))
    struct.pack_into("<f", rec, 4, f32(rng.uniform(5, 80)))
    age = {0x1F: rng.randrange(0, 120), 0x2B: rng.randrange(-5, 30), 0x38: rng.randrange(-5, 30)}.get(pid, rng.randrange(0, 40))
    heading = rng.randrange(50, 300) if pid == 0x1F else rng.randrange(0, 360)
    struct.pack_into("<ihhh", rec, 8, age, heading, rng.randrange(-5, 256), rng.randrange(0, 360))
    if pid == 0x1F:
        walk = [rng.uniform(0, 359) for _ in range(4)]
    else:
        walk = [rng.uniform(0, 359), rng.uniform(0, 3), rng.uniform(-1, 1), rng.uniform(0, 359)]
    struct.pack_into("<4f", rec, 0x14, *(f32(w) for w in walk))
    mode = rng.choice([0.0, 1.0, 2.0, 99.5]) if pid == 0x1F else rng.choice([0.0, 0.0, 0.0, 2.0])
    struct.pack_into("<3f", rec, 0x2C, f32(rng.uniform(-200, 50)), f32(mode), f32(rng.uniform(0, 359.9)))
    struct.pack_into("<f", rec, 0x6C, f32(rng.uniform(1, 40)))
    state = {0x28: rng.choice([0, 1, 2]), 0x33: rng.choice([0, 3, 10, 30, 60]), 0x2F: rng.choice([0, 1]),
             0x38: rng.choice([0, 1])}.get(pid, 0)
    color = rng.choice([0, 1, 2, 3, 4, 7, 8, 9, 9, 10, 11, 5]) if k == 0 else rng.randrange(0, 12)
    struct.pack_into("<4B", rec, 0x70, rng.choice([0, 0, 1]), state, color, rng.randrange(0, 7))
    struct.pack_into("<3f", rec, 0x88, *(f32(rng.uniform(-40, 40)) for _ in range(3)))
    struct.pack_into("<f", rec, 0xA4, f32(rng.uniform(-20, 0)))
    return bytes(rec)


def synthetic_squares(o, rng, lines):
    emu = o.emu
    actor = emu.scratch(size=0x100)
    rows = []
    for n in range(3 * len(SQUARE_IDS)):
        pid = SQUARE_IDS[n % len(SQUARE_IDS)]
        mark = emu.heap_mark()
        effect = o.rag_effect(actor)
        pos = [f32(rng.uniform(-30, 30)), f32(rng.uniform(-8, 0)), f32(rng.uniform(-30, 30))]
        emu.mem_write(effect + 4, struct.pack("<3f", *pos))
        emu.write_u32(effect + 0xC0, 300)
        prim = emu.alloc(PRIM_SIZE)
        emu.call(PRIM_CTOR, this=prim)
        emu.call(INIT_PRIMITIVE, effect, pid, 0, 0, 0, this=prim)
        sets = {}

        def w(f, v):
            emu.write_u32(prim + f, v & 0xFFFFFFFF)
            sets[f] = v & 0xFFFFFFFF

        w(0x144, rng.randrange(10, 40))
        w(0x128, rng.choice([5, 5, 1, 0x105]))
        for i, v in enumerate(toward(rng, pos, 20, 90)):
            w(0x2F8 + 4 * i, bits(v))
        ring_textures(o, prim, 2)
        for k in range(4):
            base = RING + RING_SIZE * k
            emu.mem_write(prim + base, random_square(rng, pid, k))
            for i in range(0, SQUARE_STATE, 4):
                v = emu.read_u32(prim + base + i)
                if v:
                    sets[base + i] = v
        view, yaw, slope, pitch = o.scene(rng)
        name = f"kSquare{n}"
        lines.append(f"static const FieldVector {name}Fields[] = {{" + ", ".join(
            f"{{0x{f:x}, 0x{v:08x}, {{}}}}" for f, v in sorted(sets.items())) + "};")
        frames = []
        emu.call(SRAND, 4000 + n)
        for frame in range(12):
            more = emu.call(PRIM_VF01, this=prim).eax & 0xFF
            state = square_state(o, prim)
            added = o.draw(prim) if more else []
            lines.append(f"static const std::uint32_t {name}Frame{frame}State[] = {{{hexs(state)}}};")
            if frame in (0, 6, 11):
                lines.append(f"static const TriangleVector {name}Frame{frame}Triangles[] = {triangles_cpp(added)};")
                triangles = f"{name}Frame{frame}Triangles, {len(added)}"
            else:
                triangles = "nullptr, kUnchecked"
            frames.append(f"    {{{more}, {name}Frame{frame}State, {len(state)}, {triangles}, nullptr, 0}},")
            emu.write_u32(effect + 0xBC, emu.read_u32(effect + 0xBC) + 1)
            if not more:
                break
        lines.append(f"static const ShapeFrameVector {name}Frames[] = {{")
        lines += frames
        lines.append("};")
        rows.append(f"    {{{pid}, {{{hexs(map(bits, pos))}}}, {{0, 0, 0}}, {{0, 0, 0}}, 300, "
                    f"{scene_cpp(view, yaw, slope, pitch)}, {name}Fields, {len(sets)}, {name}Frames, {len(frames)}}},")
        emu.heap_release(mark)
    lines.append("static const ShapeVector kSquares[] = {")
    lines += rows
    lines.append("};")


# The effects that add facing squares, with how many frames each runs.
SQUARE_EFFECTS = [(0xE5, 24), (0xE6, 24), (0xE9, 24), (0xF9, 48), (0x109, 48), (0x108, 56), (0x113, 56),
                  (0x116, 32), (0x117, 32), (0x119, 32), (0x11A, 32), (0x11C, 32), (0x11E, 32), (0x120, 32),
                  (0x121, 32), (0x123, 32), (0x125, 32), (0x11B, 32), (0x12A, 48), (299, 48), (0x134, 48),
                  (0x135, 32), (0x136, 32), (0x14B, 32), (0x14C, 32), (0x138, 32), (0x140, 32), (0x139, 32),
                  (0x145, 32), (0xFD, 32)]


def square_effects(o, rng, lines):
    emu = o.emu
    rows = []
    player = emu.read_u32(o.world + 0x3C)
    for run, (eid, count) in enumerate(SQUARE_EFFECTS):
        o.reset_world()
        actor = emu.scratch(size=0x100)
        pos = (f32(rng.uniform(-30, 30)), f32(rng.uniform(-10, 0)), f32(rng.uniform(-30, 30)))
        emu.mem_write(actor + 4, struct.pack("<3f", *pos))
        # 0xe5's clouds show only while the player is above their floor (y -152).
        at = (f32(rng.uniform(-30, 30)), f32(rng.uniform(-200, -160) if eid == 0xE5 else rng.uniform(-200, 0)),
              f32(rng.uniform(-30, 30)))
        emu.mem_write(player + 4, struct.pack("<3f", *at))
        target = toward(rng, pos, 30, 90)
        level = rng.randrange(1, 6)
        emu.call(SRAND, 5000 + run)
        effect = o.rag_effect(actor)
        emu.call(INIT_EFFECT, actor, eid, 0, 0, 0, this=effect)
        emu.mem_write(effect + 0xE8, struct.pack("<3f", *target))
        emu.write_u32(effect + 0xB8, level)
        duration = emu.read_u32(effect + 0xC0)
        alive, prims, frames, scenes = True, [], [], []
        name = f"kSquareEffect{run}"
        for frame in range(count):
            if frame % 8 == 0:
                scenes.append((frame, *o.scene(rng)))
            more = 0
            if alive:
                more = emu.call(RAG_EFFECT_VF01, this=effect).eax & 0xFF
                if not more:
                    alive = False
                    for _, p in o.world_objects():
                        emu.call(PRIM_VF02, 0, 0x18, 0, 0, 0, this=p)
            for node, p in o.world_objects():
                if p not in prims:
                    prims.append(p)
                if not emu.call(PRIM_VF01, this=p).eax & 0xFF:
                    o.unlink(node)
            # Every primitive's state each frame while there are one or two; every eighth
            # frame, the second and the last of the effects that add more, and of those that
            # add many only the second and the last.
            objects = o.world_objects()
            if len(objects) <= 2:
                sampled = True
            elif len(objects) <= 8:
                sampled = frame % 8 == 0 or frame in (1, count - 1)
            else:
                sampled = frame in (1, count - 1)
            states, added = [], []
            for _, p in objects:
                index = len(states)
                if sampled:
                    state = prim_state(o, p)
                    lines.append(f"static const std::uint32_t {name}Frame{frame}Prim{index}[] = {{{hexs(state)}}};")
                    ref = f"{name}Frame{frame}Prim{index}, {len(state)}"
                else:
                    ref = "nullptr, 0"
                states.append(f"{{{prims.index(p)}, {emu.read_u32(p + 0x130)}, {ref}}}")
                added += o.draw(p)
            lines.append(f"static const BurstPrimVector {name}Frame{frame}Prims[] = {{{', '.join(states) or '{}'}}};")
            checked = (0, 12, count - 1) if len(objects) <= 8 else (count - 1,)
            if frame in checked:
                lines.append(f"static const TriangleVector {name}Frame{frame}Triangles[] = {triangles_cpp(added)};")
                triangles = f"{name}Frame{frame}Triangles, {len(added)}"
            else:
                triangles = "nullptr, kUnchecked"
            frames.append(f"    {{{more}, {name}Frame{frame}Prims, {len(states)}, {triangles}, nullptr, 0}},")
            if not alive and not o.world_objects():
                break
        lines.append(f"static const BurstFrameVector {name}Frames[] = {{")
        lines += frames
        lines.append("};")
        lines.append(f"static const SceneVector {name}Scenes[] = {{")
        lines += [f"    {{{f}, {scene_cpp(v, y, sl, pi)}}}," for f, v, y, sl, pi in scenes]
        lines.append("};")
        rows.append(f"    {{{eid}, {level}, {5000 + run}, {{{hexs(map(bits, pos))}}}, {{{hexs(map(bits, at))}}}, "
                    f"{{{hexs(map(bits, target))}}}, {duration}, {name}Frames, {len(frames)}, {name}Scenes, "
                    f"{len(scenes)}}},")
    emu.mem_write(player + 4, bytes(12))
    lines.append("static const SquareEffectVector kSquareEffects[] = {")
    lines += rows
    lines.append("};")


def main():
    rng = random.Random(91)
    o = PrimOracle()
    lines = ["// Generated by tools/prim_vectors.py from the original in the emulation oracle.",
             f"// State fields, in order: {', '.join(f'0x{f:x}' for f in STATE)}."]
    table(o, lines)
    synthetic(o, rng, lines)
    hit_sparks(o, rng, lines)
    synthetic3(o, rng, lines)
    bursts(o, rng, lines)
    synthetic_rings(o, rng, lines)
    ring_effects(o, rng, lines)
    synthetic_squares(o, rng, lines)
    square_effects(o, rng, lines)
    names = sorted(o.textures.items(), key=lambda kv: kv[1])
    lines.append("static const TextureVector kTextures[] = {" +
                 ", ".join(f'{{0x{v:x}, "{c_literal(k)}"}}' for k, v in names) + "};")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
