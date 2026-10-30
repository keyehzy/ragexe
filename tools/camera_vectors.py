"""Test vectors for the port's camera and projection (docs/render/camera.md), taken from
the original in the emulation oracle.

usage: .venv/bin/python -I tools/camera_vectors.py > tests/unit/camera_vectors.inc

  - SceneRenderer::SetSize (0040db70) at assorted sizes: the fields +0 to +0x30.
  - C3dAttr::GetHeight (00427150) on a synthetic attribute map, inside, on the edges and
    outside.
  - CView frames: vf01 (004cf1d0) resets the view, then each frame may turn it
    (AddToAngle, 004cf230), set its target pitch and distance as CGameMode does, and
    toggle g_isFixedCamera (00685ecc) before CView::vf04 (004cf260) follows the player
    over the attribute map. Recorded: the fields +0x1c to +0x48 and +0x7c to +0xf0.
  - CGameMode 0050d9e0 entering indoor and outdoor maps from assorted saved settings, then
    0050db70 leaving them.
  - CGameMode 005156d0's camera input: turning, pitching and zooming with and without
    Shift and Ctrl, indoors and outdoors, with ClientState's options 5 and 6, and the
    reset state.
  - CView frames with shakes: CView::StartShake (004cf450) and SetShakeAmplitudes
    (004cf4b0) as CGameMode::SendMsg calls them and otherwise, the clock (timeGetTime)
    stepped each frame and g_viewShakeStepPending (0067cab4) set on most frames, before
    vf04, whose ApplyShake (004cf2a0) draws on rand. Recorded: the camera as above, the
    shake +4 to +0x18 and ApplyShake's statics 0067ca90 to 0067ca9c.
  - ApplyShake alone, 2000 steps of a shake that lasts, from random pitches, distances,
    points and yaws: what it leaves of them.
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
GET_HEIGHT, ATTR_SIZE = 0x427150, 0x130
INIT_ANGLE_TABLES = 0x40B2F0
CVIEW_SIZE, CVIEW_RESET, CVIEW_UPDATE, ADD_TO_ANGLE = 0x1C0, 0x4CF1D0, 0x4CF260, 0x4CF230
GET_GAME_MODE, FIXED_CAMERA = 0x4CBF30, 0x685ECC
PLACE_CAMERA, LEAVE_MAP, IS_INDOOR_MAP = 0x50D9E0, 0x50DB70, 0x598BA0
CAMERA_INPUT, MOUSE_STATE, MOUSE_X, MOUSE_Y = 0x5156D0, 0x628B74, 0x628B5C, 0x628B60
CLIENT_OPTION, SET_CURSOR, SET_LISTENER_HEADING = 0x597950, 0x4CB7F0, 0x5B0FA0
VIEW_SETTINGS = 0x614884  # current, indoor and outdoor latitude and distance
SRAND, START_SHAKE, SET_SHAKE_AMPLITUDES = 0x5C2546, 0x4CF450, 0x4CF4B0
# The swings for the pitch, distance and point at 0x67ca90 to 0x67ca98, their guard at 0x67ca9c.
SHAKE_PENDING, SHAKE_SWINGS = 0x67CAB4, 0x67CA90
SHAKE_SEED = 77
APPLY_SHAKE, SHAKE_STEPS = 0x4CF2A0, 2000
CELL_SIZE = 5


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def bits(f):
    return struct.unpack("<I", struct.pack("<f", f))[0]


class Oracle:
    def __init__(self, attr_shape, attr_heights):
        emu = self.emu = Emulator().init_crt()
        emu.uc.reg_write(UC_X86_REG_FPCW, 0x007F)
        emu.call(INIT_ANGLE_TABLES)  # WinMain fills SinDegrees' and CosDegrees' tables at start-up
        self.renderer = emu.scratch(size=SCENE_RENDERER_SIZE)
        emu.call(SET_SIZE, 800, 600, this=self.renderer)
        emu.write_u32(G_PSCENE_RENDERER, self.renderer)
        w, h = attr_shape
        cells = b"".join(struct.pack("<4fi", *hs, 0) for hs in attr_heights)
        self.attr = emu.scratch(size=ATTR_SIZE)
        emu.write_u32(self.attr + 0x110, w)
        emu.write_u32(self.attr + 0x114, h)
        emu.write_u32(self.attr + 0x118, CELL_SIZE)
        emu.write_u32(self.attr + 0x120, emu.scratch(cells))
        self.player = emu.scratch(size=0x20)
        self.world = emu.scratch(size=0x100)
        emu.write_u32(self.world + 0x3C, self.player)
        emu.write_u32(self.world + 0x40, self.attr)
        self.view = emu.scratch(size=CVIEW_SIZE)
        emu.write_u32(self.view + 0x1B4, self.world)
        self.mode = emu.scratch(size=0x400)
        emu.write_u32(self.mode + 0xB4, self.world)
        emu.write_u32(self.mode + 0xB8, self.view)
        emu.replace_thiscall(GET_GAME_MODE, 0, lambda this: self.mode)
        self.indoor = 0
        emu.replace_thiscall(IS_INDOOR_MAP, 1, lambda this, name: self.indoor)
        self.options = {}
        emu.replace_thiscall(CLIENT_OPTION, 1, lambda this, n: self.options.get(n, 0))
        emu.replace_thiscall(SET_CURSOR, 1, lambda this, cursor: 0)
        emu.replace_thiscall(SET_LISTENER_HEADING, 0, lambda this: 0)
        self.keys = set()
        emu.api.stubs["USER32.DLL!GetAsyncKeyState"] = Stub(
            1, lambda key: 0x8000 if key in self.keys else 0)

    def floats(self, addr, n):
        return list(struct.unpack(f"<{n}I", self.emu.mem_read(addr, 4 * n)))

    def write_floats(self, addr, values):
        self.emu.mem_write(addr, struct.pack(f"<{len(values)}f", *values))

    def camera(self):
        return self.floats(self.view + 0x1C, 12) + self.floats(self.view + 0x7C, 30)


def sizes(oracle):
    out = []
    for w, h in [(640, 480), (800, 600), (1024, 768), (1280, 1024), (1600, 1200), (801, 601),
                 (320, 200), (1, 1), (1920, 1080), (7, 3)]:
        sr = oracle.emu.scratch(size=SCENE_RENDERER_SIZE)
        oracle.emu.call(SET_SIZE, w, h, this=sr)
        out.append((w, h, oracle.floats(sr, 13)))
    return out


def heights(oracle, rng, shape):
    w, h = shape
    half_x, half_z = w * CELL_SIZE / 2, h * CELL_SIZE / 2
    points = [(0.0, 0.0), (-half_x, -half_z), (half_x, half_z), (half_x - 0.001, half_z - 0.001),
              (-half_x - 0.5, 0.0), (-half_x - 4.9, 1.0), (0.0, -half_z - 6.0), (1e9, 3.0)]
    points += [(rng.uniform(-half_x - 8, half_x + 8), rng.uniform(-half_z - 8, half_z + 8))
               for _ in range(40)]
    out = []
    for x, z in points:
        x, z = f32(x), f32(z)
        oracle.emu.call(GET_HEIGHT, bits(x), bits(z), this=oracle.attr)
        out.append((bits(x), bits(z), bits(f32(oracle.emu.st0()))))
    return out


def frames(oracle, rng, shape):
    emu = oracle.emu
    emu.call(CVIEW_RESET, this=oracle.view)
    w, h = shape
    pos = [0.0, 0.0, 0.0]
    out = []
    for n in range(80):
        pos = [f32(pos[0] + rng.uniform(-3, 3)), f32(rng.uniform(-4, 4)), f32(pos[2] + rng.uniform(-3, 3))]
        if n == 50:
            pos = [f32(w * CELL_SIZE), 0.0, f32(-h * CELL_SIZE)]  # off the map
        oracle.write_floats(oracle.player + 4, pos)
        turn = f32(rng.choice([0.0, 0.0, rng.uniform(-30, 30), rng.uniform(-400, 400), 179.5, -181.0]))
        if turn != 0.0:
            emu.call(ADD_TO_ANGLE, bits(turn), this=oracle.view)
        retarget = rng.random() < 0.2
        pitch, distance = f32(rng.uniform(-65, -25)), f32(rng.uniform(230, 400))
        if retarget:
            oracle.write_floats(oracle.view + 0x34, [pitch])
            oracle.write_floats(oracle.view + 0x3C, [distance])
        fixed = 1 if 30 <= n < 40 else 0
        emu.write_u32(FIXED_CAMERA, fixed)
        emu.call(CVIEW_UPDATE, this=oracle.view)
        out.append(([bits(v) for v in pos], bits(turn), int(retarget), bits(pitch), bits(distance),
                     fixed, oracle.camera()))
    return out


# Frame: the shake calls made before it, (StartShake, active, length, three amplitudes) or
# (SetShakeAmplitudes, three amplitudes). The first shake's amplitudes are changed before
# its first step, so ApplyShake's statics take those; the later ones' it ignores. The
# 450 ms shake's last frame falls exactly on its end; the others' between frames.
SHAKE_CALLS = {
    6: [("start", 1, 0, 1.0, 0.24, 0.2), ("amplitudes", 1.37, -0.61, 0.373)],
    40: [("start", 1, 1, 1.0, 0.24, 0.2)],
    55: [("start", 1, 450, 3.0, 2.0, 1.0)],
    60: [("amplitudes", 0.5, 0.5, 0.5)],
    90: [("start", 0, 0, 1.0, 0.24, 0.2)],
    100: [("start", 1, 0, 1.0, 0.24, 0.2)],
    104: [("start", 1, 0xFFFFFFF0, 1.0, 0.24, 0.2)],
    130: [("start", 1, 3000, 1.0, 0.24, 0.2)],
}


def shakes(oracle, rng):
    emu = oracle.emu
    now = [5000]
    emu.api.stubs["WINMM.DLL!timeGetTime"] = Stub(0, lambda: now[0])
    emu.call(SRAND, SHAKE_SEED)
    emu.call(CVIEW_RESET, this=oracle.view)
    pos = [0.0, 0.0, 0.0]
    out = []
    end = None
    for n in range(260):
        now[0] = (now[0] + (300 if n in (80, 112) else rng.randrange(10, 40))) & 0xFFFFFFFF
        if end is not None and now[0] >= end:
            now[0], end = end, None
        pos = [f32(pos[0] + rng.uniform(-2, 2)), f32(rng.uniform(-4, 4)), f32(pos[2] + rng.uniform(-2, 2))]
        oracle.write_floats(oracle.player + 4, pos)
        turn = f32(rng.choice([0.0, 0.0, 0.0, rng.uniform(-30, 30)]))
        if turn != 0.0:
            emu.call(ADD_TO_ANGLE, bits(turn), this=oracle.view)
        calls = []
        for call in SHAKE_CALLS.get(n, []):
            if call[0] == "start":
                _, active, length, *amps = call
                if length == 450:
                    end = now[0] + length
                emu.call(START_SHAKE, active, length, *(bits(f32(a)) for a in amps), this=oracle.view)
                calls.append((1, active, length, *(bits(f32(a)) for a in amps)))
            else:
                amps = [bits(f32(a)) for a in call[1:]]
                emu.call(SET_SHAKE_AMPLITUDES, *amps, this=oracle.view)
                calls.append((2, 0, 0, *amps))
        pending = int(rng.random() < 0.8)
        emu.write_u32(SHAKE_PENDING, pending)
        fixed = 1 if 70 <= n < 75 else 0
        emu.write_u32(FIXED_CAMERA, fixed)
        emu.call(CVIEW_UPDATE, this=oracle.view)
        state = oracle.camera() + oracle.floats(oracle.view + 4, 6) + oracle.floats(SHAKE_SWINGS, 3)
        state += [emu.read_u32(SHAKE_SWINGS + 0xC) & 0xFF, emu.read_u32(SHAKE_PENDING)]
        out.append((now[0], [bits(v) for v in pos], bits(turn), calls, pending, fixed, state))
    return out


def shake_steps(oracle, rng):
    """After shakes, so the statics hold their swings: ApplyShake on a shake started now that
    lasts, its step pending each time."""
    emu = oracle.emu
    emu.call(SRAND, SHAKE_SEED + 1)
    view = oracle.view
    emu.write_u32(view + 4, 1)
    emu.write_u32(view + 0x18, 0x7FFFFFFF)
    out = []
    for _ in range(SHAKE_STEPS):
        emu.write_u32(view + 8, emu.api.stubs["WINMM.DLL!timeGetTime"].fn())
        before = [f32(rng.uniform(-65, -25)), f32(rng.uniform(0, 360)), f32(rng.uniform(230, 400)),
                  f32(rng.uniform(-500, 500)), f32(rng.uniform(-500, 500))]
        oracle.write_floats(view + 0x1C, before[:3])
        oracle.write_floats(view + 0x28, [before[3]])
        oracle.write_floats(view + 0x30, [before[4]])
        emu.write_u32(SHAKE_PENDING, 1)
        emu.call(APPLY_SHAKE, this=view)
        after = oracle.floats(view + 0x1C, 1) + oracle.floats(view + 0x24, 1) + oracle.floats(view + 0x28, 1)
        after += oracle.floats(view + 0x30, 1)
        out.append(([bits(v) for v in before], after))
    return out


def placements(oracle, rng):
    emu = oracle.emu
    out = []
    cases = [(1, (-45.0, 300.0), (-45.0, 300.0)), (0, (-45.0, 300.0), (-45.0, 300.0)),
             (1, (-70.0, 500.0), (-10.0, 100.0)), (0, (-70.0, 500.0), (-10.0, 100.0)),
             (0, (-50.0, 250.0), (-30.5, 399.5)), (1, (-35.0, 230.0), (-65.0, 401.0)),
             (0, (0.0, 0.0), (-64.75, 230.25)), (1, (-55.25, 299.75), (20.0, -5.0))]
    for indoor, indoor_view, outdoor_view in cases:
        emu.call(CVIEW_RESET, this=oracle.view)
        yaw = f32(rng.uniform(0, 360))
        oracle.write_floats(oracle.view + 0x20, [yaw])
        oracle.write_floats(oracle.view + 0x38, [yaw])
        player = [f32(rng.uniform(-100, 100)) for _ in range(3)]
        oracle.write_floats(oracle.player + 4, player)
        oracle.write_floats(VIEW_SETTINGS, [1.0, 2.0, *indoor_view, *outdoor_view])
        oracle.indoor = indoor
        emu.call(PLACE_CAMERA, this=oracle.mode)
        placed = (oracle.camera()[:12], oracle.floats(VIEW_SETTINGS, 6),
                  emu.read_u32(oracle.mode + 0x250), emu.read_u32(oracle.mode + 0x270))
        oracle.write_floats(VIEW_SETTINGS, [f32(rng.uniform(-60, -30)), f32(rng.uniform(230, 400))])
        current = oracle.floats(VIEW_SETTINGS, 2)
        emu.call(LEAVE_MAP, this=oracle.mode)
        out.append((indoor, [bits(f32(v)) for v in (*indoor_view, *outdoor_view)], bits(yaw),
                    [bits(v) for v in player], placed, current, oracle.floats(VIEW_SETTINGS, 6)))
    return out


def inputs(oracle, rng):
    emu = oracle.emu
    out = []
    for n in range(60):
        kind = 4 if n % 6 == 5 else 2
        indoor = rng.random() < 0.5
        keys = {k for k in (0x10, 0x11) if rng.random() < 0.5}
        options = {k: 1 for k in (5, 6) if rng.random() < 0.25}
        mouse = (rng.randint(0, 1023), rng.randint(0, 767))
        last = (mouse[0] + rng.choice([0, rng.randint(-40, 40), rng.randint(-700, 700)]),
                mouse[1] + rng.choice([0, 1, -1, rng.randint(-60, 60), rng.randint(-500, 500)]))
        indoor_yaw = f32(rng.choice([45.0, rng.uniform(0, 360)]))
        targets = [f32(rng.uniform(-70, -20)), f32(rng.uniform(-30, 400)), f32(rng.uniform(200, 420))]
        emu.write_u32(MOUSE_STATE, kind)
        emu.write_u32(MOUSE_X, mouse[0])
        emu.write_u32(MOUSE_Y, mouse[1])
        emu.write_u32(oracle.mode + 0xC4, last[0])
        emu.write_u32(oracle.mode + 0xC8, last[1])
        emu.write_u32(oracle.mode + 0x250, 0 if indoor else 1)
        oracle.write_floats(oracle.mode + 0x270, [indoor_yaw])
        oracle.write_floats(oracle.view + 0x34, targets[:1])
        oracle.write_floats(oracle.view + 0x38, targets[1:2])
        oracle.write_floats(oracle.view + 0x3C, targets[2:])
        oracle.write_floats(VIEW_SETTINGS, [-1.0, -2.0])
        oracle.keys, oracle.options = keys, options
        emu.call(CAMERA_INPUT, this=oracle.mode)
        after = (oracle.floats(oracle.view + 0x34, 3) + oracle.floats(VIEW_SETTINGS, 2))
        out.append((kind, int(indoor), int(0x10 in keys), int(0x11 in keys), options.get(6, 0),
                    options.get(5, 0), *mouse, *last, bits(indoor_yaw), [bits(v) for v in targets],
                    after))
    return out


def attr_height(cell, corner):
    """The test map's height at a cell's corner: whole numbers from 20 up to -310, so that
    the ground rises above the eye in places."""
    return float(20 - (cell * 7 + corner * 13) * 29 % 331)


def hexes(values):
    return ", ".join(f"0x{v:08x}" for v in values)


def main():
    rng = random.Random(41)
    shape = (128, 96)
    attr_heights = [[attr_height(c, k) for k in range(4)] for c in range(shape[0] * shape[1])]
    oracle = Oracle(shape, attr_heights)
    lines = ["// Generated by tools/camera_vectors.py from the original in the emulation oracle."]
    lines.append("// {width, height, SetSize's fields +0 to +0x30}.")
    lines.append("static const SizeVector kSizeVectors[] = {")
    lines += [f"    {{{w}, {h}, {{{hexes(f)}}}}}," for w, h, f in sizes(oracle)]
    lines.append("};")
    lines.append("// The attribute map, its corner heights from AttrTestHeight.")
    lines.append(f"static constexpr std::int32_t kAttrWidth = {shape[0]}, kAttrHeight = {shape[1]};")
    lines.append("// {x, z, GetHeight}.")
    lines.append("static const HeightVector kHeightVectors[] = {")
    lines += [f"    {{0x{x:08x}, 0x{z:08x}, 0x{r:08x}}}," for x, z, r in heights(oracle, rng, shape)]
    lines.append("};")
    lines.append("// {player, turn, targets set, target pitch, target distance, g_isFixedCamera, the view's")
    lines.append("// fields +0x1c to +0x48 and +0x7c to +0xf0 after vf04}.")
    lines.append("static const CameraFrame kCameraFrames[] = {")
    for player, turn, retarget, pitch, distance, fixed, state in frames(oracle, rng, shape):
        lines.append(f"    {{{{{hexes(player)}}}, 0x{turn:08x}, {retarget}, 0x{pitch:08x}, "
                     f"0x{distance:08x}, {fixed},")
        lines.append(f"     {{{hexes(state)}}}}},")
    lines.append("};")
    lines.append("// {indoor, saved indoor and outdoor latitude and distance, yaw, player; after entering:")
    lines.append("// the view's fields +0x1c to +0x48, the settings 00614884 to 00614898, CGameMode")
    lines.append("// +0x250 and +0x270; the current setting before leaving, the settings after}.")
    lines.append("static const PlaceVector kPlaceVectors[] = {")
    for indoor, saved, yaw, player, placed, current, left in placements(oracle, rng):
        view, settings, outdoor_flag, indoor_yaw = placed
        lines.append(f"    {{{indoor}, {{{hexes(saved)}}}, 0x{yaw:08x}, {{{hexes(player)}}},")
        lines.append(f"     {{{hexes(view)}}},")
        lines.append(f"     {{{hexes(settings)}}}, {outdoor_flag}, 0x{indoor_yaw:08x}, "
                     f"{{{hexes(current)}}}, {{{hexes(left)}}}}},")
    lines.append("};")
    lines.append("// {mouse state, indoor, Shift, Ctrl, option 6, option 5, mouse x, y, last x, y,")
    lines.append("// CGameMode+0x270, target pitch, yaw, distance before; the targets and 00614884,")
    lines.append("// 00614888 after (-1 and -2 before)}.")
    lines.append("static const DragVector kDragVectors[] = {")
    for row in inputs(oracle, rng):
        *head, indoor_yaw, targets, after = row
        lines.append(f"    {{{', '.join(map(str, head))}, 0x{indoor_yaw:08x}, {{{hexes(targets)}}}, "
                     f"{{{hexes(after)}}}}},")
    lines.append("};")
    lines.append(f"static constexpr std::uint32_t kShakeSeed = {SHAKE_SEED};")
    lines.append("// {time, player, turn, the shake calls before it ({1 StartShake or 2 SetShakeAmplitudes,")
    lines.append("// active, length, three amplitudes}), g_viewShakeStepPending, g_isFixedCamera; the camera")
    lines.append("// as in kCameraFrames, the shake +4 to +0x18, ApplyShake's swings 0067ca90 to 0067ca98,")
    lines.append("// their guard bits and the step pending after vf04}.")
    lines.append("static const ShakeFrame kShakeFrames[] = {")
    for time, player, turn, calls, pending, fixed, state in shakes(oracle, rng):
        padded = calls + [(0, 0, 0, 0, 0, 0)] * (2 - len(calls))
        lines.append(f"    {{{time}u, {{{hexes(player)}}}, 0x{turn:08x}, "
                     f"{{{', '.join('{' + hexes(c) + '}' for c in padded)}}}, {pending}, {fixed},")
        lines.append(f"     {{{hexes(state)}}}}},")
    lines.append("};")
    lines.append("// {pitch, yaw, distance, the point's x and z before ApplyShake; pitch, distance, x and z")
    lines.append("// after}, seeded with kShakeSeed + 1.")
    lines.append("static const ShakeStep kShakeSteps[] = {")
    lines += [f"    {{{{{hexes(b)}}}, {{{hexes(a)}}}}}," for b, a in shake_steps(oracle, rng)]
    lines.append("};")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
