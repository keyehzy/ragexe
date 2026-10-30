"""Test vectors for the port's on-screen action frames and the cursor
(docs/render/actor.md#on-the-screen), taken from the original in the emulation oracle.

usage: .venv/bin/python -I tools/cursor_vectors.py > tests/unit/cursor_vectors.inc

  - SceneRenderer::DrawActFrame (00410620) on synthetic actions of 8-bit and 32-bit images:
    assorted positions, scales, angles (0, NaN and turned), colours either side of
    0xff000000, the z test and point filtering, with the device filtering by points or not
    (C3dDevice+0x1e4, 00623bb4) and the bound texture (+0x50) given. The image records sit in
    a CSprRes's tables (+0x514, +0x524); the texture cache (FindCachedSprite 0040e540,
    CacheSpriteImage 0040e130) answers a texture and where it put the image. Every call the
    fake Direct3D device sees is recorded, each draw with its four vertices.
  - CMode::DrawCursor (004cb830) for each cursor action 0 to 12 at assorted times since its
    start (+0x24), offsets (+0x30, +0x34), mouse points (00628b5c, 00628b60) and button
    states (00628b70): the frame it leaves (+0x3c), the start, and what it asks DrawActFrame
    to draw.
The actions are real CActRes structures, so GetFrame, GetItem, GetFrameCount and
GetActionDelay run as the original's. The x87 runs at single precision, as DirectDraw's
FPUSETUP leaves it in the client.
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

DRAW_ACT_FRAME, DRAW_CURSOR, INIT_ANGLE_TABLES = 0x410620, 0x4CB830, 0x40B2F0
FIND_CACHED, CACHE_IMAGE = 0x40E540, 0x40E130
GET_RESOURCE_MANAGER, GET_RESOURCE, WINDOW_MESSAGE = 0x4DFA10, 0x4DFF60, 0x4B3970
SCENE_RENDERER_SIZE, G_PSCENE_RENDERER, DEVICE_POINT_FILTER = 0x2A0, 0x625E98, 0x623BB4
MOUSE_X, MOUSE_Y, MOUSE_BUTTON = 0x628B5C, 0x628B60, 0x628B70
MODE_SIZE = 0x40


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
        emu.call(INIT_ANGLE_TABLES)
        self.device = emu.directx.create_device()
        self.renderer = emu.scratch(size=SCENE_RENDERER_SIZE)
        emu.write_u32(self.renderer + 0x78, self.device.address)
        emu.write_u32(G_PSCENE_RENDERER, self.renderer)
        # Texture ids 1 to 8: CTextures whose surface (+0xc) the fake device knows.
        self.textures = {}   # CTexture address -> id
        self.surfaces = {}   # surface address -> id
        for k in range(1, 9):
            surface = emu.directx.create_texture(16, 16)
            texture = emu.scratch(size=0x128)
            emu.write_u32(texture + 0xC, surface.address)
            self.textures[k] = texture
            self.surfaces[surface.address] = k
        self.cache = {}  # image record -> (texture id, uv)

        def cached(this, image, palette, out):
            texture, uv = self.cache[image]
            emu.mem_write(out + 0xC, struct.pack("<4f", *uv))
            return self.textures.get(texture, 0)

        # FindCachedSprite misses for some images; CacheSpriteImage then answers.
        emu.replace_thiscall(FIND_CACHED, 3, lambda this, image, palette, out: (
            0 if self.cache[image][0] % 3 == 2 else cached(this, image, palette, out)))
        emu.replace_thiscall(CACHE_IMAGE, 3, cached)
        self.resources = {}
        # __cdecl, so a replacement that pops nothing; GetResource reads the name.
        emu.replace_thiscall(GET_RESOURCE_MANAGER, 0, lambda this: 0x1234)
        emu.replace_thiscall(GET_RESOURCE, 1, lambda this, name: self.resources[emu.read_cstr(name)])
        self.messages = []

        def window_message(this, message, a, b, c):
            self.messages.append((message, a, b, c))
            return 0

        emu.replace_thiscall(WINDOW_MESSAGE, 4, window_message)
        self.now = 0
        emu.api.stubs["WINMM.DLL!timeGetTime"] = Stub(0, lambda: self.now)

    def sprite(self, images8, images32):
        """A CSprRes: the image records (width, height, halved x, halved y) of each type in
        the tables at +0x514 and +0x524, a 32-bit one's CTexture at +8 of its record."""
        emu = self.emu
        spr = emu.scratch(size=0x534)
        for offset, images in ((0x510, images8), (0x520, images32)):
            records = []
            for image in images:
                texture = self.textures.get(image[4], 0) if len(image) > 4 else 0
                record = emu.scratch(struct.pack("<4hI", *image[:4], texture) + bytes(4))
                records.append(record)
                if len(image) > 4 and offset == 0x510:
                    self.cache[record] = (image[4], image[5])
            table = emu.scratch(b"".join(struct.pack("<I", r) for r in records) or bytes(4))
            emu.write_u32(spr + offset + 4, table)
            emu.write_u32(spr + offset + 8, table + 4 * len(records))
        return spr

    def act(self, actions, delays):
        """A CActRes: 16-byte actions at +0x114 holding their 0x4c-byte frames at +4, each
        frame its 0x24-byte items at +0x24 and their count at +0x30; the delays at +0x138."""
        emu = self.emu
        act = emu.scratch(size=0x144)
        records = b""
        for frames in actions:
            frame_bytes = b""
            frame_list = []
            for items in frames:
                data = b"".join(struct.pack("<iiiI4BffiI", *it) for it in items)
                begin = emu.scratch(data or bytes(4))
                frame = bytearray(0x4C)
                struct.pack_into("<II", frame, 0x24, begin, begin + len(data))
                struct.pack_into("<I", frame, 0x2C, begin + len(data))
                struct.pack_into("<I", frame, 0x30, len(items))
                frame_list.append(bytes(frame))
            frame_bytes = b"".join(frame_list)
            begin = emu.scratch(frame_bytes or bytes(4), align=4)
            records += struct.pack("<4I", 0, begin, begin + len(frame_bytes), begin + len(frame_bytes))
        table = emu.scratch(records or bytes(4))
        emu.write_u32(act + 0x114, table)
        emu.write_u32(act + 0x118, table + len(records))
        delay_table = emu.scratch(b"".join(struct.pack("<f", d) for d in delays) or bytes(4))
        emu.write_u32(act + 0x138, delay_table)
        emu.write_u32(act + 0x13C, delay_table + 4 * len(delays))
        return act

    def calls(self, start):
        """The device calls from index start on: (kind, stage, state, value, fvf) and, for a
        draw, its vertices' 32 values."""
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
            else:
                raise SystemExit(f"unexpected {c.method}")
        return rows


def random_sprite(o, rng):
    images8 = []
    for _ in range(rng.randrange(1, 6)):
        uv = [f32(rng.choice([0.0, 0.125, 0.25, 0.5])), f32(rng.choice([0.0, 0.25, 0.5]))]
        uv += [f32(uv[0] + rng.randrange(1, 129) / 256), f32(uv[1] + rng.randrange(1, 129) / 256)]
        images8.append((rng.randrange(1, 90), rng.randrange(1, 90), rng.randrange(2), rng.randrange(2),
                        rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 8]), uv))
    images32 = [(rng.randrange(1, 90), rng.randrange(1, 90), rng.randrange(2), rng.randrange(2),
                 rng.choice([0, 6, 7, 8])) for _ in range(rng.randrange(0, 3))]
    return images8, images32


def random_items(rng, n8, n32):
    items = []
    for _ in range(rng.randrange(0, 6)):
        kind = 1 if n32 and rng.random() < 0.3 else 0
        image = rng.randrange(n32 if kind else n8)
        # Some points past 2**24, where a float no longer holds every integer.
        far = rng.random() < 0.08
        x = rng.choice([1, -1]) * rng.randrange(1 << 24, 1 << 26) if far else rng.randrange(-70, 40)
        y = rng.choice([1, -1]) * rng.randrange(1 << 24, 1 << 26) if far else rng.randrange(-110, 20)
        items.append((x, y, image, rng.choice([0, 0, 1, 2, 3]),
                      rng.randrange(256), rng.randrange(256), rng.randrange(256), rng.randrange(256),
                      f32(rng.choice([1.0, 1.0, -1.0, 0.5, 1.37, 2.25, 0.3])),
                      f32(rng.choice([1.0, 1.0, 0.75, 1.5, -1.0, 0.61])),
                      rng.choice([0, 0, 15, -30]), kind))
    return items


def emit_act(lines, name, actions, images8, images32):
    """The synthetic act and sprite as flat C++ arrays: the items, each frame's action and
    its run of items, and the images of each type."""
    items, frames = [], []
    for a, action in enumerate(actions):
        for frame in action:
            frames.append(f"{{{a}, {len(items)}, {len(frame)}}}")
            items += [f"{{{x}, {y}, {im}, {fl}, {{{r}, {g}, {b}, {al}}}, 0x{bits(sx):08x}, "
                      f"0x{bits(sy):08x}, {an}, {k}}}" for x, y, im, fl, r, g, b, al, sx, sy, an, k in frame]
    lines.append(f"static const ItemVector {name}Items[] = {{{', '.join(items) or '{}'}}};")
    lines.append(f"static const FrameVector {name}Frames[] = {{{', '.join(frames) or '{}'}}};")
    lines.append(f"static const Image8Vector {name}Images8[] = {{"
                 + (", ".join(f"{{{w}, {h}, {hx}, {hy}, {t}, {{{hexs(map(bits, uv))}}}}}"
                              for w, h, hx, hy, t, uv in images8) or "{}") + "};")
    lines.append(f"static const Image32Vector {name}Images32[] = {{"
                 + (", ".join(f"{{{w}, {h}, {hx}, {hy}, {t}}}" for w, h, hx, hy, t in images32) or "{}") + "};")
    lines.append(f"static const ActVector {name} = {{{len(actions)}, {name}Items, {name}Frames, {len(frames)}, "
                 f"{name}Images8, {len(images8)}, {name}Images32, {len(images32)}}};")


def calls_lines(rows):
    out = []
    for kind, stage, state, value, fvf, vertices in rows:
        v = f"{{{hexs(vertices)}}}" if vertices else "{}"
        out.append(f"    {{{{SceneCall::Kind::{kind}, {stage}, {state}, 0x{value:x}, 0x{fvf:x}}}, {v}}},")
    return out


def act_frames(o, rng, lines):
    emu = o.emu
    cases = []
    for n in range(40):
        images8, images32 = random_sprite(o, rng)
        actions = [[random_items(rng, len(images8), len(images32)) for _ in range(rng.randrange(1, 4))]
                   for _ in range(rng.randrange(1, 4))]
        spr = o.sprite(images8, images32)
        act = o.act(actions, [])
        name = f"kAct{n}"
        emit_act(lines, name, actions, images8, images32)
        for k in range(8):
            action = rng.randrange(len(actions) + 1) if rng.random() < 0.15 else rng.randrange(len(actions))
            frame = rng.randrange(4)
            x, y = rng.randrange(-100, 900), rng.randrange(-100, 700)
            if k == 7:
                # Past 2**24 every odd integer lies midway between two floats.
                x, y = (rng.choice([1, -1]) * rng.randrange(1 << 24, 1 << 25) for _ in range(2))
            scale = f32(rng.choice([1.0, 1.0, 0.5, 1.75, 2.0, 0.83]))
            angle = rng.choice([0.0, 0.0, 0.0, -0.0, math.nan, 15.0, -30.0, 90.0, 400.0, 37.6, -400.2, 0.5])
            angle = f32(angle) if not math.isnan(angle) else angle
            color = rng.choice([0xFFFFFFFF, 0xFF000000, 0xFAFFFFFF, 0xFF000001, rng.getrandbits(32)])
            z_test, point = rng.randrange(2), rng.randrange(2)
            device_point = rng.randrange(2)
            bound = rng.choice([0, 1, 2, 3])
            emu.write_u32(DEVICE_POINT_FILTER, device_point)
            emu.write_u32(o.renderer + 0x50, o.textures.get(bound, 0))
            start = len(emu.directx.calls)
            angle_bits = 0x7FC00000 if math.isnan(angle) else bits(angle)
            emu.call(DRAW_ACT_FRAME, x & 0xFFFFFFFF, y & 0xFFFFFFFF, act, spr, action, frame, bits(scale),
                     angle_bits, color, z_test, point, this=o.renderer)
            rows = o.calls(start)
            after = emu.read_u32(o.renderer + 0x50)
            after_id = next((i for i, t in o.textures.items() if t == after), 0)
            lines.append(f"static const CallVector kAct{n}Calls{k}[] = {{")
            lines += calls_lines(rows) or ["    {},"]
            lines.append("};")
            cases.append(f"    {{&{name}, {{{x}, {y}, {action}, {frame}, 0x{bits(scale):08x}, 0x{angle_bits:08x}, "
                         f"0x{color:08x}, {z_test}, {point}}}, {device_point}, {bound}, kAct{n}Calls{k}, "
                         f"{len(rows)}, {after_id}}},")
    lines.append("// {the act and sprite; {x, y, action, frame, scale, angle, colour, z test, point filter};")
    lines.append("// the device's point filtering; the bound texture; the calls and their count; the bound")
    lines.append("// texture after}. Texture 0 is none.")
    lines.append("static const ActFrameVector kActFrames[] = {")
    lines += cases
    lines.append("};")


def cursors(o, rng, lines):
    emu = o.emu
    # cursors.act's shape: actions of 1 to 12 frames, one item each.
    images8 = [(rng.randrange(8, 40), rng.randrange(8, 40), 0, 0, 1 + k % 3,
                [0.0, 0.0, f32(0.125), f32(0.125)]) for k in range(6)]
    # Actions 6 and 8 of fewer than two frames, which count as one.
    counts = [4, 5, 8, 3, 12, 6, 1, 7, 0, 9, 10, 5, 2]
    actions = []
    for a in range(13):
        count = counts[a]
        actions.append([[(rng.randrange(-10, 0), rng.randrange(-10, 0), rng.randrange(6), 0,
                          255, 255, 255, 255, 1.0, 1.0, 0, 0)] for _ in range(count)])
    spr = o.sprite(images8, [])
    act = o.act(actions, [f32(4.0)] * 13)
    o.resources["cursors.act"] = act
    o.resources["cursors.spr"] = spr
    emit_act(lines, "kCursorAct", actions, images8, [])
    mode = emu.scratch(size=MODE_SIZE)
    seen = []
    emu.replace_thiscall(DRAW_ACT_FRAME, 11, lambda this, *args: seen.append(args) or 0)
    rows = []
    for n in range(500):
        action = n % 13 if n < 390 else rng.randrange(13)
        start = rng.getrandbits(32)
        elapsed = rng.choice([rng.randrange(0, 200), rng.randrange(0, 3000), rng.randrange(0, 100000),
                              rng.getrandbits(32)])
        if rng.random() < 0.4:
            # Up to three times through the action, across where playing once stops.
            step = {5: 20, 7: 10, 9: 1, 10: 6, 11: 3}.get(action, 4)
            elapsed = rng.randrange(0, 24 * step * max(counts[action], 1) * 3 + 1)
        o.now = (start + elapsed) & 0xFFFFFFFF
        offset = (f32(rng.choice([0.0, 0.0, -2.5, 3.75, 0.5, -0.5, 7.0])),
                  f32(rng.choice([0.0, 0.0, -1.25, 2.5, 0.5, -0.5])))
        mouse = (rng.randrange(-20, 1100), rng.randrange(-20, 800))
        button = rng.choice([0, 1, 1, 0x80])
        frame_before = rng.randrange(20)
        emu.mem_write(mode + 0x24, struct.pack("<I", start))
        emu.mem_write(mode + 0x30, struct.pack("<2fII", *offset, action, frame_before))
        emu.write_u32(MOUSE_X, mouse[0] & 0xFFFFFFFF)
        emu.write_u32(MOUSE_Y, mouse[1] & 0xFFFFFFFF)
        emu.write_u32(MOUSE_BUTTON, button)
        emu.write_u32(DEVICE_POINT_FILTER, 0)
        emu.write_u32(o.renderer + 0x50, 0)
        o.messages = []
        seen.clear()
        emu.call(DRAW_CURSOR, this=mode)
        frame_after = emu.read_u32(mode + 0x3C)
        start_after = emu.read_u32(mode + 0x24)
        assert len(seen) == 1 and o.messages == [(0x10, frame_after, 0, 0)], (seen, o.messages)
        x, y, a_act, a_spr, a_action, a_frame, scale, angle, color, z_test, point = seen[0]
        assert (a_act, a_spr, a_action, a_frame) == (act, spr, action, frame_after)
        sx = x - (1 << 32) if x >= 1 << 31 else x
        sy = y - (1 << 32) if y >= 1 << 31 else y
        rows.append(f"    {{{action}, {frame_before}, {start}u, {o.now}u, 0x{bits(offset[0]):08x}, "
                    f"0x{bits(offset[1]):08x}, {mouse[0]}, {mouse[1]}, {button}, {frame_after}, {start_after}u, "
                    f"{{{sx}, {sy}, 0x{scale:08x}, 0x{angle:08x}, 0x{color:08x}, {z_test}, {point}}}}},")
    lines.append("// {action (+0x38), frame before (+0x3c), start (+0x24), timeGetTime, offset x and y")
    lines.append("// (+0x30, +0x34), mouse x and y, button; the frame and start after; what DrawActFrame is")
    lines.append("// asked: {x, y, scale, angle, colour, z test, point filter}}.")
    lines.append("static const CursorVector kCursors[] = {")
    lines += rows
    lines.append("};")


def main():
    rng = random.Random(61)
    o = Oracle()
    lines = ["// Generated by tools/cursor_vectors.py from the original in the emulation oracle."]
    act_frames(o, rng, lines)
    cursors(o, rng, lines)
    print("\n".join(lines))


if __name__ == "__main__":
    main()
