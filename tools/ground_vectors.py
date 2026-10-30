"""Test vectors for the port's ground builder (docs/render/ground.md), taken from the
original in the emulation oracle.

usage: .venv/bin/python -I tools/ground_vectors.py > tests/unit/ground_vectors.inc

Synthetic grounds go through the original CGndRes::Load (00429200), then into a
C3dGround15 built as CWorld::LoadWorld builds one, and C3dGround15::Build (vf03,
004bcc30) with a light, with ISLIGHTMAP off and on. TextureCache::GetTexture (0040cc00)
and C3dDevice::CreateTexture (00405350) are replaced: the first answers each texture name
with a stand-in for its index, the second keeps the atlas pages it is given. Recorded:
every cell's surfaces and lightmap colour, every referenced surface's texture, lightmap
block and colour with its four vertices, the atlas blocks, and a hash of each page (of
only its blocks' texels for a first page that is not full, whose other texels are the
stack's leftovers).
The x87 runs at single precision, as DirectDraw's FPUSETUP leaves it in the client.
"""
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from unicorn.x86_const import UC_X86_REG_FPCW  # noqa: E402

from emu import Emulator  # noqa: E402

CGNDRES_CTOR, CGNDRES_LOAD, CGNDRES_SIZE = 0x429170, 0x429200, 0x1E8
GROUND_CTOR, GROUND15_VTABLE, GROUND_SIZE, GROUND_BUILD = 0x4BC220, 0x5E897C, 0xEC, 0x4BCC30
GET_TEXTURE, CREATE_TEXTURE = 0x40CC00, 0x405350
IS_LIGHTMAP = 0x61EC60
TEXTURE_BASE, PAGE_BASE = 0x71000000, 0x72000000


def fnv1a64(data, h=0xCBF29CE484222325):
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def bits(f):
    return struct.unpack("<I", struct.pack("<f", f))[0]


def ground_file(rng, width, height, lightmaps):
    """A minor-7 ground: corners mostly shared with the neighbours, some steps, walls."""
    textures = [f"t{k}.bmp".encode() for k in range(3)]
    grid = [[rng.choice([0.0, 0.0, -5.0, 5.0, 12.5, -20.0]) for _ in range(width + 1)]
            for _ in range(height + 1)]
    surfaces, cells = [], []

    def surface():
        surfaces.append((
            [rng.choice([0.0, 0.25, 0.5, 1.0, rng.random()]) for _ in range(8)],
            rng.randrange(len(textures)), rng.randrange(lightmaps) if lightmaps else 0,
            rng.getrandbits(32)))
        return len(surfaces) - 1

    for z in range(height):
        for x in range(width):
            h = [grid[z][x], grid[z][x + 1], grid[z + 1][x], grid[z + 1][x + 1]]
            if rng.random() < 0.3:
                h[rng.randrange(4)] += rng.choice([-7.0, 3.0, 0.5])
            pick = []
            for chance in (0.9, 0.35, 0.35):
                r = rng.random()
                if r >= chance:
                    pick.append(-1)
                elif surfaces and r < 0.08:
                    pick.append(rng.randrange(len(surfaces)))   # shared with another cell
                else:
                    pick.append(surface())
            cells.append((h, pick))
    out = b"GRGN" + bytes((1, 7))
    out += struct.pack("<iifii", width, height, 10.0, len(textures), 80)
    out += b"".join(n.ljust(80, b"\0") for n in textures)
    out += struct.pack("<iiii", lightmaps, 8, 8, 1)
    out += bytes(rng.getrandbits(8) for _ in range(lightmaps * 256))
    out += struct.pack("<i", len(surfaces))
    for uv, texture, lightmap, color in surfaces:
        out += struct.pack("<8fhHI", *uv, texture, lightmap, color)
    for h, pick in cells:
        out += struct.pack("<4f3i", *h, *pick)
    return out


def build(data, lightmaps, light, other_texture=None):
    emu = Emulator(max_instructions=2_000_000_000).init_crt()
    emu.uc.reg_write(UC_X86_REG_FPCW, 0x007F)
    emu.serve_data_files({"data\\t.gnd": data})
    gnd = emu.scratch(size=CGNDRES_SIZE)
    emu.call(CGNDRES_CTOR, this=gnd)
    assert emu.call(CGNDRES_LOAD, emu.cstr("t.gnd"), this=gnd).eax & 0xFF
    names = [emu.read_u32(p) for p in range(emu.read_u32(gnd + 0x134), emu.read_u32(gnd + 0x138), 4)]
    pages = []

    def get_texture(this, name, flag):
        if name not in names and other_texture is not None:
            return other_texture(name)
        return TEXTURE_BASE + 0x10 * names.index(name)

    def create_texture(this, width, height, pixels, fmt, flag):
        pages.append((width, height, fmt, emu.mem_read(pixels, width * height * 4)))
        return PAGE_BASE + 0x10 * (len(pages) - 1)

    emu.replace_thiscall(GET_TEXTURE, 2, get_texture)
    emu.replace_thiscall(CREATE_TEXTURE, 5, create_texture)
    emu.write_u32(IS_LIGHTMAP, lightmaps)
    ground = emu.scratch(size=GROUND_SIZE)
    emu.call(GROUND_CTOR, this=ground)
    emu.write_u32(ground, GROUND15_VTABLE)
    vectors = [emu.scratch(struct.pack("<3f", *v)) for v in light]
    emu.call(GROUND_BUILD, gnd, *vectors, this=ground)
    return emu, ground, pages


def main():
    rng = random.Random(11)
    shapes = [(1, 1, 1), (3, 2, 4), (4, 4, 6), (5, 3, 9), (2, 3, 1030)]
    lights = [((0.5, -0.7, 0.5), (1.0, 0.9, 0.8), (0.3, 0.3, 0.35)),
              ((-0.3, -0.9, 0.2), (0.6, 0.6, 0.6), (0.6, 0.5, 0.4))]
    lines = ["// Generated by tools/ground_vectors.py from the original in the emulation oracle."]
    cases = []
    for k, (width, height, lightmaps) in enumerate(shapes):
        data = ground_file(rng, width, height, lightmaps)
        # Pieces, not one literal: MSVC limits a string literal to 64 KB.
        lines.append(f"static const char* const kGround{k}[] = {{")
        hexed = data.hex()
        lines += [f'    "{hexed[i:i + 96]}",' for i in range(0, len(hexed), 96)]
        lines.append("    nullptr,")
        lines.append("};")
        for use_lightmaps in (0, 1):
            light = lights[(k + use_lightmaps) % len(lights)]
            emu, g, pages = build(data, use_lightmaps, light)
            u = emu.read_u32
            n = len(cases)
            surfaces_begin = u(g + 0xA0)
            surface_count = (u(g + 0xA4) - surfaces_begin) // 0x20
            cells_begin = u(g + 0xC0)
            atlas_blocks = u(g + 0x14 + 8)

            def surface_index(p):
                return -1 if p == 0 else (p - surfaces_begin) // 0x20

            cell_rows, used = [], set()
            for c in range(width * height):
                rec = cells_begin + c * 0x54
                top, east, north = (surface_index(u(rec + o)) for o in (0x40, 0x44, 0x48))
                used.update(s for s in (top, east, north) if s >= 0)
                marker = u(rec + 0x4C) if east >= 0 else 0
                r, gg, b = emu.mem_read(rec + 0x50, 3)
                cell_rows.append(f"    {{{top}, {north}, {east}, 0x{marker:x}, {r}, {gg}, {b}}},")
            lines.append(f"static const CellVector kCells{n}[] = {{")
            lines += cell_rows
            lines.append("};")
            surface_rows = []
            for s in range(surface_count):
                rec = surfaces_begin + s * 0x20
                texture = u(rec + 8)
                if s not in used:
                    surface_rows.append(f"    {{{s}, 0, 0x{texture:x}, 0, 0, 0, {{}}}},")
                    continue
                tex_index = (texture - TEXTURE_BASE) // 0x10
                page = (u(rec + 0xC) - PAGE_BASE) // 0x10 if use_lightmaps else -1
                block = (u(rec + 0x10) - atlas_blocks) // 0x128 if use_lightmaps else -1
                verts = u(rec)
                values = []
                for v in range(4):
                    vals = list(struct.unpack("<8I", emu.mem_read(verts + v * 0x20, 0x20)))
                    if not use_lightmaps:
                        vals[6] = vals[7] = 0
                    values += vals
                surface_rows.append(f"    {{{s}, 1, {tex_index}, {page}, {block}, 0x{u(rec + 0x14):x}, {{"
                                    + ", ".join(f"0x{v:x}" for v in values) + "}},")
            lines.append(f"static const SurfaceVector kSurfaces{n}[] = {{")
            lines += surface_rows
            lines.append("};")
            block_rows = []
            if use_lightmaps:
                for b in range(lightmaps):
                    rec = atlas_blocks + b * 0x128
                    uv = struct.unpack("<8I", emu.mem_read(rec + 0x108, 0x20))
                    r, gg, bb = emu.mem_read(rec + 0x100, 3)
                    page = (u(rec + 0x104) - PAGE_BASE) // 0x10
                    block_rows.append(f"    {{{page}, {r}, {gg}, {bb}, {{"
                                      + ", ".join(f"0x{v:x}" for v in uv) + "}},")
            lines.append(f"static const BlockVector kBlocks{n}[] = {{")
            lines += block_rows or ["    {},"]
            lines.append("};")
            page_rows = []
            for p, (w, h, fmt, px) in enumerate(pages):
                # A first page that is not full keeps the stack's leftovers outside its
                # blocks: only the blocks' texels are compared there.
                whole = p > 0 or lightmaps >= 1024
                if not whole:
                    texels = b"".join(px[((i // 32 * 8 + y) * 256 + i % 32 * 8) * 4:][:32]
                                      for i in range(lightmaps) for y in range(8))
                    px = texels
                page_rows.append(f"    {{{w}, {h}, {fmt}, {int(whole)}, 0x{fnv1a64(px):016x}ull}},")
            lines.append(f"static const PageVector kPages{n}[] = {{")
            lines += page_rows or ["    {},"]
            lines.append("};")
            light_bits = ", ".join(f"0x{bits(x):08x}" for vec in light for x in vec)
            cases.append(f"    {{kGround{k}, {use_lightmaps}, {{{light_bits}}}, "
                         f"kCells{n}, {width * height}, kSurfaces{n}, {surface_count}, "
                         f"kBlocks{n}, {len(block_rows)}, kPages{n}, {len(pages)}}},")
    lines.append("// {ground file in hex, ISLIGHTMAP, light direction, diffuse and ambient as float bits, cells,")
    lines.append("// surfaces, atlas blocks, atlas pages}.")
    lines.append("static const GroundVector kGroundVectors[] = {")
    lines += cases
    lines.append("};")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
