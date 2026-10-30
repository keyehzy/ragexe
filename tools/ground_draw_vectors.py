"""Test vectors for the port's ground drawing (docs/render/ground.md), taken from the
original in the emulation oracle.

usage: .venv/bin/python -I tools/ground_draw_vectors.py > tests/unit/ground_draw_vectors.inc

Synthetic grounds are built by the original (tools/ground_vectors.py), a SceneRenderer is
constructed and sized to 800 x 600 as C3dDevice::CreateRenderer does (after C3dDevice::Init, since its
constructor makes a texture), and
C3dGround15::DrawCells (slot 4, 004be500) runs on a rectangle of cells with a view
matrix: with and without screen culling, the colour fade's other set (00679f9c), water
(none, flat, waves, type 4) and ISLIGHTMAP. The water texture getter (004bc4e0) is
replaced. Recorded: every primitive DrawCells added, in order, from the ground list and
the alpha list (its vertices, second coordinates, texture and lightmap), and every cell's
frame stamp and corner heights afterwards.

Then the cell cursor: on one more ground with an attribute map (C3dGround+4), C3dGround15
slot 15 (004c0240) for attribute cells in and around the map under each view, recording the
quad it adds to the alpha list (vertices, texture, blends); and CGameMode::GetCellAttribute's
attribute for each cell and some off the map.
"""
import math
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu.winapi import WINDOW  # noqa: E402
from ground_vectors import PAGE_BASE, TEXTURE_BASE, build, ground_file  # noqa: E402

SCENE_RENDERER_CTOR, SCENE_RENDERER_SIZE, SET_SIZE = 0x40D1A0, 0x2A0, 0x40DB70
G_C3DDEVICE, C3DDEVICE_CTOR, C3DDEVICE_INIT, IID_HAL_DEVICE = 0x6239D0, 0x404240, 0x404410, 0x5EC118
G_PSCENE_RENDERER, WORLD_COLOR_FADE, ALTERNATE_FADE = 0x625E98, 0x686598, 0x679F9C
DRAW_CELLS, WATER_TEXTURE, WATER_TEXTURE_ID = 0x4BE500, 0x4BC4E0, 0x73000000
FRAME = 7
WATER_UV_SCALE = 0x679F98
INIT_ANGLE_TABLES, SIN_TABLE, COS_TABLE = 0x40B2F0, 0x6247B4, 0x623C6C
DRAW_CURSOR, CELL_ATTRIBUTE, GRID_TEXTURE = 0x4C0240, 0x5121D0, 0x74000000
CURSOR_UV = 0x679F78  # static_init_093 (004bbb30): (0, 0), (1, 0), (0, 1), (1, 1)


def view_matrix(yaw, pitch, distance, shift):
    """Rows of a 4 x 3 row-vector matrix: rotate about y, then x, then move away."""
    cy, sy, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
    r = [[cy, sy * sp, -sy * cp], [0.0, cp, sp], [sy, -cy * sp, cy * cp]]
    return [r[0][0], r[0][1], r[0][2], r[1][0], r[1][1], r[1][2], r[2][0], r[2][1], r[2][2],
            shift[0], shift[1], distance]


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def bits(f):
    return struct.unpack("<I", struct.pack("<f", f))[0]


def renderer(emu):
    emu.call(INIT_ANGLE_TABLES)  # WinMain fills SinDegrees' and CosDegrees' tables at start-up
    # The renderer's constructor makes a texture, so the device comes first.
    emu.call(C3DDEVICE_CTOR, this=G_C3DDEVICE)
    assert emu.call(C3DDEVICE_INIT, WINDOW, 0, IID_HAL_DEVICE, 0, 6, this=G_C3DDEVICE).eax == 0
    sr = emu.scratch(size=SCENE_RENDERER_SIZE)
    emu.call(SCENE_RENDERER_CTOR, this=sr)
    emu.call(SET_SIZE, 800, 600, this=sr)
    emu.write_u32(G_PSCENE_RENDERER, sr)
    return sr


def draw(data, lightmaps, light, settings):
    emu, g, pages = build(data, lightmaps, light)
    u = emu.read_u32
    sr = renderer(emu)
    emu.write_u32(sr + 0x34, FRAME)
    emu.mem_write(WORLD_COLOR_FADE + 0x2C, struct.pack("<6f", *settings["fade"]))
    emu.write_u32(ALTERNATE_FADE, settings["alternate"])
    # The water texture's scale, which static_init_092 (004bbb10) sets before WinMain.
    emu.mem_write(WATER_UV_SCALE, struct.pack("<f", 0.25))
    level, kind, step, phase, height = settings["water"]
    emu.mem_write(g + 0x40, struct.pack("<f", level))
    emu.write_u32(g + 0x50, kind)
    emu.write_u32(g + 0x48, step)
    emu.write_u32(g + 0x60, phase)
    emu.mem_write(g + 0x54, struct.pack("<f", height))
    emu.replace_thiscall(WATER_TEXTURE, 0, lambda this: WATER_TEXTURE_ID)
    matrix = emu.scratch(struct.pack("<12f", *settings["matrix"]))
    rect = emu.scratch(struct.pack("<4i", *settings["rect"]))
    emu.call(DRAW_CELLS, matrix, rect, settings["cull"], this=g)
    viewport = struct.unpack("<2f", emu.mem_read(sr, 8)) + struct.unpack("<6i", emu.mem_read(sr + 0x1C, 24))

    def primitive(p, flags):
        verts = list(struct.unpack("<32I", emu.mem_read(u(p), 0x80)))
        second = list(struct.unpack("<8I", emu.mem_read(u(p + 4), 0x20)))
        tex = u(p + 0xC)
        texture = -1 if tex == WATER_TEXTURE_ID else (tex - TEXTURE_BASE) // 0x10
        page = u(p + 0x10)
        page = -1 if page == 0 else (page - PAGE_BASE) // 0x10
        return (flags, texture, page, u(p + 8), u(p + 0x20), verts, second)

    prims = [primitive(u(a), 0x400) for a in range(u(sr + 0xA8), u(sr + 0xAC), 4)]
    prims += [primitive(u(a + 4), 1) for a in range(u(sr + 0xD8), u(sr + 0xDC), 8)]
    width, height = u(g + 8), u(g + 0xC)
    cells = []
    for c in range(width * height):
        rec = u(g + 0xC0) + c * 0x54
        cells.append((u(rec + 0x4C),) + struct.unpack("<4I", emu.mem_read(rec + 0x14, 4) +
                                                         emu.mem_read(rec + 0x20, 4) +
                                                         emu.mem_read(rec + 0x2C, 4) +
                                                         emu.mem_read(rec + 0x38, 4)))
    tables = (struct.unpack("<361I", emu.mem_read(SIN_TABLE, 361 * 4)),
              struct.unpack("<361I", emu.mem_read(COS_TABLE, 361 * 4)))
    return viewport, prims, cells, tables


def main():
    rng = random.Random(23)
    light = ((0.5, -0.7, 0.5), (1.0, 0.9, 0.8), (0.3, 0.3, 0.35))
    fade = (0.9, 0.8, 0.7, 0.6, 0.75, 0.5)
    configs = [
        # Water far below everything: y grows downward.
        dict(shape=(6, 5), lightmaps=1, cull=0, alternate=0, water=(1000.0, 0, 0, 0, 0.0),
             matrix=view_matrix(0.3, 0.9, 120.0, (5.0, -3.0)), rect=(0, 0, 5, 4)),
        dict(shape=(6, 5), lightmaps=0, cull=1, alternate=1, water=(0.0, 0, 3, 40, 0.0),
             matrix=view_matrix(-0.4, 1.0, 90.0, (40.0, 10.0)), rect=(1, 0, 5, 4)),
        dict(shape=(5, 5), lightmaps=1, cull=1, alternate=0, water=(1.5, 2, 7, 123, 2.5),
             matrix=view_matrix(0.8, 0.7, 70.0, (-10.0, 6.0)), rect=(0, 1, 4, 4)),
        dict(shape=(4, 6), lightmaps=1, cull=0, alternate=0, water=(-2.0, 4, 11, 350, 1.0),
             matrix=view_matrix(2.6, 0.8, 100.0, (0.0, 0.0)), rect=(0, 0, 3, 5)),
        dict(shape=(7, 4), lightmaps=0, cull=1, alternate=1, water=(3.0, 4, 5, 10, 0.75),
             matrix=view_matrix(1.4, 1.2, 60.0, (300.0, -200.0)), rect=(0, 0, 6, 3)),
    ]
    lines = ["// Generated by tools/ground_draw_vectors.py from the original in the emulation oracle."]
    cases = []
    for k, cfg in enumerate(configs):
        w, h = cfg["shape"]
        data = ground_file(rng, w, h, 5)
        cfg["fade"] = fade
        viewport, prims, cells, tables = draw(data, cfg["lightmaps"], light, cfg)
        lines.append(f"static const char* const kDrawGround{k}[] = {{")
        hexed = data.hex()
        lines += [f'    "{hexed[i:i + 96]}",' for i in range(0, len(hexed), 96)]
        lines += ["    nullptr,", "};"]
        lines.append(f"static const DrawnPrimitive kDrawn{k}[] = {{")
        for flags, texture, page, count, kind, verts, second in prims:
            lines.append(f"    {{0x{flags:x}, {texture}, {page}, {count}, {kind}, {{"
                         + ", ".join(f"0x{v:x}" for v in verts) + "}, {"
                         + ", ".join(f"0x{v:x}" for v in second) + "}},")
        if not prims:
            lines.append("    {},")
        lines.append("};")
        lines.append(f"static const DrawnCell kDrawnCells{k}[] = {{")
        lines += [f"    {{0x{c[0]:x}, {{0x{c[1]:x}, 0x{c[2]:x}, 0x{c[3]:x}, 0x{c[4]:x}}}}}," for c in cells]
        lines.append("};")
        water = cfg["water"]
        water_text = (f"{{0x{bits(water[0]):08x}, {water[1]}, {water[2]}, {water[3]}, "
                      f"0x{bits(water[4]):08x}}}")
        sx, sy, cx, cy, vw, vh, ox, oy = viewport
        cases.append(
            f"    {{kDrawGround{k}, {cfg['lightmaps']}, {cfg['cull']}, {cfg['alternate']}, {water_text}, "
            f"{{{', '.join(f'0x{bits(f32(x)):08x}' for x in cfg['matrix'])}}}, "
            f"{{{', '.join(map(str, cfg['rect']))}}}, "
            f"{{0x{bits(sx):08x}, 0x{bits(sy):08x}, {cx}, {cy}, {vw}, {vh}, {ox}, {oy}}}, "
            f"kDrawn{k}, {len(prims)}, kDrawnCells{k}, {len(cells)}}},")
    lines.append(f"static constexpr std::uint32_t kDrawFrame = {FRAME};")
    for name, table in zip(("kSinTable", "kCosTable"), tables):
        lines.append(f"// InitAngleTables (0040b2f0): {name[1:4].lower()} of 0 to 360 degrees, as float bits.")
        lines.append(f"static const std::uint32_t {name}[361] = {{")
        lines += ["    " + ", ".join(f"0x{v:08x}" for v in table[i:i + 8]) + "," for i in range(0, 361, 8)]
        lines.append("};")
    lines.append("static const std::uint32_t kDrawLight[9] = {"
                 + ", ".join(f"0x{bits(x):08x}" for vec in light for x in vec) + "};")
    lines.append("static const std::uint32_t kDrawFade[6] = {"
                 + ", ".join(f"0x{bits(x):08x}" for x in fade) + "};")
    lines.append("// {ground file in hex, ISLIGHTMAP, screen culling, the fade's other set, water {level,")
    lines.append("// type, step, phase, wave height}, view matrix, cell rectangle, viewport {scale")
    lines.append("// x, scale y, centre x, centre y, width, height, origin x, origin y}, the primitives")
    lines.append("// {flags, texture, lightmap page, vertex count, primitive type, vertices, second")
    lines.append("// coordinates}, the cells {frame stamp, corner heights}}.")
    lines.append("static const DrawVector kDrawVectors[] = {")
    lines += cases
    lines.append("};")
    # The configs' views, and two rolled ones, which the client's camera never is, for the
    # order of the cursor's sums.
    rolled = []
    for roll in (0.3, -0.5):
        m = view_matrix(0.6, 0.8, 80.0, (-5.0, 4.0))
        cr, sr = math.cos(roll), math.sin(roll)
        for row in range(3):
            a, b = m[3 * row], m[3 * row + 1]
            m[3 * row], m[3 * row + 1] = a * cr - b * sr, a * sr + b * cr
        rolled.append(m)
    cursors(rng, light, [cfg["matrix"] for cfg in configs] + rolled, lines)
    print("\n".join(lines))


def cursors(rng, light, views, lines):
    w, h = 6, 5
    data = ground_file(rng, w, h, 5)
    names = []

    def texture(name):
        names.append(emu.read_cstr(name, 64, encoding="latin-1"))
        return GRID_TEXTURE

    emu, g, _ = build(data, 1, light, texture)
    u = emu.read_u32
    sr = renderer(emu)
    emu.mem_write(CURSOR_UV, struct.pack("<8f", 0, 0, 1, 0, 0, 1, 1, 1))
    aw, ah = 2 * w, 2 * h
    cells = [([f32(rng.uniform(-15, 5)) for _ in range(4)], rng.choice([0, 0, 0, 1, 5, 2, 3]))
             for _ in range(aw * ah)]
    attr = emu.scratch(size=0x130)
    emu.write_u32(attr + 0x110, aw)
    emu.write_u32(attr + 0x114, ah)
    emu.write_u32(attr + 0x118, 5)
    emu.write_u32(attr + 0x120, emu.scratch(b"".join(struct.pack("<4fi", *hs, t) for hs, t in cells)))
    emu.write_u32(g + 4, attr)
    lines.append(f"static const char* const kCursorGround[] = {{")
    hexed = data.hex()
    lines += [f'    "{hexed[i:i + 96]}",' for i in range(0, len(hexed), 96)]
    lines += ["    nullptr,", "};"]
    lines.append(f"static constexpr std::int32_t kCursorAttrWidth = {aw}, kCursorAttrHeight = {ah};")
    lines.append("// The attribute map's cells: four heights and the attribute.")
    lines.append("static const CursorAttrCell kCursorAttr[] = {")
    lines += [f"    {{{{{', '.join(f'0x{bits(v):08x}' for v in hs)}}}, {t}}}," for hs, t in cells]
    lines.append("};")
    lines.append("static const std::uint32_t kCursorViews[][12] = {" + ", ".join(
        "{" + ", ".join(f"0x{bits(f32(x)):08x}" for x in v) + "}" for v in views) + "};")
    lines.append("// {view, x, z, colour; drawn, the quad's vertices, texture name, source and destination")
    lines.append("// blends}.")
    lines.append("static const CursorVector kCursorVectors[] = {")
    for n in range(180):
        view = n % len(views)
        x, z = rng.randrange(-2, aw + 2), rng.randrange(-2, ah + 2)
        color = rng.choice([0x6600FFAA, 0xFEFF0000, rng.getrandbits(32)])
        before = (u(sr + 0xDC) - u(sr + 0xD8)) // 8
        del names[:]
        emu.call(DRAW_CURSOR, emu.scratch(struct.pack("<12f", *views[view])), x, z, color, this=g)
        added = [u(a + 4) for a in range(u(sr + 0xD8) + 8 * before, u(sr + 0xDC), 8)]
        assert len(added) <= 1
        verts, name, blends = [0] * 32, "", (0, 0)
        if added:
            p = added[0]
            verts = list(struct.unpack("<32I", emu.mem_read(u(p), 0x80)))
            assert u(p + 0xC) == GRID_TEXTURE
            name = names[0]
            blends = (u(p + 0x14), u(p + 0x18))
        lines.append(f"    {{{view}, {x}, {z}, 0x{color:08x}, {len(added)}, "
                     f"{{{', '.join(f'0x{v:x}' for v in verts)}}}, \"{name}\", {blends[0]}, {blends[1]}}},")
    lines.append("};")
    world = emu.scratch(size=0x100)
    emu.write_u32(world + 0x40, attr)
    mode = emu.scratch(size=0x400)
    emu.write_u32(mode + 0xB4, world)
    rows = []
    for z in range(-1, ah + 1):
        for x in range(-1, aw + 1):
            rows.append(f"{{{x}, {z}, {emu.call(CELL_ATTRIBUTE, x, z, this=mode).eax}}}")
    lines.append("// {x, z, CGameMode::GetCellAttribute}.")
    lines.append("static const CellAttributeVector kCellAttributes[] = {" + ", ".join(rows) + "};")


if __name__ == "__main__":
    main()
