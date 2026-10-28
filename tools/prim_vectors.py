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
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from actor_vectors import GET_GAME_MODE, Oracle, bits, f32, hexs  # noqa: E402

RAG_EFFECT_CTOR, INIT_EFFECT, RAG_EFFECT_VF01 = 0x54D220, 0x54EEE0, 0x54D4C0
LOAD_STR_FILE = 0x54FAE0
PRIM_CTOR, INIT_PRIMITIVE, PRIM_VF01, PRIM_VF02, PRIM_VF03 = 0x57CFA0, 0x586380, 0x57DCE0, 0x586050, 0x586070
ADD_PRIMITIVE, GET_TEXTURE, PLAY_WAVE = 0x412550, 0x40CC00, 0x4352A0
SET_SPRITE, SET_ACT, JOB_NAMES = 0x5018A0, 0x501910, 0x686DC8
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
                               emu.read_u32(prim + 0xC), flags))
            return 0

        def texture(this, name, flag):
            return self.textures.setdefault(emu.read_cstr(name, 64), TEXTURE_BASE + len(self.textures))

        emu.replace_thiscall(ADD_PRIMITIVE, 2, add)
        emu.replace_thiscall(GET_TEXTURE, 2, texture)
        emu.replace_thiscall(PLAY_WAVE, 0, lambda this: 0)
        # CRenderObject's constructor names an NPC sprite from the job names and loads it.
        names = emu.scratch(size=0x400)
        for i in range(0x100):
            emu.write_u32(names + 4 * i, emu.scratch(b"x\0"))
        emu.write_u32(JOB_NAMES, names)
        emu.replace_thiscall(SET_SPRITE, 1, lambda this, name: 0)
        emu.replace_thiscall(SET_ACT, 1, lambda this, name: 0)
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
        self.emu.call(PRIM_VF03, 0, this=prim)
        return list(self.added)


def triangles_cpp(added):
    return "{" + ", ".join(f"{{{{{hexs(v)}}}, 0x{t:x}, 0x{fl:x}}}" for v, t, fl in added) + "}" if added else "{{}}"


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
            data = name.encode("latin-1")
            literal = "".join(f"\\{c:03o}" if c >= 0x80 or c in (0x22, 0x5C) else chr(c) for c in data)
            rows.append(f'    {{{t}, {minimal}, {seed}, {emu.read_u32(effect + 0xC0)}, "{literal}"}},')
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
    names = sorted(o.textures.items(), key=lambda kv: kv[1])
    lines.append("static const TextureVector kTextures[] = {" +
                 ", ".join(f'{{0x{v:x}, "{k.replace(chr(92), chr(92) * 2)}"}}' for k, v in names) + "};")


def main():
    rng = random.Random(91)
    o = PrimOracle()
    lines = ["// Generated by tools/prim_vectors.py from the original in the emulation oracle.",
             f"// State fields, in order: {', '.join(f'0x{f:x}' for f in STATE)}."]
    table(o, lines)
    synthetic(o, rng, lines)
    hit_sparks(o, rng, lines)
    print("\n".join(lines))


if __name__ == "__main__":
    main()
