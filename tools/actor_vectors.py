"""Test vectors for the port's actor sprites (docs/render/actor.md), taken from the original
in the emulation oracle.

usage: .venv/bin/python -I tools/actor_vectors.py > tests/unit/actor_vectors.inc

  - The sprite scales: SceneRenderer::SetSize (0040db70) at assorted sizes, and CView::Step
    (004cf7d0) at assorted pitches and distances: the globals 0061470c, 0060564c, 00605650,
    00614704 and 00614708 they leave.
  - CRenderObject::method_00500f70, the body: a synthetic sprite of 8-bit and 32-bit images
    and a synthetic action's frames drawn by objects with assorted positions, scales,
    offsets, angles, colours and flags, under views CView::BuildView (004cfa10) builds, with
    assorted ground colours from 00500e60 (recorded in place). Each quad it passes to
    SceneRenderer 0040e980 is recorded: its twelve values and the flags.
  - CRenderObject::method_00501550, the shadow, for the same objects and views, on a
    synthetic attribute map and a synthetic shadow.spr and shadow.act.
  - CGameObject::vf04 (005000d0), an object's facing against the camera's yaw as one of
    eight directions, and CRenderObject::vf08 (00500830), the action and frame an object
    shows at a time: assorted facings, yaws, delays, play modes, times and frame counts.
  - SceneRenderer 0040e980 on quads with and without an angle, on 8-bit images (the texture
    cache 0040e540 answering a slot) and 32-bit images (their CTexture's extent): the four
    vertices of the RenderQuad it adds, its texture, and the flags passed on to
    AddPrimitive (00412550).
The x87 runs at single precision, as DirectDraw's FPUSETUP leaves it in the client.
"""
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from unicorn.x86_const import UC_X86_REG_FPCW  # noqa: E402

from emu import Emulator  # noqa: E402
from emu.winapi import Stub  # noqa: E402

SET_SIZE, SCENE_RENDERER_SIZE, G_PSCENE_RENDERER = 0x40DB70, 0x2A0, 0x625E98
INIT_ANGLE_TABLES, GET_GAME_MODE = 0x40B2F0, 0x4CBF30
CVIEW_SIZE, CVIEW_STEP, CVIEW_BUILD_VIEW, FIXED_CAMERA = 0x1C0, 0x4CF7D0, 0x4CFA10, 0x685ECC
SCALES = {"size": 0x61470C, "slope_down": 0x60564C, "slope_up": 0x605650,
          "layer_step": 0x614704, "depth_bias": 0x614708}
DRAW_BODY, DRAW_SHADOW, GROUND_COLOR = 0x500F70, 0x501550, 0x500E60
ADD_SPRITE_QUAD, FIND_CACHED, CACHE_IMAGE, NEW_QUAD, ADD_PRIMITIVE = (
    0x40E980, 0x40E540, 0x40E130, 0x412B40, 0x412550)
GET_FRAME, GET_ITEM, GET_FRAME_COUNT = 0x421A60, 0x420520, 0x421A80
DIRECTION, UPDATE_FRAME = 0x5000D0, 0x500830
GET_RESOURCE_MANAGER, GET_RESOURCE = 0x4DFA10, 0x4DFF60
RENDER_OBJECT_VTABLE = 0x5E953C
ATTR_SIZE, CELL_SIZE = 0x130, 5
ATTR_W, ATTR_H = 40, 40
SIZE = (800, 600)


def bits(f):
    return struct.unpack("<I", struct.pack("<f", f))[0]


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def hexs(values):
    return ", ".join(f"0x{v:08x}" for v in values)


def attr_height(cell, corner):
    return float((cell * 11 + corner * 5) % 23 - 15)


class Oracle:
    def __init__(self):
        emu = self.emu = Emulator().init_crt()
        emu.uc.reg_write(UC_X86_REG_FPCW, 0x007F)
        emu.call(INIT_ANGLE_TABLES)
        self.renderer = emu.scratch(size=SCENE_RENDERER_SIZE)
        emu.call(SET_SIZE, *SIZE, this=self.renderer)
        emu.write_u32(G_PSCENE_RENDERER, self.renderer)
        cells = b"".join(struct.pack("<4fi", *(attr_height(c, k) for k in range(4)), 0)
                         for c in range(ATTR_W * ATTR_H))
        attr = emu.scratch(size=ATTR_SIZE)
        emu.write_u32(attr + 0x110, ATTR_W)
        emu.write_u32(attr + 0x114, ATTR_H)
        emu.write_u32(attr + 0x118, CELL_SIZE)
        emu.write_u32(attr + 0x120, emu.scratch(cells))
        world = emu.scratch(size=0x100)
        emu.write_u32(world + 0x40, attr)
        self.view = emu.scratch(size=CVIEW_SIZE)
        emu.write_u32(self.view + 0x1B4, world)
        mode = emu.scratch(size=0x400)
        emu.write_u32(mode + 0xB4, world)
        emu.replace_thiscall(GET_GAME_MODE, 0, lambda this: mode)
        self.frames = {}     # (act, action, frame) -> ActFrame address
        self.items = {}      # (ActFrame address, index) -> ActItem address
        self.resources = {}  # name -> address
        emu.replace_thiscall(GET_FRAME, 2, lambda this, action, frame: self.get_frame(this, action, frame))
        emu.replace_thiscall(GET_ITEM, 1, lambda this, i: self.get_item(this, i))
        # __cdecl, so a replacement that pops nothing; GetResource reads the name.
        emu.replace_thiscall(GET_RESOURCE_MANAGER, 0, lambda this: 0x1234)
        emu.replace_thiscall(GET_RESOURCE, 1, lambda this, name: self.resources[emu.read_cstr(name)])
        self.ground = (1.0, 1.0, 1.0)

        def ground_color(this, r, g, b):
            emu.mem_write(r, struct.pack("<f", self.ground[0]))
            emu.mem_write(g, struct.pack("<f", self.ground[1]))
            emu.mem_write(b, struct.pack("<f", self.ground[2]))
            return 0

        emu.replace_thiscall(GROUND_COLOR, 3, ground_color)
        self.quads = []

        def add_quad(this, desc, palette, flags):
            self.quads.append((list(struct.unpack("<12I", emu.mem_read(desc, 48))), palette, flags))
            return 0

        self.add_quad = add_quad

    def get_frame(self, act, action, frame):
        return self.frames[(act, action, frame)]

    def get_item(self, frame, index):
        return self.items[(frame, index)]

    def sprite(self, images8, images32):
        """A CSprRes stand-in: the image records of each type at +0x510 and +0x520."""
        emu = self.emu
        spr = emu.scratch(size=0x534)
        for offset, images in ((0x510, images8), (0x520, images32)):
            records = [emu.scratch(struct.pack("<4h", *image) + bytes(8)) for image in images]
            table = emu.scratch(b"".join(struct.pack("<I", r) for r in records) or bytes(4))
            emu.write_u32(spr + offset + 4, table)
            emu.write_u32(spr + offset + 8, table + 4 * len(records))
        return spr

    def act(self, actions):
        """A CActRes stand-in: GetFrame and GetItem answer from the frames given, each a
        list of items (x, y, image, flags, r, g, b, a, scale x, scale y, angle, type)."""
        emu = self.emu
        act = emu.scratch(size=0x144)
        for a, frames in enumerate(actions):
            for f, items in enumerate(frames):
                frame = emu.scratch(size=0x4C)
                emu.write_u32(frame + 0x30, len(items))
                self.frames[(act, a, f)] = frame
                for i, it in enumerate(items):
                    self.items[(frame, i)] = emu.scratch(struct.pack("<iiiI4BffiI", *it))
        return act

    def build_view(self, pitch, yaw, distance, at):
        emu = self.emu
        v = self.view
        emu.mem_write(v + 0x1C, struct.pack("<3f", pitch, yaw, distance))
        emu.mem_write(v + 0x28, struct.pack("<3f", *at))
        emu.mem_write(v + 0x34, struct.pack("<3f", pitch, yaw, distance))
        emu.mem_write(v + 0x40, struct.pack("<3f", *at))
        emu.mem_write(v + 0x88, struct.pack("<3f", 0.0, 1.0, 0.0))
        emu.write_u32(FIXED_CAMERA, 0)
        emu.call(CVIEW_STEP, this=v)
        emu.call(CVIEW_BUILD_VIEW, this=v)
        return (list(struct.unpack("<12I", emu.mem_read(v + 0x94, 48))),
                [emu.read_u32(SCALES[k]) for k in SCALES])


def random_items(rng, n8, n32):
    items = []
    for _ in range(rng.randrange(1, 7)):
        kind = 1 if n32 and rng.random() < 0.25 else 0
        image = rng.randrange(n32 if kind else n8) if rng.random() > 0.1 else -1
        items.append((rng.randrange(-70, 40), rng.randrange(-110, 20), image, rng.choice([0, 0, 1, 2, 3]),
                      rng.randrange(256), rng.randrange(256), rng.randrange(256),
                      rng.choice([255, 255, 128, 0, rng.randrange(256)]),
                      f32(rng.choice([1.0, 1.0, -1.0, 0.5, 1.37, 2.25])),
                      f32(rng.choice([1.0, 1.0, 0.75, 1.5, -1.0])),
                      rng.choice([0, 0, 0, 15, -30, 90, 400]), kind))
    return items


def sprite_quads(o, rng):
    """SceneRenderer 0040e980 on its own: the texture cache and the quad pool answer from
    here, and AddPrimitive records what it is given."""
    emu = o.emu
    cached = {}
    added = []

    def find_cached(this, image, palette, out):
        u0, v0, u1, v1 = cached[image]
        emu.mem_write(out + 0xC, struct.pack("<4f", u0, v0, u1, v1))
        return 0x7700 + image % 0x100

    emu.replace_thiscall(FIND_CACHED, 3, find_cached)
    quad = emu.scratch(size=0xCC)
    vertices = emu.scratch(size=0x80)
    emu.replace_thiscall(NEW_QUAD, 0, lambda this: quad)

    def add_primitive(this, prim, flags):
        added.append((list(struct.unpack("<32I", emu.mem_read(vertices, 0x80))), emu.read_u32(prim + 0xC), flags))
        return 0

    emu.replace_thiscall(ADD_PRIMITIVE, 2, add_primitive)
    lines = ["// SceneRenderer 0040e980: {the 8 values, 32-bit image, the texture's u0, v0, u1, v1, flags},",
             "// then the four vertices it adds {x, y, z, rhw, diffuse, specular, u, v} and the texture",
             "// the primitive gets (the CTexture of a 32-bit image, the cache's answer otherwise)."]
    lines.append("static const SpriteQuadVector kSpriteQuads[] = {")
    for k in range(200):
        emu.mem_write(quad, bytes(0xCC))
        emu.write_u32(quad, vertices)
        top, left = f32(rng.uniform(-50, 600)), f32(rng.uniform(-50, 800))
        bottom, right = f32(top + rng.uniform(-5, 160)), f32(left + rng.uniform(-80, 160))
        rhw_bottom, rhw_top = f32(rng.uniform(0.0006, 0.1)), f32(rng.uniform(0.0006, 0.1))
        if k % 9 == 0:
            rhw_top = rhw_bottom
        color = rng.getrandbits(32)
        angle = f32(rng.choice([0.0, 0.0, 0.0, 15.0, -30.0, 90.0, 400.0, 37.6, -400.2, 1.0e-3, 180.0]))
        is32 = rng.random() < 0.3
        uv = [f32(rng.choice([0.0, 0.125, 0.25, 0.5])) for _ in range(2)]
        uv += [f32(uv[0] + rng.randrange(1, 129) / 256), f32(uv[1] + rng.randrange(1, 129) / 256)]
        record = emu.scratch(size=0x10)
        texture = 0
        if is32:
            texture = emu.scratch(size=0x128)
            emu.mem_write(texture + 0x18, struct.pack("<2f", uv[2], uv[3]))
            emu.write_u32(record + 8, texture)
            uv[0] = uv[1] = 0.0
        else:
            cached[record] = uv
        flags = rng.choice([0x101, 1])
        desc = struct.pack("<6fIf3I", top, left, bottom, right, rhw_bottom, rhw_top, color, angle, 0, 0, 0)
        desc += struct.pack("<I", record)
        added.clear()
        emu.call(ADD_SPRITE_QUAD, emu.scratch(desc), 0x1234, flags, this=o.renderer)
        assert len(added) == 1 and added[0][2] == flags
        verts, prim_texture, _ = added[0]
        expect = texture if is32 else 0x7700 + record % 0x100
        assert prim_texture == expect, (hex(prim_texture), hex(expect))
        values = [bits(top), bits(left), bits(bottom), bits(right), bits(rhw_bottom), bits(rhw_top), color, bits(angle)]
        lines.append(f"    {{{{{hexs(values)}}}, {int(is32)}, {{{hexs(map(bits, uv))}}}, 0x{flags:x}, "
                     f"{{{hexs(verts)}}}}},")
    lines.append("};")
    return lines


def animation(rng):
    """vf04 and vf08 with the camera's yaw at CGameMode+0xb8 (the CView) +0x20."""
    emu = Emulator().init_crt()
    emu.uc.reg_write(UC_X86_REG_FPCW, 0x007F)
    view = emu.scratch(size=CVIEW_SIZE)
    mode = emu.scratch(size=0x400)
    emu.write_u32(mode + 0xB8, view)
    emu.replace_thiscall(GET_GAME_MODE, 0, lambda this: mode)
    clock = [0]
    emu.api.stubs["WINMM.DLL!timeGetTime"] = Stub(0, lambda: clock[0])
    counts = {}
    emu.replace_thiscall(GET_FRAME_COUNT, 1, lambda this, action: counts[action])
    lines = ["// CGameObject::vf04 (005000d0): {facing, camera yaw, direction}."]
    lines.append("static const DirectionVector kDirections[] = {")
    angles = [0.0, 22.5, 44.9, 45.0, 90.0, 179.99, 180.0, 359.9, 360.0, -1.0, -45.0, 720.5, -800.25, 1e-6]
    for _ in range(120):
        facing = f32(rng.choice(angles + [rng.uniform(-1000, 1000)]))
        yaw = f32(rng.choice(angles + [rng.uniform(0, 360)]))
        emu.mem_write(view + 0x20, struct.pack("<f", yaw))
        d = emu.call(DIRECTION, bits(facing), this=0).eax
        lines.append(f"    {{0x{bits(facing):08x}, 0x{bits(yaw):08x}, {d}}},")
    lines.append("};")
    lines.append("// CRenderObject::vf08 (00500830): {base action, facing, camera yaw, delay, repeats, play")
    lines.append("// mode, start, now, finished, held, frame before, frame count}, then the action, the")
    lines.append("// frame and finished after.")
    lines.append("static const FrameStepVector kFrameSteps[] = {")
    obj = emu.scratch(size=0xA0)
    act = emu.scratch(size=0x144)
    for _ in range(200):
        emu.mem_write(obj, bytes(0xA0))
        emu.write_u32(obj, RENDER_OBJECT_VTABLE)
        emu.write_u32(obj + 0x9C, act)
        base = rng.choice([0, 8, 16, 24, 32])
        facing = f32(rng.choice([0.0, 45.0, 90.0, 200.0, 315.0, rng.uniform(0, 360)]))
        yaw = f32(rng.choice([0.0, 45.0, 135.0, rng.uniform(0, 360)]))
        delay = f32(rng.choice([1.0, 2.0, 4.0, 6.5, 0.75, 12.0]))
        repeats = f32(rng.choice([1.0, 1.0, 2.0, 3.5]))
        play = rng.choice([0, 0, 0, 1, 1, 2])
        start = rng.randrange(0, 1 << 31)
        now = (start + rng.choice([0, 1, 23, 24, 100, 950, 4000, 20000, rng.randrange(1 << 20)])) & 0xFFFFFFFF
        finished, held = int(rng.random() < 0.1), int(rng.random() < 0.1)
        frame = rng.randrange(0, 12)
        count = rng.choice([1, 1, 2, 3, 4, 5, 8, 13])
        emu.mem_write(view + 0x20, struct.pack("<f", yaw))
        emu.write_u32(obj + 0x20, base)
        emu.mem_write(obj + 0x38, struct.pack("<f", facing))
        emu.mem_write(obj + 0x44, struct.pack("<f", delay))
        emu.mem_write(obj + 0x4C, struct.pack("<f", repeats))
        emu.write_u32(obj + 0x54, play)
        emu.write_u32(obj + 0x70, start)
        emu.write_u32(obj + 0x28, frame)
        emu.mem_write(obj + 0x81, bytes([finished, held]))
        clock[0] = now
        counts.clear()
        for a in range(base, base + 8):
            counts[a] = count
        emu.call(UPDATE_FRAME, this=obj)
        after = (emu.read_u32(obj + 0x24), emu.read_u32(obj + 0x28), emu.mem_read(obj + 0x81, 1)[0])
        lines.append(f"    {{{base}, 0x{bits(facing):08x}, 0x{bits(yaw):08x}, 0x{bits(delay):08x}, "
                     f"0x{bits(repeats):08x}, {play}, 0x{start:08x}, 0x{now:08x}, {finished}, {held}, "
                     f"{frame}, {count}, {after[0]}, {after[1]}, {after[2]}}},")
    lines.append("};")
    return lines


def main():
    rng = random.Random(23)
    o = Oracle()
    emu = o.emu
    lines = ["// Generated by tools/actor_vectors.py from the original in the emulation oracle."]

    lines.append("// SetSize (0040db70): {width, height}, then 0061470c, 0060564c and 00605650.")
    lines.append("static const SizeScaleVector kSizeScales[] = {")
    for w, h in [(640, 480), (800, 600), (1024, 768), (1280, 1024), (801, 601), (320, 200), (1, 1), (7, 3)]:
        sr = emu.scratch(size=SCENE_RENDERER_SIZE)
        emu.call(SET_SIZE, w, h, this=sr)
        lines.append(f"    {{{w}, {h}, {{{hexs(emu.read_u32(SCALES[k]) for k in ('size', 'slope_down', 'slope_up'))}}}}},")
    lines.append("};")
    emu.call(SET_SIZE, *SIZE, this=o.renderer)

    lines.append("// CView::Step (004cf7d0) at the projection of 800 x 600, still: {pitch, yaw, distance},")
    lines.append("// the view BuildView makes, and 0061470c, 0060564c, 00605650, 00614704, 00614708.")
    views = []
    ats = []
    cases = [(-45.0, 0.0, 300.0), (-45.0, 90.0, 230.0), (-30.0, 200.0, 400.0), (-60.5, 45.0, 260.0),
             (-89.0, 10.0, 300.0), (-1.0, 300.0, 350.0), (-20.4, 135.0, 250.0), (-50.0, 315.0, 1000.0),
             (-70.9, 270.0, 120.5), (-45.0, 180.0, 3.0)]
    lines.append("static const ViewScaleVector kViewScales[] = {")
    for pitch, yaw, distance in cases:
        at = (rng.uniform(-50, 50), rng.uniform(-20, 0), rng.uniform(-50, 50))
        view, scales = o.build_view(f32(pitch), f32(yaw), f32(distance), at)
        views.append((view, scales))
        lines.append(f"    {{{{{hexs([bits(pitch), bits(yaw), bits(distance)])}}}, {{{hexs(view)}}}, "
                     f"{{{hexs(scales)}}}}},")
        ats.append(at)
    lines.append("};")

    images8 = [(rng.randrange(1, 140), rng.randrange(1, 140), rng.choice([0, 0, 1]), rng.choice([0, 0, 1]))
               for _ in range(12)]
    images32 = [(rng.randrange(1, 70), rng.randrange(1, 70), 0, 0) for _ in range(3)]
    spr = o.sprite(images8, images32)
    actions = [[random_items(rng, len(images8), len(images32)) for _ in range(3)] for _ in range(4)]
    act = o.act(actions)
    shadow_spr = o.sprite([(28, 14, 0, 0), (9, 9, 1, 1)], [])
    shadow_act = o.act([[[(-14, -7, 1, 0, 255, 255, 255, 160, 1.0, 1.0, 0, 0)]]])
    o.resources["shadow.spr"] = shadow_spr
    o.resources["shadow.act"] = shadow_act

    lines.append("// The sprite's images {width, height, halved x, halved y}, 8-bit then 32-bit.")
    lines.append(f"static const std::int16_t kImages8[][4] = {{{', '.join('{' + ', '.join(map(str, i)) + '}' for i in images8)}}};")
    lines.append(f"static const std::int16_t kImages32[][4] = {{{', '.join('{' + ', '.join(map(str, i)) + '}' for i in images32)}}};")
    lines.append("// The action's items: {x, y, image, flags, R, G, B, A, scale x, scale y, angle, type}.")
    frame_refs = []
    for a, frames in enumerate(actions):
        for f, items in enumerate(frames):
            name = f"kItems{a}_{f}"
            lines.append(f"static const ItemVector {name}[] = {{")
            for it in items:
                lines.append(f"    {{{it[0]}, {it[1]}, {it[2]}, {it[3]}, {it[4]}, {it[5]}, {it[6]}, {it[7]}, "
                             f"0x{bits(it[8]):08x}, 0x{bits(it[9]):08x}, {it[10]}, {it[11]}}},")
            lines.append("};")
            frame_refs.append(f"{{{name}, {len(items)}}}")
    rows = [", ".join(frame_refs[a * 3:a * 3 + 3]) for a in range(len(actions))]
    lines.append(f"static const FrameVector kFrames[4][3] = {{{', '.join('{' + r + '}' for r in rows)}}};")

    records = {}
    for t, images in ((0, images8), (1, images32)):
        table = emu.read_u32(spr + (0x514 if t == 0 else 0x524))
        for i in range(len(images)):
            records[emu.read_u32(table + 4 * i)] = (t, i)
    shadow_table = emu.read_u32(shadow_spr + 0x514)
    obj = emu.scratch(size=0xA0)
    emu.replace_thiscall(ADD_SPRITE_QUAD, 3, o.add_quad)
    lines.append("// Objects {view, position, action, frame, scale, shadow scale, x offset, angle, depth")
    lines.append("// index, key colour, colour, flags 0x80 0x83 0x86 0x88, ground colour, shadow argument},")
    lines.append("// then the quads of the body {the first 8 values, image type, image, flags} and of the")
    lines.append("// shadow, and the pixel scale the body leaves at +0x48.")
    objects = []
    for k in range(240):
        view_index = k % len(views)
        view, scales = views[view_index]
        # the view's own globals, as Step left them for it
        for key, v in zip(SCALES, scales):
            emu.write_u32(SCALES[key], v)
        emu.mem_write(obj, bytes(0xA0))
        emu.write_u32(obj, RENDER_OBJECT_VTABLE)
        at = ats[view_index]
        pos = (f32(at[0] + rng.uniform(-60, 60)), f32(rng.uniform(-25, 5)), f32(at[2] + rng.uniform(-60, 60)))
        action, frame = rng.randrange(4), rng.randrange(3)
        scale = f32(rng.choice([1.0, 1.0, 0.5, 1.5, 0.0, 2.0]))
        shadow_scale = f32(rng.choice([1.0, 1.0, 0.0, 0.75]))
        offset_x, angle, depth_index = rng.choice([0, 0, 7, -12]), rng.choice([0, 0, 20, -45]), rng.randrange(0, 40)
        key_color = rng.choice([0xFFFF0000, 0xFFFF0000, 0xFF00FF00])
        color = rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 0x80FF8040, rng.getrandbits(32)])
        flags = [rng.random() < 0.2, rng.random() < 0.2, rng.random() < 0.15, rng.random() < 0.8]
        o.ground = tuple(f32(rng.choice([1.0, 1.0, 0.5, 0.8, 1.2])) for _ in range(3))
        shadow_arg = f32(rng.choice([1.0, 1.0, 0.0, 0.6]))
        emu.mem_write(obj + 4, struct.pack("<3f", *pos))
        emu.write_u32(obj + 0x20, action)
        emu.write_u32(obj + 0x24, action)
        emu.write_u32(obj + 0x28, frame)
        emu.mem_write(obj + 0x3C, struct.pack("<2f", scale, shadow_scale))
        emu.mem_write(obj + 0x5C, struct.pack("<iii", offset_x, angle, depth_index))
        emu.write_u32(obj + 0x68, key_color)
        emu.write_u32(obj + 0x7C, color)
        for offset, on in zip((0x80, 0x83, 0x86, 0x88), flags):
            emu.mem_write(obj + offset, bytes([int(on)]))
        emu.mem_write(obj + 0x84, bytes([1]))
        emu.write_u32(obj + 0x98, spr)
        emu.write_u32(obj + 0x9C, act)
        o.quads.clear()
        emu.call(DRAW_BODY, emu.scratch(struct.pack("<12I", *view)), this=obj)
        body = list(o.quads)
        pixel_scale = emu.read_u32(obj + 0x48)
        o.quads.clear()
        emu.call(DRAW_SHADOW, emu.scratch(struct.pack("<12I", *view)), bits(shadow_arg), this=obj)
        shadow = list(o.quads)
        assert len(shadow) <= 1
        name = f"kObject{k}"
        lines.append(f"static const QuadVector {name}Body[] = {{")
        for values, palette, fl in body:
            assert palette == spr + 0x110
            t, i = records[values[11]]
            lines.append(f"    {{{{{hexs(values[:8])}}}, {t}, {i}, 0x{fl:x}}},")
        if not body:
            lines.append("    {},")
        lines.append("};")
        shadow_text = "{}"
        if shadow:
            values, palette, fl = shadow[0]
            assert palette == shadow_spr + 0x110 and values[11] == emu.read_u32(shadow_table)
            shadow_text = f"{{{{{hexs(values[:8])}}}, 0, 0, 0x{fl:x}}}"
        objects.append(
            f"    {{{view_index}, {{{hexs(map(bits, pos))}}}, {action}, {frame}, 0x{bits(scale):08x}, "
            f"0x{bits(shadow_scale):08x}, {offset_x}, {angle}, {depth_index}, 0x{key_color:08x}, "
            f"0x{color:08x}, {{{', '.join(str(int(f)) for f in flags)}}}, {{{hexs(map(bits, o.ground))}}}, "
            f"0x{bits(shadow_arg):08x}, {name}Body, {len(body)}, {int(bool(shadow))}, {shadow_text}, "
            f"0x{pixel_scale:08x}}},")
    lines.append("static const ObjectVector kObjects[] = {")
    lines += objects
    lines.append("};")
    lines += sprite_quads(Oracle(), rng)
    lines += animation(rng)
    print("\n".join(lines))


if __name__ == "__main__":
    main()
