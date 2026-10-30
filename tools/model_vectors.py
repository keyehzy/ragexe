"""Test vectors for the port's model instances (docs/render/model.md), taken from the
original in the emulation oracle.

usage: RAGEXE_MODELS=DIR .venv/bin/python -I tools/model_vectors.py > tests/unit/model_vectors.inc

DIR holds data.grf's models as `ragres extract` writes them (DIR/data/model/...).

Each model (synthetic ones of every version from tools/rsm_oracle.py, and real ones from
data.grf) goes through C3dModelRes::Load (0042be00), then into a ModelInstance placed as
CWorld::LoadWorld (004c3ab0) places one: the constructor (004b9340), the position,
rotation, scale and animation speed, Setup (004b9820) and Light (004ba270) with a light.
TextureCache::GetTexture (0040cc00) answers each name with a texture object whose pixel
format (+0x10) is 1 (ARGB 4444) for a name of odd length; C3dGround15::GetCellColor
(004c03b0) is replaced by a function of its point's bits (CellColor below). Recorded:
the instance's fields +0xc0 to +0x1e4, each node's alpha, matrix, box, face colours and
ground colours, depth first; then every primitive Render (004b9740) hands
SceneRenderer::AddPrimitive (00412550) under a few views, with and without clipping, the
fade's other sets and translucency; and the node matrices after AdvanceAnimation
(004b96a0) steps.
The loader runs at the CRT's double precision, as on the client's lazy loading thread and
as tools/rsm_oracle.py checks the port's loader; the rest at single precision, as
DirectDraw's FPUSETUP leaves the main thread.
"""
import glob
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from unicorn.x86_const import UC_X86_REG_FPCW  # noqa: E402

from emu import Emulator  # noqa: E402
from emu.winapi import WINDOW  # noqa: E402
from rsm_oracle import model as synthetic_model  # noqa: E402

C3DMODELRES_CTOR, C3DMODELRES_LOAD, C3DMODELRES_SIZE = 0x42BB00, 0x42BE00, 0x1A8
GET_TEXTURE, GET_CELL_COLOR, GET_GAME_MODE = 0x40CC00, 0x4C03B0, 0x4CBF30
INSTANCE_CTOR, INSTANCE_SIZE, COPY_VOLUMES = 0x4B9340, 0x200, 0x4B9550
SETUP, LIGHT, RENDER, ADVANCE = 0x4B9820, 0x4BA270, 0x4B9740, 0x4B96A0
SET_TRANSLUCENT, SLERP = 0x4B7DA0, 0x401050
G_C3DDEVICE, C3DDEVICE_CTOR, C3DDEVICE_INIT, IID_HAL_DEVICE = 0x6239D0, 0x404240, 0x404410, 0x5EC118
SCENE_RENDERER_CTOR, SCENE_RENDERER_SIZE, SET_SIZE = 0x40D1A0, 0x2A0, 0x40DB70
G_PSCENE_RENDERER, ADD_PRIMITIVE = 0x625E98, 0x412550
WORLD_COLOR_FADE, GROUND_WATER_FADE = 0x686598, 0x679F9C
SPEED_SCALE = 1.0 / 3.0  # 005e8968

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REAL_MODELS = [  # small ones covering the shade types, hierarchies and rotation keys
    "%BF%DC%BA%CE%BC%D2%C7%B0/%C6%AE%B7%A602.rsm",
    "%C7%D8%BA%AF%B8%B6%C0%BB/%B0%A3%C6%C7_%C4%AB%C7%C1%B6%F3.rsm",
    "%B1%DB%B7%A1%BD%BA%C6%AE/%B1%DB%B7%A1%BD%BA%C6%AE_%B1%E2%B5%D5%C3%A2%BB%EC.rsm",
    "%C6%E4%C0%CC%BF%E6/%C6%E4%C0%CC%BF%E6%C1%D901.rsm",
    "%BF%DC%BA%CE%BC%D2%C7%B0/%C6%F3%B0%A1%BA%AE.rsm",
]


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def bits(f):
    return struct.unpack("<I", struct.pack("<f", f))[0]


def cell_color(x, y, z):
    """The stand-in for GetCellColor: channels from the point's bits."""
    return ((bits(x) >> 7) & 0x7F, (bits(y) >> 9) & 0x7F, (bits(z) >> 5) & 0x7F)


def texture_names(data):
    """The header's texture names, which the faces' texture indices are written against."""
    minor = data[5]
    at = 6 + 8 + (1 if minor >= 4 else 0) + 16
    count = struct.unpack_from("<i", data, at)[0]
    return [data[at + 4 + 40 * k:at + 44 + 40 * k].split(b"\0")[0] for k in range(count)]


def look_at(eye, target):
    f = [t - e for t, e in zip(target, eye)]
    n = math.sqrt(sum(c * c for c in f))
    f = [c / n for c in f]
    up = (0.0, 1.0, 0.0)
    s = [up[1] * f[2] - up[2] * f[1], up[2] * f[0] - up[0] * f[2], up[0] * f[1] - up[1] * f[0]]
    n = math.sqrt(sum(c * c for c in s))
    s = [c / n for c in s]
    u = [f[1] * s[2] - f[2] * s[1], f[2] * s[0] - f[0] * s[2], f[0] * s[1] - f[1] * s[0]]
    dot = [-sum(a * b for a, b in zip(axis, eye)) for axis in (s, u, f)]
    return [f32(v) for v in (s[0], u[0], f[0], s[1], u[1], f[1], s[2], u[2], f[2], *dot)]


class Oracle:
    def __init__(self):
        emu = self.emu = Emulator(max_instructions=2_000_000_000).init_crt()
        emu.uc.reg_write(UC_X86_REG_FPCW, 0x007F)
        emu.call(C3DDEVICE_CTOR, this=G_C3DDEVICE)
        assert emu.call(C3DDEVICE_INIT, WINDOW, 0, IID_HAL_DEVICE, 0, 6, this=G_C3DDEVICE).eax == 0
        self.renderer = emu.scratch(size=SCENE_RENDERER_SIZE)
        emu.call(SCENE_RENDERER_CTOR, this=self.renderer)
        emu.call(SET_SIZE, 800, 600, this=self.renderer)
        emu.write_u32(G_PSCENE_RENDERER, self.renderer)
        self.mode = emu.scratch(size=0x400)
        world = emu.scratch(size=0x100)
        emu.write_u32(self.mode + 0xB4, world)
        emu.write_u32(world + 0x38, emu.scratch(size=0x100))
        emu.replace_thiscall(GET_GAME_MODE, 0, lambda this: self.mode)
        self.textures, self.names = {}, []

        def get_texture(this, name, flag):
            text = emu.mem_read(name, 256).split(b"\0")[0]
            texture = emu.alloc(0x130)
            emu.write_u32(texture + 0x10, len(text) % 2)
            self.textures[texture] = text
            return texture

        def get_cell_color(this, x, y, z, out):
            for k, c in enumerate(cell_color(*(struct.unpack("<f", struct.pack("<I", v))[0]
                                               for v in (x, y, z)))):
                emu.write_u32(out + 4 * k, c)
            return 0

        self.primitives = []

        def add_primitive(this, primitive, flags):
            u = emu.read_u32
            count = u(primitive + 8)
            texture = u(primitive + 0xC)
            self.primitives.append((flags, count, self.names.index(self.textures[texture]),
                                    list(struct.unpack(f"<{count * 8}I",
                                                       emu.mem_read(u(primitive), count * 0x20)))))
            return 0

        emu.replace_thiscall(GET_TEXTURE, 2, get_texture)
        emu.replace_thiscall(GET_CELL_COLOR, 4, get_cell_color)
        emu.replace_thiscall(ADD_PRIMITIVE, 2, add_primitive)

    def floats(self, addr, n):
        return list(struct.unpack(f"<{n}I", self.emu.mem_read(addr, 4 * n)))

    def set_fade(self, fade, alternate, tint):
        self.emu.mem_write(WORLD_COLOR_FADE + 0x2C, struct.pack("<6f", *fade))
        self.emu.mem_write(WORLD_COLOR_FADE + 0xD, bytes((tint,)))
        self.emu.write_u32(GROUND_WATER_FADE, alternate)

    def nodes(self, root):
        """Depth first: alpha, matrix, box, face colours, ground colours."""
        u = self.emu.read_u32
        out = []

        def visit(node):
            faces = u(u(node + 0x9C) + 4)
            out.append((self.floats(node + 0xA0, 1)[0], self.floats(node + 0xB4, 12),
                        self.floats(node + 0x128, 6), self.floats(u(node + 0x118), faces * 9),
                        self.floats(u(node + 0x144), faces * 9)))
            head = u(node + 0xAC)
            child = u(head)
            while child != head:
                visit(u(child + 8))
                child = u(child)

        visit(root)
        return out

    def matrices(self, root):
        return [n[1] for n in self.nodes(root)]

    def run(self, data, placement, light, views):
        emu = self.emu
        self.textures, self.names = {}, texture_names(data)
        mark = emu.heap_mark()
        emu.serve_data_files({"data\\m.rsm": data})
        res = emu.alloc(C3DMODELRES_SIZE)
        emu.call(C3DMODELRES_CTOR, this=res)
        # The loader at the CRT's precision, as LazyModelLoadThread runs it and as the port's
        # loader is checked (tools/rsm_oracle.py); the instance at FPUSETUP's.
        emu.uc.reg_write(UC_X86_REG_FPCW, 0x027F)
        assert emu.call(C3DMODELRES_LOAD, emu.cstr("m.rsm"), this=res).eax & 0xFF
        emu.uc.reg_write(UC_X86_REG_FPCW, 0x007F)
        position, rotation, scale, speed = placement
        instance = emu.alloc(INSTANCE_SIZE)
        emu.call(INSTANCE_CTOR, this=instance)
        stored_speed = f32(speed * f32(SPEED_SCALE)) if speed != 0.0 else speed
        emu.mem_write(instance + 0xB4, struct.pack("<f", stored_speed))
        emu.call(COPY_VOLUMES, res + 0x198, this=instance)
        emu.mem_write(instance, struct.pack("<9f", *position, *rotation, *scale))
        emu.write_u32(instance + 0xBC, 1)
        emu.call(SETUP, res, this=instance)
        emu.write_u32(instance + 0x1E8, 0)
        vectors = [emu.scratch(struct.pack("<3f", *v)) for v in light]
        emu.call(LIGHT, *vectors, this=instance)
        root = emu.read_u32(instance + 0x28)
        placed = (self.floats(instance + 0xC0, 73), self.nodes(root))
        renders = []
        for view, clip, fade, alternate, tint, translucent in views:
            self.set_fade(fade, alternate, tint)
            if translucent:
                emu.call(SET_TRANSLUCENT, this=root)
            self.primitives = []
            matrix = emu.scratch(struct.pack("<12f", *view))
            emu.call(RENDER, matrix, clip, 0, this=instance)
            renders.append(self.primitives)
        steps = []
        for _ in range(3):
            emu.call(ADVANCE, this=instance)
            steps.append((emu.read_u32(instance + 0xAC), self.matrices(root)))
        emu.heap_release(mark)
        return placed, renders, steps


def slerps(oracle):
    """SlerpRotationKey on random unit quaternions, fractions in and out of [0, 1], and
    pairs closer than its threshold."""
    import random
    rng = random.Random(5)
    emu = oracle.emu
    out = []
    for k in range(200):
        a = [rng.uniform(-1, 1) for _ in range(4)]
        n = math.sqrt(sum(c * c for c in a))
        a = [f32(c / n) for c in a]
        if k % 5 == 0:
            b = [f32(c + rng.uniform(-1e-4, 1e-4)) for c in a]
        elif k % 7 == 0:
            b = list(a)
        else:
            b = [rng.uniform(-1, 1) for _ in range(4)]
            n = math.sqrt(sum(c * c for c in b))
            b = [f32(c / n) for c in b]
        t = f32(rng.choice([rng.random(), rng.uniform(-3, 0), 0.0, 1.0, rng.uniform(1, 2)]))
        pa = emu.scratch(struct.pack("<i4f", 0, *a))
        pb = emu.scratch(struct.pack("<i4f", 0, *b))
        r = emu.scratch(size=20)
        emu.call(SLERP, bits(t), pa, pb, 0, this=r)
        out.append((bits(t), [bits(c) for c in a], [bits(c) for c in b],
                    list(struct.unpack("<4I", emu.mem_read(r + 4, 16)))))
    return out


def hexes(values):
    return ", ".join(f"0x{v:08x}" for v in values)


def main():
    models = [("synthetic 1." + str(minor), synthetic_model(minor, minor)) for minor in range(1, 6)]
    root = os.path.join(ROOT, "original", "install")
    for name in REAL_MODELS:
        path = glob.glob(os.path.join(os.environ.get("RAGEXE_MODELS", ""), "data", "model", name))
        if not path:
            sys.exit(f"model_vectors: extract data\\model\\{name} under $RAGEXE_MODELS "
                     f"(ragres extract {root}/data.grf DIR NAME)")
        with open(path[0], "rb") as f:
            models.append((name, f.read()))
    light = ((0.5, -0.7, 0.5), (1.0, 0.9, 0.8), (0.3, 0.3, 0.35))
    fade = (0.9, 0.8, 0.7, 0.6, 0.75, 0.5)
    oracle = Oracle()
    lines = ["// Generated by tools/model_vectors.py from the original in the emulation oracle."]
    lines.append("// {t, a, b, SlerpRotationKey}.")
    lines.append("static const SlerpVector kSlerpVectors[] = {")
    for t, a, b, r in slerps(oracle):
        lines.append(f"    {{0x{t:08x}, {{{hexes(a)}}}, {{{hexes(b)}}}, {{{hexes(r)}}}}},")
    lines.append("};")
    cases = []
    for k, (name, data) in enumerate(models):
        placement = ((10.0 * k - 20.0, -3.5, 7.25 * k), (15.0 * k, 30.0 - 7.0 * k, -12.5),
                     (1.0, 1.0, 1.0) if k % 3 == 0 else (1.5, 0.75, -1.25 if k % 2 else 2.0),
                     0.0 if k % 2 else 0.09)
        centre = placement[0]
        views = []
        for v, (yaw, distance) in enumerate([(0.3, 160.0), (2.2, 60.0), (4.0, 35.0)]):
            eye = (centre[0] + distance * math.sin(yaw), centre[1] - distance * 0.8,
                   centre[2] - distance * math.cos(yaw))
            views.append((look_at(eye, centre), v % 2, fade, int(v == 1), int(v == 2), 0))
        views.append((views[0][0], 0, fade, 0, 0, 1))
        placed, renders, steps = oracle.run(data, placement, light, views)
        instance, nodes = placed
        lines.append(f"// {name}")
        lines.append(f"static const char* const kModel{k}[] = {{")
        hexed = data.hex()
        lines += [f'    "{hexed[i:i + 96]}",' for i in range(0, len(hexed), 96)]
        lines += ["    nullptr,", "};"]
        for n, (alpha, matrix, box, colours, ground) in enumerate(nodes):
            lines.append(f"static const std::uint32_t kShade{k}_{n}[] = {{{hexes(colours) or '0'}}};")
            lines.append(f"static const std::int32_t kTint{k}_{n}[] = {{{hexes(ground) or '0'}}};")
        lines.append(f"static const NodeVector kNodes{k}[] = {{")
        for n, (alpha, matrix, box, colours, ground) in enumerate(nodes):
            lines.append(f"    {{0x{alpha:08x}, {{{hexes(matrix)}}}, {{{hexes(box)}}}, kShade{k}_{n}, "
                         f"kTint{k}_{n}, {len(colours) // 9}}},")
        lines.append("};")
        lines.append(f"static const std::uint32_t kInstance{k}[73] = {{{hexes(instance)}}};")
        for v, prims in enumerate(renders):
            for p, (flags, count, texture, vertices) in enumerate(prims):
                lines.append(f"static const std::uint32_t kVerts{k}_{v}_{p}[] = {{{hexes(vertices)}}};")
            lines.append(f"static const DrawnFace kDrawn{k}_{v}[] = {{")
            for p, (flags, count, texture, vertices) in enumerate(prims):
                lines.append(f"    {{{flags}, {count}, {texture}, kVerts{k}_{v}_{p}}},")
            if not prims:
                lines.append("    {},")
            lines.append("};")
        lines.append(f"static const std::uint32_t kSteps{k}[] = {{")
        for time, matrices in steps:
            lines.append(f"    {time}, {hexes(m for mat in matrices for m in mat)},")
        lines.append("};")
        views_text = ", ".join(
            f"{{{{{hexes(bits(x) for x in view)}}}, {clip}, {alternate}, {tint}, {translucent}, "
            f"kDrawn{k}_{v}, {len(renders[v])}}}"
            for v, (view, clip, _, alternate, tint, translucent) in enumerate(views))
        position, rotation, scale, speed = placement
        cases.append(f"    {{kModel{k}, {{{hexes(bits(f32(x)) for x in (*position, *rotation, *scale))}}}, "
                     f"0x{bits(f32(speed)):08x}, kInstance{k}, kNodes{k}, {len(nodes)},\n"
                     f"     {{{views_text}}},\n     kSteps{k}}},")
    lines.append("static const std::uint32_t kModelLight[9] = {"
                 + hexes(bits(f32(x)) for v in light for x in v) + "};")
    lines.append("static const std::uint32_t kModelFade[6] = {" + hexes(bits(f32(x)) for x in fade) + "};")
    lines.append("// {file, position, rotation and scale, speed, the instance's fields +0xc0 to +0x1e4,")
    lines.append("// the nodes, the renders {view, clip, the fade's other set, ground tint, translucent,")
    lines.append("// faces}, the animation steps {time, every node's matrix}}.")
    lines.append("static const ModelVector kModelVectors[] = {")
    lines += cases
    lines.append("};")
    lines += single_loads(models)
    print("\n".join(lines))


def single_loads(models):
    """The same models loaded at single precision, as the main thread loads the models near
    the player: every node's vertices, face normals and corner normals, in file order."""
    lines = []
    for k, (name, data) in enumerate(models):
        emu = Emulator(max_instructions=2_000_000_000).init_crt()
        emu.replace_thiscall(GET_TEXTURE, 2, lambda this, name, flag: emu.alloc(0x130))
        emu.serve_data_files({"data\\m.rsm": data})
        res = emu.alloc(C3DMODELRES_SIZE)
        emu.call(C3DMODELRES_CTOR, this=res)
        emu.uc.reg_write(UC_X86_REG_FPCW, 0x007F)
        assert emu.call(C3DMODELRES_LOAD, emu.cstr("m.rsm"), this=res).eax & 0xFF
        head = emu.read_u32(res + 0x11C)
        node, nodes = emu.read_u32(head), []
        while node != head:
            nodes.append(emu.read_u32(node + 8))
            node = emu.read_u32(node)
        rows = []
        for n, p in enumerate(nodes):
            mesh = emu.read_u32(p + 0x114)
            nv, nf = struct.unpack("<ii", emu.mem_read(mesh, 8))
            arrays = [list(struct.unpack(f"<{count}I", emu.mem_read(emu.read_u32(mesh + offset), 4 * count)))
                      if count else [] for offset, count in ((0xC, 3 * nv), (0x10, 3 * nf), (0x14, 9 * nf))]
            for a, values in zip(("Vertices", "FaceNormals", "CornerNormals"), arrays):
                lines.append(f"static const std::uint32_t kSingle{k}_{n}{a}[] = {{{hexes(values) or '0'}}};")
            rows.append(f"    {{kSingle{k}_{n}Vertices, {nv}, kSingle{k}_{n}FaceNormals, kSingle{k}_{n}CornerNormals, "
                        f"{nf}}},")
        lines.append(f"static const SingleNodeVector kSingle{k}[] = {{")
        lines += rows
        lines.append("};")
    lines.append("// Each model of kModelVectors loaded at single precision: per node in file order {vertices,")
    lines.append("// their count, face normals, corner normals, the face count}.")
    lines.append("static const SingleLoadVector kSingleLoads[] = {")
    lines += [f"    {{kModel{k}, kSingle{k}, sizeof(kSingle{k}) / sizeof(kSingle{k}[0])}}," for k in range(len(models))]
    lines.append("};")
    return lines


if __name__ == "__main__":
    main()
