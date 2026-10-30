"""Test vectors for the port's screen rectangles of surfaces and textures
(docs/render/texture.md#drawing), taken from the original in the emulation oracle.

usage: .venv/bin/python -I tools/blit_vectors.py > tests/unit/blit_vectors.inc

  - CTexture::DrawRegion (00416f10) and DrawImage (00416fc0), slots 3 and 4, through
    SceneRenderer 00410ab0: assorted points, sizes (some wrapping), colours, texture sizes
    (powers of two, odd, 0 and past 2**24), image scales and texel offsets (006053f4,
    006053f8), with the bound texture (+0x50) given. Every call the fake Direct3D device
    sees is recorded, each draw with its four vertices.
  - CSurface's slots 3 and 4 (004163c0, 004163f0), through SceneRenderer 0040e610 and
    0040e6a0: assorted points, inside, across and past each edge of assorted screens
    (+0x80, +0x84), and sizes. The BltFast or Blt the back buffer (+0x7c) gets is recorded.
The x87 runs at single precision, as DirectDraw's FPUSETUP leaves it in the client.
"""
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from unicorn.x86_const import UC_X86_REG_FPCW  # noqa: E402

from emu import Emulator  # noqa: E402

DRAW_REGION, DRAW_IMAGE, SURFACE_SLOT3, SURFACE_SLOT4 = 0x416F10, 0x416FC0, 0x4163C0, 0x4163F0
SCENE_RENDERER_SIZE, G_PSCENE_RENDERER, TEXEL_OFFSET = 0x2A0, 0x625E98, 0x6053F4


def bits(f):
    return struct.unpack("<I", struct.pack("<f", f))[0]


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def hexs(values):
    return ", ".join(f"0x{v:08x}" for v in values)


def signed(v):
    return v - (1 << 32) if v >= 1 << 31 else v


class Oracle:
    def __init__(self):
        emu = self.emu = Emulator().init_crt()
        emu.uc.reg_write(UC_X86_REG_FPCW, 0x007F)
        dx = emu.directx
        self.device = dx.create_device()
        self.back = dx.create_texture(16, 16)
        self.renderer = emu.scratch(size=SCENE_RENDERER_SIZE)
        emu.write_u32(self.renderer + 0x78, self.device.address)
        emu.write_u32(self.renderer + 0x7C, self.back.address)
        emu.write_u32(G_PSCENE_RENDERER, self.renderer)
        # Texture ids 1 to 4, each a CTexture whose surface (+0xc) the fake device knows.
        self.textures = {}
        self.surfaces = {}
        for k in range(1, 5):
            surface = dx.create_texture(16, 16)
            texture = emu.scratch(size=0x128)
            emu.write_u32(texture + 0xC, surface.address)
            self.textures[k] = texture
            self.surfaces[surface.address] = k
        self.source = dx.create_texture(16, 16)
        self.surface = emu.scratch(size=0x20)
        emu.write_u32(self.surface + 0xC, self.source.address)

    def calls(self, start):
        """The device's and the back buffer's calls from index start on."""
        rows = []
        for c in self.emu.directx.calls[start:]:
            if c.method == "SetRenderState":
                rows.append(("kRenderState", -1, c.args[0], c.args[1], 0, None))
            elif c.method == "SetTextureStageState":
                rows.append(("kStageState", c.args[0], c.args[1], c.args[2], 0, None))
            elif c.method == "SetTexture":
                rows.append(("kTexture", c.args[0], 0, self.surfaces.get(c.args[1], 0), 0, None))
            elif c.method == "DrawPrimitive":
                vertices = []
                for v in c.detail["vertices"]:
                    vertices += [bits(v["xyz"][0]), bits(v["xyz"][1]), bits(v["xyz"][2]), bits(v["rhw"]),
                                 v["diffuse"], v["specular"], bits(v["tex0"][0]), bits(v["tex0"][1])]
                rows.append(("kDraw", -1, c.args[0], 0, c.args[1], vertices))
            elif c.method in ("BltFast", "Blt"):
                rows.append((c.method, c))
            else:
                raise SystemExit(f"unexpected {c.method}")
        return rows


def textures(o, rng):
    emu = o.emu
    cases, lines = [], []
    sizes = [1, 2, 16, 64, 128, 256, 512, 3, 37, 100, 1000, 0, (1 << 24) + 3, (1 << 25) + 1]
    for n in range(400):
        texture = rng.randrange(1, 5)
        width, height = rng.choice(sizes), rng.choice(sizes)
        scale = [f32(rng.choice([1.0, 0.5, 0.75, rng.random()])) for _ in range(2)]
        offset = [f32(rng.choice([0.0, 0.0, 0.5, -0.5, 0.25, rng.uniform(-1, 1)])) for _ in range(2)]
        x, y = rng.randrange(-200, 1100), rng.randrange(-200, 900)
        w, h = rng.choice([rng.randrange(0, 300), 0, 1, 256, rng.randrange(-50, 0)]), rng.randrange(0, 300)
        if n % 25 == 7:
            x, w = 0x7FFFFF00, 0x200
        if n % 25 == 19:
            # Past 2**24, where adding the size as a float would round it first.
            w, h = (rng.randrange(1 << 24, 1 << 25) | 1 for _ in range(2))
        if n % 25 == 13:
            x, y = (rng.choice([1, -1]) * rng.randrange(1 << 24, 1 << 25) for _ in range(2))
        color = rng.choice([0xFFFFFFFF, 0x80FFFFFF, rng.getrandbits(32)])
        bound = rng.choice([0, 1, 2, 3, 4])
        image = n % 2 == 1
        t = o.textures[texture]
        emu.mem_write(t + 4, struct.pack("<II", width, height))
        emu.mem_write(t + 0x18, struct.pack("<2f", *scale))
        emu.mem_write(TEXEL_OFFSET, struct.pack("<2f", *offset))
        emu.write_u32(o.renderer + 0x50, o.textures.get(bound, 0))
        start = len(emu.directx.calls)
        if image:
            emu.call(DRAW_IMAGE, x & 0xFFFFFFFF, y & 0xFFFFFFFF, w & 0xFFFFFFFF, h, this=t)
        else:
            emu.call(DRAW_REGION, x & 0xFFFFFFFF, y & 0xFFFFFFFF, w & 0xFFFFFFFF, h, color, this=t)
        rows = o.calls(start)
        after = emu.read_u32(o.renderer + 0x50)
        after_id = next((i for i, a in o.textures.items() if a == after), 0)
        lines.append(f"static const CallVector kRect{n}Calls[] = {{")
        for kind, stage, state, value, fvf, vertices in rows:
            v = f"{{{hexs(vertices)}}}" if vertices else "{}"
            lines.append(f"    {{{{SceneCall::Kind::{kind}, {stage}, {state}, 0x{value:x}, 0x{fvf:x}}}, {v}}},")
        lines.append("};")
        cases.append(f"    {{{int(image)}, {x}, {y}, {w}, {h}, 0x{color:08x}, {texture}, {width}u, {height}u, "
                     f"{{{hexs(map(bits, scale))}}}, {{{hexs(map(bits, offset))}}}, {bound}, kRect{n}Calls, "
                     f"{len(rows)}, {after_id}}},")
    lines.append("// {DrawImage (else DrawRegion), x, y, width, height, colour, texture, its width and")
    lines.append("// height, its image's u and v scales, the texel offset's u and v, the bound texture; the")
    lines.append("// calls and their count; the bound texture after}. Texture 0 is none.")
    lines.append("static const RectVector kRects[] = {")
    lines += cases
    lines.append("};")
    return lines


def surfaces(o, rng):
    emu = o.emu
    rows = []
    for n in range(600):
        screen = rng.choice([(640, 480), (800, 600), (1024, 768), (rng.randrange(0, 1500), rng.randrange(0, 1200))])
        w, h = rng.choice([rng.randrange(0, 400), 0, 1, 640, rng.randrange(-50, 0)]), rng.randrange(-20, 400)
        # Inside, across and past each edge.
        x = rng.choice([rng.randrange(0, screen[0] + 1), rng.randrange(-w - 30, 1) if w > 0 else -5,
                        screen[0] - rng.randrange(0, 40), screen[0] + rng.randrange(0, 40), rng.randrange(-900, -400),
                        rng.getrandbits(32) - (1 << 31)])
        y = rng.choice([rng.randrange(0, screen[1] + 1), rng.randrange(-h - 30, 1) if h > 0 else -5,
                        screen[1] - rng.randrange(0, 40), screen[1] + rng.randrange(0, 40), rng.randrange(-900, -400)])
        slot = 4 if n % 4 == 3 else 3
        emu.write_u32(o.renderer + 0x80, screen[0])
        emu.write_u32(o.renderer + 0x84, screen[1])
        start = len(emu.directx.calls)
        if slot == 3:
            emu.call(SURFACE_SLOT3, x & 0xFFFFFFFF, y & 0xFFFFFFFF, w & 0xFFFFFFFF, h & 0xFFFFFFFF, 0x1234,
                     this=o.surface)
        else:
            emu.call(SURFACE_SLOT4, x & 0xFFFFFFFF, y & 0xFFFFFFFF, w & 0xFFFFFFFF, h & 0xFFFFFFFF,
                     this=o.surface)
        calls = o.calls(start)
        blit = "0, 0, 0, {0, 0, 0, 0}, 0"
        if calls:
            assert len(calls) == 1
            method, c = calls[0]
            assert c.obj == o.back.label and c.detail["src"] == o.source.label, (c.obj, c.detail)
            if method == "BltFast":
                l, t, r, b = c.detail["src_rect"]
                blit = f"1, {signed(c.args[0])}, {signed(c.args[1])}, {{{l}, {t}, {r}, {b}}}, 0x{c.args[4]:x}"
            else:
                assert c.detail["src_rect"] is None
                l, t, r, b = c.detail["dst"]
                blit = f"2, 0, 0, {{{l}, {t}, {r}, {b}}}, 0x{c.args[3]:x}"
        rows.append(f"    {{{slot}, {x}, {y}, {w}, {h}, {screen[0]}u, {screen[1]}u, {blit}}},")
    return ["// {slot, x, y, width, height, the screen's width and height; the call: 0 none, 1 BltFast",
            "// (x, y, source rectangle, flags), 2 Blt (destination rectangle, flags)}.",
            "static const SurfaceVector kSurfaces[] = {"] + rows + ["};"]


def main():
    rng = random.Random(67)
    o = Oracle()
    lines = ["// Generated by tools/blit_vectors.py from the original in the emulation oracle."]
    lines += textures(o, rng)
    lines += surfaces(o, rng)
    print("\n".join(lines))


if __name__ == "__main__":
    main()
