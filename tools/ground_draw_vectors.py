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


def draw(data, lightmaps, light, settings):
    emu, g, pages = build(data, lightmaps, light)
    u = emu.read_u32
    emu.call(INIT_ANGLE_TABLES)  # WinMain fills SinDegrees' and CosDegrees' tables at start-up
    # The renderer's constructor makes a texture, so the device comes first.
    emu.call(C3DDEVICE_CTOR, this=G_C3DDEVICE)
    assert emu.call(C3DDEVICE_INIT, WINDOW, 0, IID_HAL_DEVICE, 0, 6, this=G_C3DDEVICE).eax == 0
    sr = emu.scratch(size=SCENE_RENDERER_SIZE)
    emu.call(SCENE_RENDERER_CTOR, this=sr)
    emu.call(SET_SIZE, 800, 600, this=sr)
    emu.write_u32(G_PSCENE_RENDERER, sr)
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
    print("\n".join(lines))


if __name__ == "__main__":
    main()
