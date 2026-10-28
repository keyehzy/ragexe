"""Test vectors for the port's .str effects (docs/render/effect.md), taken from the original
in the emulation oracle.

usage: .venv/bin/python -I tools/effect_vectors.py > tests/unit/effect_vectors.inc

Synthetic effects (layers of keys of both types, every texture mode, colours, corners,
angles and blends) played by CRagEffect: 0054fb40 resets it, then each frame 0054fba0
advances it with assorted offsets, actors' positions and views, CRagEffect::vf03
(0054ee50) draws its layers through 00550070, and the frame counts on as vf01 counts it.
The last effect plays to its end and on. Each frame records what 0054fba0 returns and leaves (the screen point, every layer's
values, current key and moved flag, the layers finished), and every triangle 00550070
adds (AddPrimitive and GetEffectEntryTexture recorded in place).
The x87 runs at single precision, as DirectDraw's FPUSETUP leaves it in the client.
"""
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from actor_vectors import GET_GAME_MODE, Oracle, bits, f32, hexs  # noqa: E402

RESET, ADVANCE, DRAW = 0x54FB40, 0x54FBA0, 0x54EE50
ADD_PRIMITIVE, ENTRY_TEXTURE = 0x412550, 0x4289A0
EFFECT_SIZE, RES_SIZE, QUAD_SIZE, LAYER_SIZE, STATE_SIZE = 0x9000, 0xD3B4, 0x9C, 0x380, 0x74
TEXTURE_BASE = 0x5000


def random_effect(rng, layers, empty):
    """Layers of (texture count, keys), each key (frame, type, 29 dwords); some later
    layers without keys when empty is set."""
    out = []
    for layer in range(layers):
        if empty and layer > 0 and rng.random() < 0.12:
            out.append((rng.randrange(1, 4), []))
            continue
        textures = rng.randrange(2, 9)
        keys = []
        frame = rng.randrange(0, 6)
        for k in range(rng.randrange(2, 9)):
            kind = 0 if k == 0 or rng.random() < 0.3 else 1
            values = [bits(f32(v)) for v in (
                rng.uniform(200, 440), rng.uniform(120, 360),        # position
                rng.choice([0.0, 0.25]), rng.choice([0.0, 0.5]),     # u, v
                rng.choice([1.0, 0.75]), rng.choice([1.0, 0.5]),     # extent
                *(rng.uniform(-5, 5) for _ in range(4)),             # unused here
                *(rng.uniform(-60, 60) for _ in range(4)),           # corner x
                *(rng.uniform(-60, 60) for _ in range(4)),           # corner y
                rng.uniform(0, textures) if kind == 0 else rng.uniform(-2, 2),  # texture
                0.0, 0.0, 0.0,                                        # mode, step, angle
                *(rng.uniform(0, 255) if kind == 0 else rng.uniform(-30, 30) for _ in range(4)),
                0.0, 0.0, 0.0)]
            if kind == 1:
                values = [bits(f32(rng.uniform(-8, 8))) if 0 <= i <= 0x11 else v for i, v in enumerate(values)]
            values[0x13] = rng.choice([0, 1, 2, 3, 4, 5, 5, 7])
            values[0x14] = bits(f32(rng.choice([0.5, 1.0, 1.5, 2.25, -0.75])))
            values[0x15] = bits(f32(rng.choice([0.0, 0.0, 128.0, -64.5, 513.0, rng.uniform(-40, 40)])))
            values[0x1a] = rng.choice([2, 5, 6])
            values[0x1b] = rng.choice([2, 6, 7])
            values[0x1c] = rng.choice([0, 1, 2])
            keys.append((frame, kind, values))
            # an equal frame now and then, never for the last key
            frame += rng.choice([0, 1, 2, 3, 5, 8]) if k < 5 else rng.choice([1, 3, 6])
        if len(keys) > 1 and keys[-1][0] == keys[-2][0]:
            keys[-1] = (keys[-1][0] + 1, keys[-1][1], keys[-1][2])
        out.append((textures, keys))
    return out


class EffectOracle(Oracle):
    def __init__(self):
        super().__init__()
        emu = self.emu
        self.mode = emu.call(GET_GAME_MODE).eax
        emu.write_u32(self.mode + 0xB8, self.view)
        self.added = []
        emu.replace_thiscall(ENTRY_TEXTURE, 1, lambda this, index: TEXTURE_BASE + index)

        def add(this, prim, flags):
            vertices = emu.read_u32(prim)
            self.added.append((list(struct.unpack("<24I", emu.mem_read(vertices, 0x60))),
                               emu.read_u32(prim + 0xC), emu.read_u32(prim + 0x14),
                               emu.read_u32(prim + 0x18), emu.read_u32(prim + 0x1C), flags))
            return 0

        emu.replace_thiscall(ADD_PRIMITIVE, 2, add)

    def load(self, layers):
        emu = self.emu
        res = emu.scratch(size=RES_SIZE)
        emu.write_u32(res + 0x118, len(layers))
        for i, (textures, keys) in enumerate(layers):
            base = res + 0x120 + LAYER_SIZE * i
            emu.write_u32(base, textures)
            emu.write_u32(base + 0x378, len(keys))
            if keys:
                data = b"".join(struct.pack("<ii29I", f, t, *v) for f, t, v in keys)
                emu.write_u32(base + 0x37C, emu.scratch(data))
        effect = emu.scratch(size=EFFECT_SIZE)
        for q in range(2 * len(layers) + 2):
            emu.write_u32(effect + 0x43E8 + QUAD_SIZE * q, emu.scratch(size=0x60))
        emu.write_u32(effect + 0x758, res)
        emu.write_u32(effect + 0x75C, res + 0x110)
        actor = emu.scratch(size=0xA0)
        emu.write_u32(effect + 0xA0, actor)
        return effect, actor


def main():
    rng = random.Random(77)
    o = EffectOracle()
    emu = o.emu
    lines = ["// Generated by tools/effect_vectors.py from the original in the emulation oracle."]
    effects = []
    # The last has no empty layer, which would never finish, so that it plays to its end.
    for e, (count, frames, empty) in enumerate([(6, 70, True), (8, 90, True), (5, 80, False)]):
        layers = random_effect(rng, count, empty)
        lines.append(f"// Effect {e}: its layers' keys {{frame, type, 29 values}}.")
        layer_refs = []
        for li, (textures, keys) in enumerate(layers):
            name = f"kEffect{e}Layer{li}"
            lines.append(f"static const KeyVector {name}[] = {{" + (", ".join(
                f"{{{f}, {t}, {{{hexs(v)}}}}}" for f, t, v in keys) or "{}") + "};")
            layer_refs.append(f"{{{textures}, {name}, {len(keys)}}}")
        lines.append(f"static const LayerVector kEffect{e}Layers[] = {{{', '.join(layer_refs)}}};")
        effect, actor = o.load(layers)
        emu.call(RESET, this=effect)
        rows = []
        views = []
        for frame in range(frames):
            if frame % 25 == 0:
                at = (rng.uniform(-30, 30), rng.uniform(-10, 0), rng.uniform(-30, 30))
                o.build_view(f32(rng.choice([-45.0, -35.0, -55.0])), f32(rng.uniform(0, 360)),
                             f32(rng.choice([300.0, 250.0])), at)
                pos = (f32(at[0] + rng.uniform(-20, 20)), f32(at[1]), f32(at[2] + rng.uniform(-20, 20)))
                emu.mem_write(actor + 4, struct.pack("<3f", *pos))
                views.append((frame, list(struct.unpack("<12I", emu.mem_read(o.view + 0x94, 48))), pos))
            offset = rng.choice([(0.0, -80.0, 0.0), (0.0, -80.0, 0.0), (0.0, -20.0, 0.0), (3.5, 10.0, 0.25)])
            o.added.clear()
            more = emu.call(ADVANCE, *(bits(f32(v)) for v in offset), this=effect).eax & 0xFF
            emu.call(DRAW, 0, this=effect)
            state = list(struct.unpack(f"<{29 * len(layers)}I", emu.mem_read(effect + 0x760, STATE_SIZE * len(layers))))
            current = list(struct.unpack(f"<{len(layers)}i", emu.mem_read(effect + 0x41E8, 4 * len(layers))))
            moved = list(emu.mem_read(effect + 0x4168, len(layers)))
            screen = list(struct.unpack("<3I", emu.mem_read(effect + 0xDC, 12)))
            finished = emu.read_u32(effect + 0x4160)
            name = f"kEffect{e}Frame{frame}"
            lines.append(f"static const std::uint32_t {name}State[] = {{{hexs(state)}}};")
            lines.append(f"static const TriangleVector {name}Triangles[] = {{" + (", ".join(
                f"{{{{{hexs(v)}}}, 0x{tex:x}, 0x{sb:x}, 0x{db:x}, 0x{md:x}, 0x{fl:x}}}"
                for v, tex, sb, db, md, fl in o.added) or "{}") + "};")
            rows.append(f"    {{{{{hexs(bits(f32(v)) for v in offset)}}}, {more}, {{{hexs(screen)}}}, {finished}, "
                        f"{{{', '.join(map(str, current))}}}, {{{', '.join(map(str, moved))}}}, {name}State, "
                        f"{name}Triangles, {len(o.added)}}},")
            # vf01: the next frame
            emu.write_u32(effect + 0xBC, emu.read_u32(effect + 0xBC) + 1)
        lines.append(f"static const FrameVector kEffect{e}Frames[] = {{")
        lines += rows
        lines.append("};")
        lines.append(f"static const ViewVector kEffect{e}Views[] = {{")
        lines += [f"    {{{f}, {{{hexs(v)}}}, {{{hexs(map(bits, p))}}}}}," for f, v, p in views]
        lines.append("};")
        effects.append(f"    {{kEffect{e}Layers, {len(layers)}, kEffect{e}Frames, {frames}, kEffect{e}Views, {len(views)}}},")
    lines.append("static const EffectVector kEffects[] = {")
    lines += effects
    lines.append("};")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
