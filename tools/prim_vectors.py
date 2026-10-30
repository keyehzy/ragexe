"""Test vectors for the port's effect table and screen-space primitives
(docs/render/effect-prim.md), taken from the original in the emulation oracle.

usage: .venv/bin/python -I tools/prim_vectors.py tests/unit/prim_vectors

It writes one file per family of primitives into that directory, so that none grows too
large to keep in git; tests/unit/prim_test.cpp includes them all.

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

from unicorn import UC_HOOK_CODE

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from actor_vectors import (ADD_SPRITE_QUAD, GET_FRAME_COUNT, GET_GAME_MODE, SCALES,  # noqa: E402
                           Oracle, bits, f32)

RAG_EFFECT_CTOR, INIT_EFFECT, RAG_EFFECT_VF01 = 0x54D220, 0x54EEE0, 0x54D4C0
LOAD_STR_FILE, ADVANCE_STR, RESET_STR = 0x54FAE0, 0x54FBA0, 0x54FB40
GET_ACTOR_BY_ID, PLAYER_ID = 0x4C4CF0, 0x68734C
# The ids the tube's runs look up: the player's (g_ClientState+0xcdc) and one other actor's,
# where LINK_ACTOR stands; tests/unit/prim_test.cpp uses the same.
LINK_PLAYER_ID, LINK_ACTOR_ID, LINK_ACTOR, LINK_PLAYER = 31337, 4242, (-18.0, -6.5, 9.0), (12.5, -3.0, -20.0)
EMPTY_VF4 = 0x4BC290
MODE_SWITCHER = 0x67C9E8
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
        # SetAct hands out set_act; each stand-in act counts its own frames.
        self.set_act = self.act_res
        self.actions_of = {self.act_res: ACTIONS}
        emu.replace_thiscall(SET_ACT, 1, lambda this, name: emu.write_u32(this + 0x9C, self.set_act) or 0)
        emu.replace_thiscall(GET_FRAME_COUNT, 1, lambda this, a: len(self.actions_of[this][a])
                             if len(self.actions_of[this][a]) >= 2 else 1)
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

    def scene(self, rng, look_at=None):
        at = (rng.uniform(-30, 30), rng.uniform(-10, 0), rng.uniform(-30, 30))
        if look_at is not None:
            at = look_at
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


TRAIL_IDS = [7, 8, 0x15, 0x26, 0x2C, 0x31]
# What the trails' updates change beyond STATE3: the key, the fall's rate, the pitch's, the
# distance, its speed and rate, the offset and the drift's y.
TRAIL_EXTRA = [0x138, 0x220, 0x270, 0x278, 0x27C, 0x280, 0x304, 0x308, 0x30C, 0x314]


def state_trail(o, prim):
    """STATE3 and TRAIL_EXTRA, the matrices +0xc4 and +0xf4, the first record and the
    history records."""
    emu = o.emu
    n = emu.read_u32(prim + 0x154)
    return ([emu.read_u32(prim + f) for f in STATE3 + TRAIL_EXTRA] +
            list(struct.unpack("<24I", emu.mem_read(prim + 0xC4, 96))) +
            list(struct.unpack(f"<{RING_SIZE // 4}I", emu.mem_read(prim + RING, RING_SIZE))) +
            list(struct.unpack(f"<{35 * n}I", emu.mem_read(prim + 0x358, 0x8C * n))))


def random_trail(rng, o, owner, pid):
    """A primitive of a trail id with the fields its update and drawing read set at random;
    returns it, its offset and the fields set, as random_prim3."""
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
    for i in range(3):
        wf(0x304 + 4 * i, rng.uniform(-15, 15))
    if pid == 0x15:
        flags = 0
        for bit in (1, 2, 4, 8, 0x20, 0x400, 0x80):
            if rng.random() < 0.3:
                flags |= bit
        w(0x13C, flags)
        w(0x134, rng.randrange(1, 9))
        w(0x14C, rng.choice([-1, -1, rng.randrange(0, 6)]))
        w(0x160, rng.choice([-1, rng.randrange(0, 6)]))
        for f in range(0x1D4, 0x1EC, 4):
            wf(f, rng.uniform(-1.5, 1.5))
    else:
        w(0x13C, rng.choice([0, 0x20, 0x80, 0xA0]))
    if pid == 7:
        for f, lo, hi in ((0x278, 0, 20), (0x27C, -2, 2), (0x280, -0.3, 0.3), (0x21C, -3, 1), (0x220, -0.2, 0.2),
                          (0x260, -20, 20), (0x26C, -0.5, 0.5), (0x264, -0.2, 0.2)):
            wf(f, rng.uniform(lo, hi))
        w(0x160, rng.choice([-1, rng.randrange(0, 6)]))
        for f in (0x1CC, 0x1D0):
            if rng.random() < 0.5:
                wf(f, rng.uniform(-2, 2))
        if rng.random() < 0.6:
            n = rng.randrange(1, 4)
            at = sorted(rng.sample(range(0, life), min(n, life)))
            n = len(at)
            for base, values in ((0x2A4, [float(a) for a in at]), (0x2B4, [rng.uniform(-1, 1) for _ in at]),
                                 (0x2C4, [rng.uniform(-0.1, 0.1) for _ in at])):
                buf = emu.alloc(4 * (n + 1))
                emu.mem_write(buf, struct.pack(f"<{n + 1}f", *values, 1e9))
                for k, at_k in enumerate((buf, buf + 4 * n, buf + 4 * n)):
                    emu.write_u32(prim + base + 4 * k, at_k)
                sets[base] = (n, [bits(f32(v)) for v in values])
            w(0xBC, 1)
            w(0x138, 0)
    elif pid == 8:
        for f, lo, hi in ((0x21C, -3, 3), (0x220, -0.2, 0.2), (0x264, -3, 3), (0x270, -0.2, 0.2),
                          (0x240, -10, 10)):
            wf(f, rng.uniform(lo, hi))
        axis = [rng.uniform(-1, 1) for _ in range(3)]
        for i, v in enumerate(axis):
            wf(0x1C0 + 4 * i, v)
    elif pid in (0x26, 0x2C, 0x31):
        rec = bytearray(RING_SIZE)
        struct.pack_into("<Bxxxf", rec, 0, rng.random() < 0.85, f32(rng.uniform(0.05, 3)))
        struct.pack_into("<4f", rec, 0x14, *(f32(rng.choice([float(rng.randrange(0, 360)), rng.uniform(0, 359)]))
                                             for _ in range(4)))
        struct.pack_into("<B", rec, 0x70, rng.choice([0, 1]))
        at = emu.read_u32(owner + 8)
        struct.pack_into("<3f", rec, 0xA0, f32(rng.uniform(-40, 40)),
                         f32(struct.unpack("<f", struct.pack("<I", at))[0] - rng.uniform(-2, 40)),
                         f32(rng.uniform(-40, 40)))
        emu.mem_write(prim + RING, bytes(rec))
        for i in range(0, RING_SIZE, 4):
            v = emu.read_u32(prim + RING + i)
            if v:
                sets[RING + i] = v
    wf(0x248, rng.uniform(0.3, 2.0))
    wf(0x24C, rng.uniform(-0.05, 0.05))
    wf(0x250, rng.uniform(-0.01, 0.01))
    wf(0x38, rng.uniform(0, 360))
    w(0x24, rng.randrange(0, len(ACTIONS)))
    w(0x28, rng.randrange(0, len(ACTIONS[emu.read_u32(prim + 0x24)])))
    w(0x128, rng.choice([1, 1, 0x101, 0x201]))
    w(0x154, rng.randrange(1, 13))
    if pid in (8, 0x26, 0x2C, 0x31):
        # Their updates' answers are ignored, so an action that does not loop would run on
        # past its frames.
        w(0x1B4, 1)
    emu.write_u32(prim + 0x98, o.spr)
    emu.write_u32(prim + 0x9C, o.act_res)
    emu.call(RESET_HISTORY, this=prim)
    return prim, offset, sets


def synthetic_trails(o, rng, lines):
    """Four primitives of each trail id, stepped and drawn on their own after srand(13000 +
    n), as synthetic3."""
    emu = o.emu
    actor = emu.scratch(size=0x100)
    rows = []
    for n in range(4 * len(TRAIL_IDS)):
        pid = TRAIL_IDS[n % len(TRAIL_IDS)]
        mark = emu.heap_mark()
        effect = o.rag_effect(actor)
        pos = [f32(rng.uniform(-30, 30)), f32(rng.uniform(-8, 0)), f32(rng.uniform(-30, 30))]
        emu.mem_write(effect + 4, struct.pack("<3f", *pos))
        target = [f32(rng.uniform(-30, 30)), 0.0, f32(rng.uniform(-30, 30))]
        emu.mem_write(effect + 0xE8, struct.pack("<3f", *target))
        duration = rng.choice([12, 40, 300])
        emu.write_u32(effect + 0xC0, duration)
        emu.write_u32(effect + 0xBC, 0)
        prim, offset, sets = random_trail(rng, o, effect, pid)
        emu.call(SRAND, 13000 + n)
        view, yaw, slope, pitch = o.scene(rng)
        name = f"kTrail{n}"
        lines.append(f"static const FieldVector {name}Fields[] = {{" + ", ".join(
            f"{{0x{f:x}, 0x{v:08x}, {{}}}}" if isinstance(v, int) else
            f"{{0x{f:x}, {v[0]}, {{{hexs(v[1] + [0] * (4 - len(v[1])))}}}}}" for f, v in sorted(sets.items())) + "};")
        frames = []
        for frame in range(40):
            more = emu.call(PRIM_VF01, this=prim).eax & 0xFF
            state = state_trail(o, prim)
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
    lines.append("static const ShapeVector kTrails[] = {")
    lines += rows
    lines.append("};")


SCREEN_IDS = [3, 4, 0xC, 0x13, 0x14]
# What ids 3, 4, 0xc, 0x13 and 0x14 change: the position, the colour, the key, the flags,
# the age, the fade, the sweep, the alpha's limit, the heading, the width, the alpha and its
# speed, the heading's speed and rate, the radius with its speed and rate, the height with
# its speed and rate, the bottom radius and its speed, the speed, the pitch, the step, the
# start and the travel.
SCREEN_STATE = [0x4, 0x8, 0xC, 0x124, 0x138, 0x13C, 0x140, 0xC0, 0x1B8, 0x204, 0x23C, 0x248, 0x254, 0x258,
                0x260, 0x26C, 0x278, 0x27C, 0x280, 0x290, 0x294, 0x298, 0x224, 0x22C, 0x1EC, 0x240,
                0x2E0, 0x2E4, 0x2E8, 0x2EC, 0x2F0, 0x2F4, 0x2F8, 0x2FC, 0x300]


def screen_state(o, prim):
    """SCREEN_STATE, the matrix +0xc4 and the history records."""
    emu = o.emu
    n = emu.read_u32(prim + 0x154)
    return ([emu.read_u32(prim + f) for f in SCREEN_STATE] +
            list(struct.unpack("<12I", emu.mem_read(prim + 0xC4, 48))) +
            list(struct.unpack(f"<{35 * n}I", emu.mem_read(prim + 0x358, 0x8C * n))))


def random_screen(rng, o, owner, pid):
    """A primitive of id 3, 4, 0xc, 0x13 or 0x14 with the fields its update and drawing
    read set at random; returns it, its offset and the fields set, as random_prim3."""
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
    w(0x150, rng.choice([-1, 0, rng.randrange(0, life)]))
    w(0x160, rng.choice([-1, rng.randrange(0, 6)]))
    w(0x13C, rng.choice([0, 1, 0x80, 0x81]))
    for f in (0x19C, 0x1A0, 0x1A4):
        w(f, rng.randrange(0, 256))
    wf(0x254, rng.uniform(0, 254))
    wf(0x258, rng.uniform(-15, 15))
    wf(0x204, rng.choice([254.0, 200.0]))
    wf(0x208, rng.choice([0.0, 0.0, 30.0]))
    for f in (0x1CC, 0x1D0, 0x1FC):
        if rng.random() < 0.6:
            wf(f, rng.uniform(-2, 2))
    for i in range(3):
        wf(0x304 + 4 * i, rng.uniform(-30, 30))
    frames = rng.randrange(1, 4)
    w(0x164, frames)
    textures = emu.alloc(4 * frames)
    for i in range(frames):
        emu.write_u32(textures + 4 * i, TEXTURE_BASE + 0x100 + i)
    emu.write_u32(prim + 0xA8, textures)
    if pid in (3, 0x13):
        for f, lo, hi in ((0x23C, -400, 400), (0x260, -8, 8), (0x26C, -0.5, 0.5), (0x1B8, 1, 40),
                          (0x288, -1, 1), (0x294, -4, 4), (0x298, -0.4, 0.4)):
            wf(f, rng.uniform(lo, hi))
        wf(0x290, rng.choice([0.0, rng.uniform(-5, 60), rng.uniform(0, 60)]))
    elif pid == 4:
        for f, lo, hi in ((0x278, 0, 80), (0x27C, -3, 6), (0x280, -0.3, 0.3), (0x224, -10, 40)):
            wf(f, rng.uniform(lo, hi))
        wf(0x1B8, rng.choice([10.0, 22.5, 30.0, 36.0, 45.0, 90.0, 400.0]))
    elif pid == 0xC:
        for f, lo, hi in ((0x23C, -200, 200), (0x240, -200, 200), (0x260, -8, 8), (0x26C, -0.5, 0.5),
                          (0x1EC, -3, 3), (0x1F0, -0.3, 0.3), (0x278, 0, 20), (0x27C, -2, 2), (0x280, -0.3, 0.3),
                          (0x224, -10, 30), (0x22C, -1, 1), (0x234, -0.1, 0.1)):
            wf(f, rng.uniform(lo, hi))
        # 36 and 50 leave u partway round, to run on into the next record.
        wf(0x1B8, rng.choice([36.0, 50.0, 22.5, 36.0, 90.0]))
        matrix = [rng.uniform(-1, 1) for _ in range(9)] + [rng.uniform(-0.5, 0.5) for _ in range(3)]
        for i, v in enumerate(matrix):
            wf(0xC4 + 4 * i, v)
        w(0x154, rng.randrange(2, 7))
        w(0x128, rng.choice([1, 0x201, 5, 0x105]))
    emu.call(RESET_HISTORY, this=prim)
    return prim, offset, sets


def synthetic_screens(o, rng, lines):
    """Four primitives of each of SCREEN_IDS, stepped and drawn on their own after
    srand(15000 + n) under a moving point on screen, as synthetic; the triangles every
    frame, the rings' every third."""
    emu = o.emu
    actor = emu.scratch(size=0x100)
    rows = []
    for n in range(4 * len(SCREEN_IDS)):
        pid = SCREEN_IDS[n % len(SCREEN_IDS)]
        mark = emu.heap_mark()
        effect = o.rag_effect(actor)
        pos = [f32(rng.uniform(-30, 30)), f32(rng.uniform(-8, 0)), f32(rng.uniform(-30, 30))]
        emu.mem_write(effect + 4, struct.pack("<3f", *pos))
        duration = rng.choice([12, 40, 300])
        emu.write_u32(effect + 0xC0, duration)
        emu.write_u32(effect + 0xBC, 0)
        prim, offset, sets = random_screen(rng, o, effect, pid)
        emu.call(SRAND, 15000 + n)
        view, yaw, slope, pitch = o.scene(rng)
        name = f"kScreen{n}"
        lines.append(f"static const FieldVector {name}Fields[] = {{" + ", ".join(
            f"{{0x{f:x}, 0x{v:08x}, {{}}}}" for f, v in sorted(sets.items())) + "};")
        frames = []
        for frame in range(40):
            sx, sy = f32(rng.uniform(100, 700)), f32(rng.uniform(100, 500))
            emu.mem_write(effect + 0xDC, struct.pack("<2f", sx, sy))
            more = emu.call(PRIM_VF01, this=prim).eax & 0xFF
            state = screen_state(o, prim)
            added = o.draw(prim) if more else []
            lines.append(f"static const std::uint32_t {name}Frame{frame}State[] = {{{hexs(state)}}};")
            if pid not in (4, 0xC) or frame < 3 or frame % 3 == 0 or not more:
                lines.append(f"static const TriangleVector {name}Frame{frame}Triangles[] = {triangles_cpp(added)};")
                triangles = f"{name}Frame{frame}Triangles, {len(added)}"
            else:
                triangles = "nullptr, kUnchecked"
            frames.append(f"    {{{{{hexs([bits(sx), bits(sy)])}}}, {more}, {name}Frame{frame}State, {len(state)}, "
                          f"{triangles}}},")
            emu.write_u32(effect + 0xBC, emu.read_u32(effect + 0xBC) + 1)
            if not more:
                break
        lines.append(f"static const ScreenFrameVector {name}Frames[] = {{")
        lines += frames
        lines.append("};")
        rows.append(f"    {{{pid}, {{{hexs(map(bits, pos))}}}, {{{hexs(map(bits, offset))}}}, {duration}, "
                    f"{scene_cpp(view, yaw, slope, pitch)}, {name}Fields, {len(sets)}, {name}Frames, {len(frames)}}},")
        emu.heap_release(mark)
    lines.append("static const ScreenPrimVector kScreens[] = {")
    lines += rows
    lines.append("};")


FLIGHT_IDS = [0xF, 0x16]
# What the flying ids change: the position, image, colour, period (the v's tenths), flags,
# age, fade, changed and reached, the colour count and colours, the speed and acceleration,
# heading, pitch and angle, the alpha and its speed, the heading's and pitch's speeds, the
# width with its speed, the height with its speed and rate, v at the top and bottom, the
# step, start and travel, the twelve changes and max_height.
FLIGHT_STATE = [0x4, 0x8, 0xC, 0x28, 0x124, 0x134, 0x13C, 0x140, 0xC0, 0xA0, 0xB0, 0x15C, 0x19C, 0x1A0,
                0x1A4, 0x1EC, 0x1F0, 0x23C, 0x240, 0x244, 0x254, 0x258, 0x260, 0x264, 0x284, 0x288, 0x290,
                0x294, 0x298, 0x214, 0x218, 0x2E0, 0x2E4, 0x2E8, 0x2EC, 0x2F0, 0x2F4, 0x2F8, 0x2FC, 0x300,
                0x1CC, 0x1D0, 0x1D4, 0x1D8, 0x1DC, 0x1E0, 0x1E4, 0x1E8, 0x1F4, 0x1F8, 0x1FC, 0x200, 0x31C,
                0x320, 0x324]


def flight_state(o, prim):
    """FLIGHT_STATE, the second matrix +0xf4, and the owner's target and landed flag."""
    emu = o.emu
    owner = emu.read_u32(prim + 0xA4)
    tail = (list(struct.unpack("<3I", emu.mem_read(owner + 0xE8, 12))) + [emu.read_u32(owner + 0xB4)]
            if owner else [0, 0, 0, 0])
    return ([emu.read_u32(prim + f) for f in FLIGHT_STATE] +
            list(struct.unpack("<12I", emu.mem_read(prim + 0xF4, 48))) + tail)


def random_flight(rng, o, owner, pid):
    """A primitive of id 0xf, 0x10 or 0x16 with the fields its update and drawing read set at
    random; returns it, its offset and the fields set, as random_prim3."""
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
    w(0x150, rng.choice([-1, 0, rng.randrange(0, life)]))
    w(0x160, rng.choice([-1, rng.randrange(0, 6)]))
    flags = 0
    for bit in (2, 4, 8, 0x10, 0x20, 0x40, 0x80, 0x100, 0x200, 0x400, 0x800):
        if rng.random() < (0.15 if bit in (0x400, 0x200) else 0.3):
            flags |= bit
    w(0x13C, flags)
    w(0x12C, rng.randrange(1, 9))
    w(0x194, rng.choice([0, 0, rng.randrange(1, 5)]))
    w(0x134, rng.randrange(0, 10))
    cycle = flags & 0x800
    for f in (0x19C, 0x1A0, 0x1A4):
        w(f, rng.choice([0xFE, rng.randrange(-20, 280)] if cycle else [-3, 254, rng.randrange(-20, 280)]))
    for f in (0x1A8, 0x1AC, 0x1B0):
        w(f, rng.choice([0, 0, 9, -13, 40]))
    wf(0x254, rng.uniform(0, 254))
    wf(0x258, rng.uniform(-15, 15))
    wf(0x204, rng.choice([254.0, 200.0]))
    for f in (0x1CC, 0x1D0, 0x1D4, 0x1D8, 0x1DC, 0x1E0, 0x1E4, 0x1E8, 0x1F4, 0x1F8, 0x1FC, 0x200):
        if rng.random() < 0.4:
            wf(f, rng.uniform(-2, 2))
    for f, lo, hi in ((0x1EC, -3, 3), (0x1F0, -0.3, 0.3), (0x23C, -200, 200), (0x240, -200, 200),
                      (0x244, -200, 200), (0x260, -8, 8), (0x26C, -0.5, 0.5), (0x264, -8, 8), (0x270, -0.5, 0.5),
                      (0x284, 0, 20), (0x288, -1, 1), (0x28C, -0.1, 0.1), (0x290, -5, 30), (0x294, -2, 2),
                      (0x298, -0.2, 0.2), (0x38, 0, 360), (0x2E0, -2, 2), (0x2E4, -2, 2), (0x2E8, -2, 2),
                      (0x31C, -0.3, 0.3), (0x320, -0.3, 0.3), (0x324, -0.3, 0.3)):
        wf(f, rng.uniform(lo, hi))
    wf(0x29C, rng.choice([100.0, 30.0, 12.0]))
    for i in range(3):
        wf(0x304 + 4 * i, rng.uniform(-15, 15))
    matrix = [rng.uniform(-1, 1) for _ in range(9)] + [rng.uniform(-0.5, 0.5) for _ in range(3)]
    for i, v in enumerate(matrix):
        wf(0xC4 + 4 * i, v)
    frames = rng.randrange(1, 4)
    w(0x164, frames)
    w(0x148, rng.choice([1, 2, 3]))
    # vf01 ignores whether 0xf's and 0x10's images ran out, so one that does not loop would
    # read past its textures.
    w(0x1B4, 1 if pid in (0xF, 0x10) else rng.choice([0, 1, 1]))
    textures = emu.alloc(4 * frames)
    for i in range(frames):
        emu.write_u32(textures + 4 * i, TEXTURE_BASE + 0x100 + i)
    emu.write_u32(prim + 0xA8, textures)
    w(0x128, rng.choice([1, 3, 0x201, 0x203]))
    return prim, offset, sets


def synthetic_flights(o, rng, lines, ids=FLIGHT_IDS, seed=17000, prefix="kFlight", table="kFlights",
                      make=None, state_of=None, sampled=None):
    """Ten primitives of each of ids, stepped and drawn on their own after srand(seed + n),
    as synthetic3, with a target near the owner and the ground below; make and state_of
    stand in for random_flight and flight_state, and sampled, when given, names the frames
    whose triangles are recorded besides the last."""
    emu = o.emu
    actor = emu.scratch(size=0x100)
    rows = []
    for n in range(10 * len(ids)):
        pid = ids[n % len(ids)]
        mark = emu.heap_mark()
        effect = o.rag_effect(actor)
        pos = [f32(rng.uniform(-30, 30)), f32(rng.uniform(-8, 0)), f32(rng.uniform(-30, 30))]
        emu.mem_write(effect + 4, struct.pack("<3f", *pos))
        # Some targets within 2, which kSeeksTarget reaches at once.
        near = rng.choice([12, 12, 2])
        target = [f32(pos[0] + rng.uniform(-near, near)), 0.0, f32(pos[2] + rng.uniform(-near, near))]
        emu.mem_write(effect + 0xE8, struct.pack("<3f", *target))
        duration = rng.choice([12, 40, 300])
        emu.write_u32(effect + 0xC0, duration)
        emu.write_u32(effect + 0xBC, 0)
        prim, offset, sets = (make or random_flight)(rng, o, effect, pid)
        emu.call(SRAND, seed + n)
        view, yaw, slope, pitch = o.scene(rng)
        name = f"{prefix}{n}"
        lines.append(f"static const FieldVector {name}Fields[] = {{" + ", ".join(
            f"{{0x{f:x}, 0x{v:08x}, {{}}}}" for f, v in sorted(sets.items())) + "};")
        frames = []
        for frame in range(40):
            more = emu.call(PRIM_VF01, this=prim).eax & 0xFF
            state = (state_of or flight_state)(o, prim)
            added = o.draw(prim) if more else []
            lines.append(f"static const std::uint32_t {name}Frame{frame}State[] = {{{hexs(state)}}};")
            if sampled is None or frame in sampled or not more or frame == 39:
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
        rows.append(f"    {{{pid}, {{{hexs(map(bits, pos))}}}, {{{hexs(map(bits, target))}}}, "
                    f"{{{hexs(map(bits, offset))}}}, {duration}, {scene_cpp(view, yaw, slope, pitch)}, {name}Fields, "
                    f"{len(sets)}, {name}Frames, {len(frames)}}},")
        emu.heap_release(mark)
    lines.append(f"static const ShapeVector {table}[] = {{")
    lines += rows
    lines.append("};")


SOLID_IDS = [9, 0xE, 0x11, 0x12]
# What the pyramid, sphere and 0x12's updates change: the position, image, colour, age,
# fade, colours, speed and acceleration, heading and pitch, alpha and its speed, the
# heading's speed, the distance with its speed and rate, the radii and their speeds, the
# height and its speed, and the step, start and travel.
SOLID_STATE = [0x4, 0x8, 0xC, 0x28, 0x124, 0x140, 0xC0, 0x19C, 0x1A0, 0x1A4, 0x1EC, 0x1F0, 0x23C, 0x240,
               0x254, 0x258, 0x260, 0x278, 0x27C, 0x280, 0x224, 0x228, 0x22C, 0x230, 0x290, 0x294, 0x2E0,
               0x2E4, 0x2E8, 0x2EC, 0x2F0, 0x2F4, 0x2F8, 0x2FC, 0x300]


def solid_state(o, prim):
    """SOLID_STATE and the matrix +0xc4."""
    emu = o.emu
    return [emu.read_u32(prim + f) for f in SOLID_STATE] + list(struct.unpack("<12I", emu.mem_read(prim + 0xC4, 48)))


def random_solid(rng, o, owner, pid):
    """A primitive of SOLID_IDS with the fields its update and drawing read set at random;
    returns it, its offset and the fields set, as random_flight. Sweeps stay at 20 or more,
    where a sphere's triangles fit the primitive's buffer (+0x1eb0, 512)."""
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

    life = rng.randrange(8, 46)
    w(0x144, life)
    w(0x150, rng.choice([-1, 0, rng.randrange(0, life)]))
    w(0x160, rng.choice([-1, rng.randrange(0, 6)]))
    w(0x13C, rng.choice([0, 1, 2, 3]))
    w(0x134, rng.randrange(0, 10))
    for f in (0x19C, 0x1A0, 0x1A4):
        w(f, rng.choice([-3, 254, rng.randrange(-20, 280)]))
    for f in (0x1A8, 0x1AC, 0x1B0):
        w(f, rng.choice([0, 0, 9, -13, 40]))
    wf(0x254, rng.uniform(0, 254))
    wf(0x258, rng.uniform(-15, 15))
    wf(0x204, rng.choice([254.0, 200.0]))
    for f, lo, hi in ((0x1CC, -2, 2), (0x1D0, -0.2, 0.2), (0x1D8, -2, 2), (0x1E4, -0.2, 0.2), (0x1EC, -3, 3),
                      (0x1F0, -0.3, 0.3), (0x23C, 0, 360), (0x240, -90, 180), (0x260, -8, 8), (0x26C, -0.5, 0.5),
                      (0x264, -8, 8), (0x270, -0.5, 0.5), (0x248, 0.5, 5), (0x290, -5, 30), (0x294, -2, 2),
                      (0x298, -0.2, 0.2), (0x278, 0, 20), (0x27C, -2, 2), (0x280, -0.2, 0.2), (0x224, 0, 10),
                      (0x228, 0, 10), (0x22C, -1, 1), (0x230, -1, 1), (0x234, -0.1, 0.1), (0x238, -0.1, 0.1)):
        wf(f, rng.uniform(lo, hi))
    wf(0x29C, rng.choice([100.0, 30.0, 12.0]))
    # 26: 1/(360/26) and 26/360 round apart.
    wf(0x1B8, rng.choice([20.0, 22.5, 26.0, 30.0, 36.0, 45.0, 60.0, 90.0, 100.0]))
    for base in (0xC4, 0xF4):
        matrix = [rng.uniform(-1, 1) for _ in range(9)] + [rng.uniform(-0.5, 0.5) for _ in range(3)]
        for i, v in enumerate(matrix):
            wf(base + 4 * i, v)
    frames = rng.randrange(1, 4)
    w(0x164, frames)
    w(0x148, rng.choice([1, 2, 3]))
    # vf01 ignores whether 0x12's images ran out, so one that does not loop would read past
    # its textures.
    w(0x1B4, 1)
    textures = emu.alloc(4 * frames)
    for i in range(frames):
        emu.write_u32(textures + 4 * i, TEXTURE_BASE + 0x100 + i)
    emu.write_u32(prim + 0xA8, textures)
    return prim, offset, sets


# The effects that add pyramids and spheres; 0x1b twice more, once with its target 1.8
# from the actor, so that its first step ends 3.2 from it, just past the 2.5 it stops
# within; 0x7b again with its target within 2.5, where its stones stop; 0x8e for its whole
# 300 frames, until its spikes sink; 0x59 until its last icicles are gone.
SOLID_EFFECTS = [(0x1B, 100), (0x1C, 60), (0x4A, 48), (0x4F, 250), (0x7B, 100), (0x7B, 40, 3, True), (0x84, 40),
                 (0x8E, 300), (0x59, 230), (0x11, 40), (0x3F, 30), (0xAA, 305), (0x1B, 80), (0x1B, 40, 3, True)]


# The tube and panel effects: 0xe8 to the player, to the other actor and to no one, and
# AddPanels' five ids.
SPAN_EFFECTS = [(0xE8, 40, 1, LINK_PLAYER_ID), (0xE8, 40, 1, LINK_ACTOR_ID), (0xE8, 12), (0x115, 40), (0x11F, 40),
                (0x122, 40), (0x124, 40), (0x126, 40)]


def solid_effect_state(o, prim):
    """What the pyramid and sphere effects' primitives are checked by: solid_state for
    SOLID_IDS, screen_state for the rings and state_trail for the rest."""
    pid = o.emu.read_u32(prim + 0x130)
    if pid in SOLID_IDS:
        return solid_state(o, prim)
    return screen_state(o, prim) if pid in SCREEN_IDS else state_trail(o, prim)


RIBBON_IDS = [0xA, 0xB]


def ribbon_state(o, prim):
    """screen_state, the offset and the four points +0x328."""
    emu = o.emu
    return (screen_state(o, prim) + list(struct.unpack("<3I", emu.mem_read(prim + 0x304, 12))) +
            list(struct.unpack("<12I", emu.mem_read(prim + 0x328, 48))))


def random_ribbon(rng, o, owner, pid):
    """A primitive of id 0xa or 0xb with the fields its update and drawing read set at
    random and one to six history records; as random_flight."""
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
    w(0x150, rng.choice([-1, 0, rng.randrange(0, life)]))
    for f in (0x19C, 0x1A0, 0x1A4):
        w(f, rng.choice([-3, 254, rng.randrange(-20, 280)]))
    for f in (0x1A8, 0x1AC, 0x1B0):
        w(f, rng.choice([0, 0, 9, -13, 40]))
    wf(0x254, rng.uniform(0, 254))
    wf(0x258, rng.uniform(-15, 15))
    wf(0x204, rng.choice([254.0, 200.0]))
    for f, lo, hi in ((0x23C, -400, 800), (0x260, -40, 40), (0x278, 0, 20), (0x294, -3, 3), (0x248, -2, 8)):
        wf(f, rng.uniform(lo, hi))
    w(0x154, rng.randrange(1, 7))
    textures = emu.alloc(4)
    emu.write_u32(textures, TEXTURE_BASE + 0x100)
    emu.write_u32(prim + 0xA8, textures)
    emu.call(RESET_HISTORY, this=prim)
    return prim, offset, sets


SPAN_IDS = [0x24, 0x2E]


def random_span(rng, pid):
    """A record as ids 0x24 and 0x2e read it, the bytes they leave alone zero: the tube's
    radius, age around its 15, alpha and the id of the player, the other actor, an unknown
    one or none; the panels' sway, height, alpha, kind and edges."""
    rec = bytearray(RING_SIZE)
    active = rng.random() < 0.9
    if pid == 0x24:
        tag = rng.choice([LINK_PLAYER_ID, LINK_ACTOR_ID, LINK_ACTOR_ID + 0.25, 77, 0, -3])
        struct.pack_into("<BxhfIhh", rec, 0, active, 0, f32(rng.uniform(0.1, 3)),
                         rng.choice([rng.randrange(0, 20), 13, 14, 15]), 0, rng.randrange(-5, 256))
        struct.pack_into("<f", rec, 0x14, f32(tag))
    else:
        struct.pack_into("<BxhfIhh", rec, 0, active, rng.randrange(0, 360), f32(rng.uniform(0, 40)),
                         rng.randrange(0, 50), 0, rng.randrange(-5, 256))
        struct.pack_into("<f", rec, 0x14, f32(rng.choice([0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 1.5])))
    for k in range(4):
        struct.pack_into("<3f", rec, 0x88 + 0xC * k, *random_point(rng))
    return bytes(rec)


def link_actors(o):
    """The player's id and position and LINK_ACTOR_ID's actor, for the tube's runs."""
    emu = o.emu
    emu.write_u32(PLAYER_ID, LINK_PLAYER_ID)
    emu.mem_write(emu.read_u32(o.world + 0x3C) + 4, struct.pack("<3f", *LINK_PLAYER))
    actor = emu.scratch(size=0x40)
    emu.mem_write(actor + 4, struct.pack("<3f", *LINK_ACTOR))
    emu.replace_thiscall(GET_ACTOR_BY_ID, 1, lambda this, aid: actor if aid == LINK_ACTOR_ID else 0)


def flight_effect_state(o, prim):
    """What the flying effects' primitives are checked by: flight_state for FLIGHT_IDS,
    screen_state for the screen and history ids and state_trail for the rest."""
    pid = o.emu.read_u32(prim + 0x130)
    if pid in FLIGHT_IDS or pid == 0x10:
        return flight_state(o, prim)
    return screen_state(o, prim) if pid in SCREEN_IDS else state_trail(o, prim)


def screen_effect_state(o, prim):
    """What the screen effects' primitives are checked by: screen_state for SCREEN_IDS,
    ring_state for the closing rings and state_trail for the rest."""
    pid = o.emu.read_u32(prim + 0x130)
    if pid in SCREEN_IDS:
        return screen_state(o, prim)
    return ring_state(o, prim) if pid == 0x3E else state_trail(o, prim)


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


RING_IDS = [0x17, 0x18, 0x19, 0x1C, 0x1D, 0x22, 0x27, 0x30, 0x36, 0x39, 0x3C, 0x3E]
FLAT_IDS = [0x1A, 0x25, 0x40]


def synthetic_rings(o, rng, lines, ids=RING_IDS, prefix="kRing", per_id=2):
    emu = o.emu
    actor = emu.scratch(size=0x100)
    rows = []
    for n in range(per_id * len(ids)):
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
        name = f"{prefix}{n}"
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
    lines.append(f"static const ShapeVector {prefix}s[] = {{")
    lines += rows
    lines.append("};")


RING_EFFECTS = [0xE1, 0xE7, 0xEC, 0xED, 0xEE, 0xF7, 0xF8, 0x103, 0x104, 0x105, 0x107,
                0xC8, 0xDE, 0xFC, 0xEF, 0xF0, 0xF1, 0x118, 0x11D, 0x130, 0x13A, 0x13B, 0x13C]


FLAT_EFFECTS = [0xC9, 0xF2]


def ring_effects(o, rng, lines, effects=RING_EFFECTS, prefix="kRingEffect", seed=3000):
    emu = o.emu
    rows = []
    for run, eid in enumerate(effects):
        o.reset_world()
        actor = emu.scratch(size=0x100)
        pos = (f32(rng.uniform(-30, 30)), f32(rng.uniform(-10, 0)), f32(rng.uniform(-30, 30)))
        emu.mem_write(actor + 4, struct.pack("<3f", *pos))
        emu.call(SRAND, seed + run)
        effect = o.rag_effect(actor)
        emu.call(INIT_EFFECT, actor, eid, 0, 0, 0, this=effect)
        duration = emu.read_u32(effect + 0xC0)
        alive, prims, frames, scenes = True, [], [], []
        name = f"{prefix}{run}"
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
        rows.append(f"    {{{eid}, 0, {seed + run}, {{{hexs(map(bits, pos))}}}, 0, {duration}, "
                    f"{name}Frames, {len(frames)}, {name}Scenes, {len(scenes)}}},")
    lines.append(f"static const BurstVector {prefix}s[] = {{")
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


MOTE_IDS = [0x1B, 0x1E, 0x21]
# 0x21's drawing writes its motes' x and z back into the record; the drawn triangles check
# them, and the state leaves them out.
MOTE_DRAWN = [0x88, 0x90, 0x94, 0x9C, 0xA0, 0xA8, 0xAC, 0xB4]


def mote_state(o, prim):
    emu = o.emu
    out = [emu.read_u32(prim + f) for f in RING_STATE]
    drawn = emu.read_u32(prim + 0x130) == 0x21
    for k in range(4):
        words = list(struct.unpack(f"<{RING_SIZE // 4}I", emu.mem_read(prim + RING + RING_SIZE * k, RING_SIZE)))
        out += [0 if drawn and 4 * i in MOTE_DRAWN else w for i, w in enumerate(words)]
    return out


PIECE_IDS = [0x29, 0x2A, 0x32, 0x34, 0x35, 0x41]


def piece_state(o, prim):
    """As mote_state, leaving out the corners 0x41's drawing writes at +0x88 to +0xb4."""
    emu = o.emu
    out = [emu.read_u32(prim + f) for f in RING_STATE]
    drawn = emu.read_u32(prim + 0x130) == 0x41
    for k in range(4):
        words = list(struct.unpack(f"<{RING_SIZE // 4}I", emu.mem_read(prim + RING + RING_SIZE * k, RING_SIZE)))
        out += [0 if drawn and 4 * i >= 0x88 else w for i, w in enumerate(words)]
    return out


WALL_IDS = [0x20, 0x23, 0x3A, 0x3B, 0x3D, 0x3F, 0x42, 0x43]
# The drawings that write their corners into the record at +0x88 to +0xb4.
WALL_DRAWN = [0x20, 0x23, 0x3B, 0x3D]


def wall_state(o, prim):
    """As piece_state, leaving out the corners the drawings of WALL_DRAWN write."""
    emu = o.emu
    out = [emu.read_u32(prim + f) for f in RING_STATE]
    drawn = emu.read_u32(prim + 0x130) in WALL_DRAWN
    for k in range(4):
        words = list(struct.unpack(f"<{RING_SIZE // 4}I", emu.mem_read(prim + RING + RING_SIZE * k, RING_SIZE)))
        out += [0 if drawn and 4 * i >= 0x88 else w for i, w in enumerate(words)]
    return out


def prim_state(o, prim):
    pid = o.emu.read_u32(prim + 0x130)
    if pid in SQUARE_IDS:
        return square_state(o, prim)
    if pid in WALL_IDS:
        return wall_state(o, prim)
    if pid in PIECE_IDS:
        return piece_state(o, prim)
    return mote_state(o, prim) if pid in MOTE_IDS else ring_state(o, prim)


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


def random_motes(rng, pid):
    rec = bytearray(RING_SIZE)
    struct.pack_into("<Bxxxfi", rec, 0, rng.random() < 0.85, f32(rng.uniform(0.05, 2)), rng.randrange(0, 250))
    struct.pack_into("<h", rec, 0xE, rng.randrange(0, 256))
    if pid == 0x1B:
        values = [rng.uniform(0, 359) for _ in range(16)]
    elif pid == 0x1E:
        values = [rng.uniform(0, 359) for _ in range(6)] + [rng.uniform(0, 359.9), rng.uniform(-20, 0),
                                                             rng.uniform(0, 359.9)]
    else:
        values = []
        for _ in range(4):
            values += [rng.choice([0, 3, 9, 10, 11, rng.uniform(0, 12)]), rng.uniform(0, 300),
                       rng.choice([0, 1, 2, 3, 4]), rng.choice([rng.randrange(0, 360), rng.uniform(0, 359.9)])]
    struct.pack_into(f"<{len(values)}f", rec, 0x14, *(f32(v) for v in values))
    struct.pack_into("<f", rec, 0x6C, f32(rng.uniform(0.5, 10)))
    for k in range(4):
        struct.pack_into("<3f", rec, 0x88 + 0xC * k, f32(rng.uniform(-10, 10)), f32(rng.uniform(-40, 60)),
                         f32(rng.uniform(-10, 10)))
    return bytes(rec)


def random_piece(rng, pid):
    """A record as ids 0x29, 0x2a, 0x32, 0x34, 0x35 and 0x41 read it, the bytes they leave
    alone zero."""
    rec = bytearray(RING_SIZE)
    age = {0x29: rng.randrange(-3, 40), 0x2A: rng.randrange(-3, 30), 0x34: rng.randrange(-2, 30),
           0x35: rng.randrange(-2, 30),
           0x32: rng.choice([rng.randrange(-3, 10), rng.randrange(150, 160), rng.randrange(155, 245),
                             rng.randrange(1990, 2001)]),
           0x41: rng.choice([rng.randrange(-3, 25), rng.randrange(40, 60), rng.randrange(130, 150)])}[pid]
    turn = rng.randrange(0, 400 if pid == 0x29 else 360)
    struct.pack_into("<Bxhfihhh", rec, 0, rng.random() < 0.9, rng.randrange(0, 360), f32(rng.uniform(0.5, 6)), age,
                     turn, rng.randrange(0, 256), rng.randrange(0, 360))
    growth = {0x35: rng.uniform(-25, 25), 0x41: rng.uniform(-30, 30)}.get(pid, rng.uniform(-1, 4))
    if pid == 0x29:
        values = [rng.uniform(1, 6), rng.uniform(0, 359.9)]
    elif pid == 0x2A:
        values = [rng.uniform(0.3, 2)]
    elif pid == 0x41:
        values = [rng.choice([0, rng.uniform(0, 260)]) for _ in range(10)]
    else:
        values = []
    struct.pack_into(f"<{1 + len(values)}f", rec, 0x14, *(f32(v) for v in [growth] + values))
    distance = {0x32: rng.uniform(0.3, 1), 0x41: rng.uniform(5, 25), 0x34: rng.uniform(20, 120),
                0x35: rng.uniform(20, 120)}.get(pid, rng.uniform(-10, 60))
    struct.pack_into("<hxxf", rec, 0x68, rng.randrange(0, 360), f32(distance))
    color = {0x29: rng.choice([0, 1]), 0x2A: rng.choice([0, 1, 2]), 0x32: rng.randrange(0, 6),
             0x41: rng.randrange(0, 3)}.get(pid, 0)
    length = {0x2A: rng.choice([24, 16, -5, 0]), 0x32: rng.choice([0, 1, 1, 2])}.get(pid, 0)
    delay = rng.choice([0, 20, 40, 60, -10]) if pid == 0x32 else 0
    image = rng.randrange(0, 3) if pid == 0x32 else 0
    struct.pack_into("<BbbB", rec, 0x70, color, length, delay, image)
    for k in range(4):
        struct.pack_into("<3f", rec, 0x88 + 0xC * k, f32(rng.uniform(-40, 40)), f32(rng.uniform(-60, 10)),
                         f32(rng.uniform(-40, 40)))
    return bytes(rec)


def random_point(rng):
    return (f32(rng.uniform(-40, 40)), f32(rng.uniform(-60, 10)), f32(rng.uniform(-40, 40)))


def random_wall(rng, pid):
    """A record as ids 0x20, 0x23, 0x3a, 0x3b, 0x3d, 0x3f, 0x42 and 0x43 read it, the bytes
    they leave alone zero; the angles stay inside the tables the drawings index."""
    rec = bytearray(RING_SIZE)
    active = rng.random() < 0.9
    if pid in (0x20, 0x3A, 0x3B):
        # 0x20's third ring leans 90 less each wall's angle: within the first quarter, every
        # ring's leans stay inside the tables.
        span = rng.choice([90, 90, 45, 9, 27, 18] if pid == 0x20 else [360, 360, 30, 90, 120, 21, 50])
        height = {0x20: rng.uniform(20, 130), 0x3A: rng.uniform(5, 60)}.get(pid, rng.uniform(0, 10))
        age = {0x20: rng.choice([rng.randrange(0, 40), 9, 10, 30, 31, 95]),
               0x3A: rng.choice([rng.randrange(0, 120), 9, 10, 89, 90, 91])}.get(
            pid, rng.choice([rng.randrange(-3, 20), rng.randrange(1395, 1405)]))
        angle = rng.randrange(0, {0x20: 91 - span, 0x3A: 700, 0x3B: 400}[pid])
        kind = rng.choice([0, 5, 10, 50, 1400, 30000]) if pid == 0x3A else 0
        struct.pack_into("<BxhfIhhh", rec, 0, active, span, f32(height), age & 0xFFFFFFFF, angle,
                         rng.randrange(-5, 256), kind)
        struct.pack_into("<21f", rec, 0x14, *(f32(rng.uniform(-5, 40)) for _ in range(21)))
        struct.pack_into("<hxxf", rec, 0x68, rng.randrange(20, 120), f32(rng.uniform(0.5, 25)))
        if pid == 0x20:
            struct.pack_into("<bb", rec, 0x70, rng.choice([23, -23, 0, 100, -100]), rng.choice([23, -23, 0, 7]))
        if pid == 0x3A:
            struct.pack_into("<3f", rec, 0x88, *random_point(rng))
            # An orbit of 0 leaves the centre's x and z +0 or −0 by the angle's cosine and sine.
            struct.pack_into("<f", rec, 0xAC, f32(rng.choice([0.0, rng.uniform(0, 30)])))
        return bytes(rec)
    age = {0x23: rng.choice([rng.randrange(0, 40), 9, 10, 95]), 0x3D: rng.randrange(-3, 100),
           0x3F: rng.randrange(-50, 800), 0x42: rng.randrange(-5, 30), 0x43: rng.randrange(-5, 60)}[pid]
    phase = rng.randrange(-300, 400) if pid == 0x3D else 0
    size = {0x23: rng.uniform(10, 80), 0x3D: rng.uniform(5, 60), 0x3F: rng.uniform(0.5, 6),
            0x42: rng.uniform(0.1, 2)}.get(pid, rng.uniform(0.05, 1))
    angle = rng.randrange(0, 400 if pid == 0x3F else 360)
    fade_at = rng.randrange(0, 120) if pid == 0x3D else 0
    struct.pack_into("<BxhfIhhh", rec, 0, active, phase, f32(size), age & 0xFFFFFFFF, angle,
                     rng.randrange(-5, 256), fade_at)
    if pid in (0x23, 0x3D):
        values = [rng.uniform(1, 30), rng.uniform(1, 30), rng.uniform(-10, 60),
                  rng.choice([0.0, 1.0, 0.0, 1.0, -0.0]) if pid == 0x3D else rng.uniform(-5, 5)]
    elif pid == 0x3F:
        values = [rng.uniform(0.5, 6)]
    elif pid == 0x42:
        values = [rng.uniform(0, 15), rng.uniform(0, 15)]
    else:
        values = [rng.choice([0.0, rng.uniform(0, 180), rng.randrange(0, 181)]) for _ in range(21)]
    struct.pack_into(f"<{len(values)}f", rec, 0x14, *(f32(v) for v in values))
    struct.pack_into("<hxxf", rec, 0x68, rng.randrange(0, 360) if pid == 0x43 else 0,
                     f32(rng.uniform(2, 14) if pid == 0x43 else rng.uniform(5, 40) if pid == 0x42 else 0))
    if pid == 0x43:
        struct.pack_into("<21b", rec, 0x70, *(rng.randrange(-2, 8) for _ in range(21)))
    if pid in (0x3F, 0x42, 0x43):
        for k in range(4):
            struct.pack_into("<3f", rec, 0x88 + 0xC * k, *random_point(rng))
    return bytes(rec)


def synthetic_records(o, rng, lines, ids, prefix, seed, make, state_of, frame_count=12, checked=(0, 6, 11)):
    """Primitives of the ids with random records (make), each stepped and drawn on its own
    after srand(seed + n); the states as state_of reads them."""
    emu = o.emu
    actor = emu.scratch(size=0x100)
    rows = []
    for n in range(4 * len(ids)):
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
        w(0x128, rng.choice([5, 5, 1, 0x105]))
        ring_textures(o, prim, 5)
        for k in range(4):
            base = RING + RING_SIZE * k
            emu.mem_write(prim + base, make(rng, pid))
            for i in range(0, RING_SIZE, 4):
                v = emu.read_u32(prim + base + i)
                if v:
                    sets[base + i] = v
        view, yaw, slope, pitch = o.scene(rng)
        name = f"{prefix}{n}"
        lines.append(f"static const FieldVector {name}Fields[] = {{" + ", ".join(
            f"{{0x{f:x}, 0x{v:08x}, {{}}}}" for f, v in sorted(sets.items())) + "};")
        frames = []
        emu.call(SRAND, seed + n)
        for frame in range(frame_count):
            more = emu.call(PRIM_VF01, this=prim).eax & 0xFF
            state = state_of(o, prim)
            added = o.draw(prim) if more else []
            lines.append(f"static const std::uint32_t {name}Frame{frame}State[] = {{{hexs(state)}}};")
            if frame in checked:
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
    lines.append(f"static const ShapeVector {prefix}s[] = {{")
    lines += rows
    lines.append("};")


MOTE_EFFECTS = [(0xCA, 32), (0xE4, 32), (0xCB, 32), (0xFE, 32)]
TRAIL_EFFECTS = [(7, 48), (0xE, 40), (0xF, 64), (0x47, 40), (0x48, 48), (100, 40), (0x56, 100), (0x75, 48),
                 (0xD8, 64), (0xD9, 64), (0xA2, 32), (0xA3, 40), (0x14D, 40), (0x10B, 40), (0x12E, 40),
                 (0x10B, 16), (0x10B, 16), (0x10B, 16), (0x10B, 16), (0x10B, 16), (0x10B, 16), (0x10B, 16)]
# The snow about the player, the leaves 200 above the actor and 0x10b's sprite at the target
# are out of the usual views.
TRAIL_LOOKS = {0xA2: lambda pos, player, target: (player[0], player[1] - 60, player[2]),
               0xA3: lambda pos, player, target: (pos[0], pos[1] - 190, pos[2]),
               0x14D: lambda pos, player, target: (pos[0], pos[1] - 190, pos[2]),
               0x10B: lambda pos, player, target: target}
WALL_EFFECTS = [(0xE2, 112), (0xE0, 56), (0x13D, 56), (0x13E, 40), (0x13F, 40), (0x133, 32), (0x142, 32),
                (0x143, 32), (0x144, 32), (0x149, 48), (0x14A, 80)]
# The effects with screen and history rings, 0x3d past its restart at 145.
SCREEN_EFFECTS = [(8, 48), (0xC, 40), (0x38, 32), (0x3A, 40), (0x3B, 32), (0x2A, 40), (0x10, 48), (0x23, 40),
                  (0x24, 40), (0x3D, 152), (0x3E, 48), (0x49, 56), (0x62, 48), (0x79, 40), (0x80, 48), (0xD7, 48),
                  (0x28, 72), (0x141, 40)]
# The effects with flying quads and billboards: 0x52 past its square at frame 50,
# the flasher past its second key at 100, and 0x9f again at level 11, tinted green.
# The effects that add crossed quads, the arrows once more at levels 5 and 10 (where ten
# times the level first differs from nine times), blitz beat at 2, and Concentration past
# its end at frame 151.
CROSSED_EFFECTS = [(9, 100), (0x2B, 110), (0x25, 110), (0x26, 110), (0x1A, 170), (0x1F, 170), (0x1A, 170, 5),
                   (0x1F, 170, 5), (0x73, 72), (0x73, 72, 2), (0xDC, 160), (0x1A, 170, 10)]
FLIGHT_EFFECTS = [(0xD, 40), (0x72, 40), (0x89, 40), (0x30, 48), (0x3C, 40), (0x9F, 24), (0xA0, 24),
                  (0x52, 72), (0x5D, 30), (0x5E, 48), (99, 104), (0x74, 40), (0x76, 40), (0x77, 64),
                  (0xA1, 16), (0x9F, 12, 11)]
PIECE_EFFECTS = [(0xE3, 104), (0x106, 40), (0x10A, 40), (0x114, 40), (0x129, 40), (300, 32), (0x12F, 112),
                 (0x148, 64)]


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


def square_effects(o, rng, lines, effects=SQUARE_EFFECTS, prefix="kSquareEffect", seed=5000, state_of=None,
                   with_quads=False, looks=None, release=False, messages=False, dense=False, facings=None):
    """The effects through the world's list with a target, a level and the player's
    position; looks maps an effect id to where its views look, from the actor, the player
    and the target; release gives the heap back after each run; messages gives the actor a
    vf02 that ignores them (CEffectPrim's), for effects that send it some; dense (True, or
    the effect ids it applies to) records the first four frames, then every fourth state and
    every eighth drawing, however many primitives there are. An entry of effects may fix the
    level as a third value, and with a fourth put the target within 2.5 of the actor (True)
    or give its y (a number, the actor id 0xe8 runs to). facings maps a run's index to the
    actor's facing (+0x38), 0 otherwise."""
    emu = o.emu
    rows = []
    player = emu.read_u32(o.world + 0x3C)
    for run, entry in enumerate(effects):
        eid, count = entry[:2]
        mark = emu.heap_mark()
        o.reset_world()
        actor = emu.scratch(size=0x100)
        if messages:
            emu.write_u32(actor, emu.scratch(struct.pack("<3I", 0, 0, PRIM_VF02)))
        pos = (f32(rng.uniform(-30, 30)), f32(rng.uniform(-10, 0)), f32(rng.uniform(-30, 30)))
        emu.mem_write(actor + 4, struct.pack("<3f", *pos))
        if facings and run in facings:
            emu.mem_write(actor + 0x38, struct.pack("<f", facings[run]))
        # 0xe5's clouds show only while the player is above their floor (y -152).
        at = (f32(rng.uniform(-30, 30)), f32(rng.uniform(-200, -160) if eid == 0xE5 else rng.uniform(-200, 0)),
              f32(rng.uniform(-30, 30)))
        emu.mem_write(player + 4, struct.pack("<3f", *at))
        target = toward(rng, pos, 30, 90)
        if eid == 0xE4:
            # As many motes as the target's x rounds up to.
            target = (f32(rng.uniform(1.5, 4.5)), target[1], target[2])
        level = rng.randrange(1, 6)
        if len(entry) > 2:
            level = entry[2]
        if len(entry) > 3 and entry[3] is True:
            target = (f32(pos[0] + 1.5), target[1], f32(pos[2] - 1.0))
        elif len(entry) > 3:
            target = (target[0], f32(entry[3]), target[2])
        emu.call(SRAND, seed + run)
        effect = o.rag_effect(actor)
        emu.call(INIT_EFFECT, actor, eid, 0, 0, 0, this=effect)
        emu.mem_write(effect + 0xE8, struct.pack("<3f", *target))
        emu.write_u32(effect + 0xB8, level)
        duration = emu.read_u32(effect + 0xC0)
        alive, prims, frames, scenes = True, [], [], []
        name = f"{prefix}{run}"
        look = (looks or {}).get(eid)
        for frame in range(count):
            if frame % 8 == 0:
                scenes.append((frame, *o.scene(rng, look(pos, at, target) if look else None)))
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
            is_dense = dense is True or (dense and eid in dense)
            if is_dense:
                sampled = frame < 4 or frame % 4 == 0 or frame == count - 1
            elif len(objects) <= 2:
                sampled = True
            elif len(objects) <= 8:
                sampled = frame % 8 == 0 or frame in (1, count - 1)
            else:
                sampled = frame in (1, count - 1)
            states, added, quads = [], [], []
            for _, p in objects:
                index = len(states)
                if sampled:
                    state = (state_of or prim_state)(o, p)
                    lines.append(f"static const std::uint32_t {name}Frame{frame}Prim{index}[] = {{{hexs(state)}}};")
                    ref = f"{name}Frame{frame}Prim{index}, {len(state)}"
                else:
                    ref = "nullptr, 0"
                states.append(f"{{{prims.index(p)}, {emu.read_u32(p + 0x130)}, {ref}}}")
                added += o.draw(p)
                quads += o.quads
            lines.append(f"static const BurstPrimVector {name}Frame{frame}Prims[] = {{{', '.join(states) or '{}'}}};")
            if is_dense:
                checked = (frame,) if frame < 4 or frame % 8 == 0 else (count - 1,)
            else:
                checked = (0, 12, count - 1) if len(objects) <= 8 else (count - 1,)
            quad_ref = "nullptr, kUnchecked" if with_quads else "nullptr, 0"
            if frame in checked:
                lines.append(f"static const TriangleVector {name}Frame{frame}Triangles[] = {triangles_cpp(added)};")
                triangles = f"{name}Frame{frame}Triangles, {len(added)}"
                if with_quads:
                    lines.append(f"static const QuadVector {name}Frame{frame}Quads[] = {quads_cpp(quads)};")
                    quad_ref = f"{name}Frame{frame}Quads, {len(quads)}"
            else:
                triangles = "nullptr, kUnchecked"
            frames.append(f"    {{{more}, {name}Frame{frame}Prims, {len(states)}, {triangles}, {quad_ref}}},")
            if not alive and not o.world_objects():
                break
        lines.append(f"static const BurstFrameVector {name}Frames[] = {{")
        lines += frames
        lines.append("};")
        lines.append(f"static const SceneVector {name}Scenes[] = {{")
        lines += [f"    {{{f}, {scene_cpp(v, y, sl, pi)}}}," for f, v, y, sl, pi in scenes]
        lines.append("};")
        rows.append(f"    {{{eid}, {level}, {seed + run}, {{{hexs(map(bits, pos))}}}, {{{hexs(map(bits, at))}}}, "
                    f"{{{hexs(map(bits, target))}}}, {duration}, {name}Frames, {len(frames)}, {name}Scenes, "
                    f"{len(scenes)}}},")
        if release:
            emu.heap_release(mark)
    emu.mem_write(player + 4, bytes(12))
    lines.append(f"static const SquareEffectVector {prefix}s[] = {{")
    lines += rows
    lines.append("};")


# The cases of CRagEffect::vf01 that only play their .str, or nothing, with Concentration's
# columns (0xda, 0xdb and 0xdd); then those ported before that step a .str or send the game
# mode messages.
STR_IDS = (
    0x17, 0x19, 0x1D, 0x1E, 0x29, 0x31, 0x34, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x4B, 0x4C, 0x4D, 0x4E,
    0x53, 0x54, 0x55, 0x57, 0x58, 0x5A, 0x5B, 0x5C, 0x5F, 0x60, 0x61, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B,
    0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x7C, 0x7E, 0x7F, 0x81, 0x82, 0x83, 0x85, 0x86, 0x87, 0x8B, 0x8D, 0x8F,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0xA7, 0xA8, 0xA9,
    0xC7, 0xCC, 0xCD, 0xCE, 0xCF, 0xD0, 0xD1, 0xD2, 0xD3, 0xD5, 0xD6, 0xDA, 0xDB, 0xDD, 0xFA, 0xFB, 0x111,
    0x127, 0x128, 0x12D, 0x131, 0x132, 0x137, 0x14F, 0x150,
    0xD, 0x28, 0x4F, 0x56, 0x59, 0x8E, 0xAA, 0xDC, 0xF8, 0x13B)
# The cylinder effects and the spell circles; 0x7a's runs face 30 and 200 degrees.
CYLINDER_EFFECTS = [(6, 60), (0x22, 64), (0x7A, 16), (0x7A, 16), (0x8A, 70), (0x8C, 240), (0x36, 48),
                    (0x37, 48), (0x39, 48)]
CYLINDER_FACINGS = {2: 30.0, 3: 200.0}


def cylinder_effect_state(o, prim):
    """ring_state for the spell circle (0x17), state_trail for the cylinders."""
    return ring_state(o, prim) if o.emu.read_u32(prim + 0x130) == 0x17 else state_trail(o, prim)


# The particle effects of their own sprites; 0xa5 four times for its picks, 0x2c twice.
PARTICLE_EFFECTS = [(0x2F, 40), (0x27, 40), (0x35, 40), (0xD4, 72), (0xA5, 130), (0xA5, 40), (0xA5, 40),
                    (0xA5, 40), (0x2C, 40), (0x2C, 40), (0x151, 40), (0xAB, 40), (0x14E, 40), (0x14, 48),
                    (0x7D, 48), (0x88, 48)]

# The particle effects of the stock sprites and the fireballs; 0x18 and 0x50 twice.
OTHER_PARTICLE_EFFECTS = [(0x15, 48), (0x18, 56), (0x18, 56), (0x50, 40), (0x50, 40), (0x21, 40), (0x16, 40),
                          (0x2D, 150)]


# Concentration's other columns, as the crossed-quad effects run 0xdc.
COLUMN_EFFECTS = [(0xDA, 70), (0xDB, 70), (0xDD, 70)]
# AdvanceStr's stand-in answers false to every period-th call (never, for 0), as a .str
# that has ended would.
STR_PERIODS = (7, 0)
STR_STEPS = 2600  # past 0x12d's turn back at frame 2300


def str_effects(o, rng, lines, seed=26000):
    """Each of STR_IDS through the world's list, once for each of STR_PERIODS, for up to
    STR_STEPS steps: each step what the effect asks of its .str (AdvanceStr's offsets and
    answer, the real ResetStr on a stand-in .str header) and the messages it sends the game
    mode (C3dGround::vf03, an empty function of four arguments, standing in for its vf05),
    then its frame and duration where they move other than by a step. Steps that ask the
    same are run together."""
    emu = o.emu
    events = []
    calls, period = [0], [0]

    def advance(this, x, y, z):
        n = calls[0]
        calls[0] += 1
        more = int(period[0] == 0 or n % period[0] != period[0] - 1)
        events.append((0 if more else 1, x, y, z, 0))
        return more

    def message(this, m, a, b, c):
        if m == 0x94:
            a, b, c = emu.read_u32(a), emu.read_u32(b), emu.read_u32(c)
        events.append((3, m, a, b, c))
        return 0

    o.advance_str = advance
    emu.uc.hook_add(UC_HOOK_CODE, lambda uc, address, size, data: events.append((2, 0, 0, 0, 0)),
                    begin=RESET_STR, end=RESET_STR)
    emu.replace_thiscall(EMPTY_VF4, 4, message)
    # 0x127 and 0x128 read the game mode straight from g_ModeSwitcher+4 (0057ccc1).
    emu.write_u32(MODE_SWITCHER + 4, o.mode)
    # Three layers, layer 0 with keys (ResetStr reads +8 and +0x388).
    header = emu.scratch(size=0x400)
    emu.write_u32(header + 8, 3)
    emu.write_u32(header + 0x388, 1)
    player = emu.read_u32(o.world + 0x3C)
    rows = []
    run = 0
    for eid in STR_IDS:
        for p in STR_PERIODS:
            mark = emu.heap_mark()
            o.reset_world()
            actor = emu.scratch(size=0x100)
            emu.write_u32(actor, emu.scratch(struct.pack("<3I", 0, 0, PRIM_VF02)))
            pos = (f32(rng.uniform(-30, 30)), f32(rng.uniform(-10, 0)), f32(rng.uniform(-30, 30)))
            emu.mem_write(actor + 4, struct.pack("<3f", *pos))
            at = (f32(rng.uniform(-30, 30)), f32(rng.uniform(-200, 0)), f32(rng.uniform(-30, 30)))
            emu.mem_write(player + 4, struct.pack("<3f", *at))
            target = toward(rng, pos, 30, 90)
            level = rng.randrange(1, 6)
            emu.call(SRAND, seed + run)
            effect = o.rag_effect(actor)
            emu.call(INIT_EFFECT, actor, eid, 0, 0, 0, this=effect)
            emu.mem_write(effect + 0xE8, struct.pack("<3f", *target))
            emu.write_u32(effect + 0xB8, level)
            emu.write_u32(effect + 0x75C, header)
            calls[0], period[0] = 0, p
            frame, duration = 0, emu.read_u32(effect + 0xC0)
            steps, ended = [], False
            for step in range(STR_STEPS):
                del events[:]
                more = emu.call(RAG_EFFECT_VF01, this=effect).eax & 0xFF
                asked = [e for e in events if e[0] < 3] + [e for e in events if e[0] == 3]
                if more:
                    f, d = emu.read_u32(effect + 0xBC), emu.read_u32(effect + 0xC0)
                    if f != frame + 1:
                        asked.append((4, f, 0, 0, 0))
                    if d != duration:
                        asked.append((5, d, 0, 0, 0))
                    frame, duration = f, d
                if steps and steps[-1][2] == asked and steps[-1][1] == step - 1:
                    steps[-1][1] = step
                else:
                    steps.append([step, step, asked])
                if not more:
                    ended = True
                    break
                for node, prim in o.world_objects():
                    if not emu.call(PRIM_VF01, this=prim).eax & 0xFF:
                        o.unlink(node)
            name = f"kStrEffect{run}"
            flat, refs = [], []
            for first, last, asked in steps:
                refs.append(f"{{{first}, {last}, {len(flat)}, {len(asked)}}}")
                flat += asked
            lines.append(f"static const StrEventVector {name}Events[] = "
                         f"{{{', '.join('{' + hexs(e) + '}' for e in flat) or '{}'}}};")
            lines.append(f"static const StrStepVector {name}Steps[] = {{{', '.join(refs)}}};")
            rows.append(f"    {{{eid}, {p}, {seed + run}, {{{hexs(map(bits, pos))}}}, {{{hexs(map(bits, at))}}}, "
                        f"{{{hexs(map(bits, target))}}}, {level}, {steps[-1][1] + 1}, {int(ended)}, {name}Events, "
                        f"{name}Steps, {len(steps)}}},")
            emu.heap_release(mark)
            run += 1
    lines.append("static const StrEffectVector kStrEffects[] = {")
    lines += rows
    lines.append("};")


GENERATED = "// Generated by tools/prim_vectors.py from the original in the emulation oracle."
# The files main writes, in the order the oracle runs them; kTextures, last, names every
# texture the others use.
PARTS = ("basics", "rings", "squares", "motes", "walls", "trails", "screens", "flights", "crossed", "solids",
         "ribbons", "strs", "cylinders", "particles", "textures")


def main():
    out = sys.argv[1]
    rng = random.Random(91)
    o = PrimOracle()
    parts = {name: [GENERATED] for name in PARTS}
    lines = parts["basics"]
    lines.append(f"// State fields, in order: {', '.join(f'0x{f:x}' for f in STATE)}.")
    table(o, lines)
    synthetic(o, rng, lines)
    hit_sparks(o, rng, lines)
    synthetic3(o, rng, lines)
    bursts(o, rng, lines)
    lines = parts["rings"]
    synthetic_rings(o, rng, lines)
    ring_effects(o, rng, lines)
    lines = parts["squares"]
    synthetic_squares(o, rng, lines)
    square_effects(o, rng, lines)
    synthetic_rings(o, rng, lines, FLAT_IDS, "kFlatSquare", 4)
    ring_effects(o, rng, lines, FLAT_EFFECTS, "kFlatEffect", 6000)
    lines = parts["motes"]
    synthetic_records(o, rng, lines, MOTE_IDS, "kMote", 7000, random_motes, mote_state)
    square_effects(o, rng, lines, MOTE_EFFECTS, "kMoteEffect", 8000)
    synthetic_records(o, rng, lines, PIECE_IDS, "kPiece", 9000, random_piece, piece_state, 24, (0, 8, 16, 23))
    square_effects(o, rng, lines, PIECE_EFFECTS, "kPieceEffect", 10000)
    lines = parts["walls"]
    synthetic_records(o, rng, lines, WALL_IDS, "kWall", 11000, random_wall, wall_state, 24, (0, 8, 16, 23))
    square_effects(o, rng, lines, WALL_EFFECTS, "kWallEffect", 12000)
    lines = parts["trails"]
    synthetic_trails(o, rng, lines)
    # The trail effects pick actions up to 3: the stand-in's two actions twice over.
    wide = o.act(ACTIONS + ACTIONS)
    o.actions_of[wide] = ACTIONS + ACTIONS
    o.set_act = wide
    square_effects(o, rng, lines, TRAIL_EFFECTS, "kTrailEffect", 14000, state_trail, True, TRAIL_LOOKS, True)
    lines = parts["screens"]
    synthetic_screens(o, rng, lines)
    # Signum's Cross.str plays on; the port leaves its end to the .str player.
    o.advance_str = lambda this, x, y, z: 1
    o.emu.replace_thiscall(ADVANCE_STR, 3, lambda this, x, y, z: o.advance_str(this, x, y, z))
    square_effects(o, rng, lines, SCREEN_EFFECTS, "kScreenEffect", 16000, screen_effect_state, True, None, True,
                   True, True)
    lines = parts["flights"]
    synthetic_flights(o, rng, lines)
    square_effects(o, rng, lines, FLIGHT_EFFECTS, "kFlightEffect", 18000, flight_effect_state, False, None, True,
                   False, {0x30, 0x52, 99, 0xA1})
    lines = parts["crossed"]
    synthetic_flights(o, rng, lines, [0x10], 19000, "kCrossed", "kCrossedQuads")
    # Concentration sends the game mode a message through its vf05; C3dGround::vf03 (004bc290)
    # is an empty function of four arguments to stand in for it.
    o.emu.write_u32(o.mode, o.emu.scratch(struct.pack("<6I", 0, 0, 0, 0, 0, EMPTY_VF4)))
    square_effects(o, rng, lines, CROSSED_EFFECTS, "kCrossedEffect", 20000, flight_effect_state, True, None, True,
                   True, True)
    lines = parts["solids"]
    synthetic_flights(o, rng, lines, SOLID_IDS, 21000, "kSolid", "kSolids", random_solid, solid_state,
                      (0, 3, 9, 20))
    square_effects(o, rng, lines, SOLID_EFFECTS, "kSolidEffect", 22000, solid_effect_state, True, None, True,
                   True, True)
    lines = parts["ribbons"]
    synthetic_flights(o, rng, lines, RIBBON_IDS, 23000, "kRibbon", "kRibbons", random_ribbon, ribbon_state)
    link_actors(o)
    synthetic_records(o, rng, lines, SPAN_IDS, "kSpan", 24000, random_span, wall_state, 24, (0, 8, 16, 23))
    square_effects(o, rng, lines, SPAN_EFFECTS, "kSpanEffect", 25000, wall_state, False, None, True, True, True)
    lines = parts["strs"]
    square_effects(o, rng, lines, COLUMN_EFFECTS, "kColumnEffect", 25500, flight_effect_state, True, None, True,
                   True, True)
    str_effects(o, rng, lines)
    lines = parts["cylinders"]
    square_effects(o, rng, lines, CYLINDER_EFFECTS, "kCylinderEffect", 28000, cylinder_effect_state, False, None,
                   True, True, True, CYLINDER_FACINGS)
    lines = parts["particles"]
    # 0xa5 picks actions up to 4: the stand-in's two actions three times over.
    six = o.act(ACTIONS * 3)
    o.actions_of[six] = ACTIONS * 3
    o.set_act = six
    square_effects(o, rng, lines, PARTICLE_EFFECTS, "kParticleEffect", 29000, flight_effect_state, True, None,
                   True, True, True)
    square_effects(o, rng, lines, OTHER_PARTICLE_EFFECTS, "kOtherParticleEffect", 29500, flight_effect_state, True,
                   None, True, True, True)
    names = sorted(o.textures.items(), key=lambda kv: kv[1])
    lines = parts["textures"]
    lines.append("static const TextureVector kTextures[] = {" +
                 ", ".join(f'{{0x{v:x}, "{c_literal(k)}"}}' for k, v in names) + "};")
    os.makedirs(out, exist_ok=True)
    for name, lines in parts.items():
        with open(os.path.join(out, f"{name}.inc"), "w", encoding="ascii", newline="\n") as f:
            f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
