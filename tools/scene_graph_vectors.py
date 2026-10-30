"""Test vectors for the port's scene graph and view frustum (docs/render/scene_graph.md),
taken from the original in the emulation oracle.

usage: .venv/bin/python -I tools/scene_graph_vectors.py > tests/unit/scene_graph_vectors.inc

A synthetic ground is built by the original (tools/ground_vectors.py) and given, with a
synthetic attribute map and model stand-ins (their oriented box +0x130..+0x168 and world
bounds +0x1cc..+0x1e0), to a CWorld that CWorld::BuildSceneGraph (004c4f10) fills: once
from the heights (SceneGraphNode::UpdateHeightBounds, 004c5fa0) and once copying a bounds
tree as a version 2.1 .rsw gives one (SceneGraphNode::CopyBounds, 004c68c0). Recorded:
every node, depth first. Then ViewFrustum::CullSceneGraph (004ceb00) runs under several
views: its planes, the nodes it finds inside and crossing, and
ViewFrustum::ClassifyOrientedBox (004ced90) of each model. Then OrientedBoxIntersectsBox
(004c56d0) on random boxes. Last the picking, under the same views on an 800 x 600
SceneRenderer: SceneRenderer::ScreenRay's (00411c50) ray through random screen points,
SceneRayQuery::CollectNodes' leaves (004c62e0) and CGameMode::PickCell's (005147a0) cell for
each, with the view's inverse from Matrix::Invert (00409ed0); and SceneRayQuery::HitsBox
(004c6460), RayIntersectsTriangle (0040b7b0) and C3dAttr::IntersectCellRay (00427230) on
random rays.
The x87 runs at single precision, as DirectDraw's FPUSETUP leaves it in the client.
"""
import math
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ground_vectors import build, ground_file  # noqa: E402
from model_vectors import look_at  # noqa: E402

BUILD_SCENE_GRAPH, NODE_CTOR, NODE_SIZE, SUBDIVIDE = 0x4C4F10, 0x4C5270, 0x80, 0x4C5340
FRUSTUM_CTOR, FRUSTUM_SIZE, CULL, CLASSIFY_OBB, CLEAR_VISIBLE = 0x4CEA70, 0xC0, 0x4CEB00, 0x4CED90, 0x4CEC80
OBB_INTERSECTS_BOX = 0x4C56D0
SET_SIZE, SCENE_RENDERER_SIZE, G_PSCENE_RENDERER, INVERT = 0x40DB70, 0x2A0, 0x625E98, 0x409ED0
SCREEN_RAY, LIST_CTOR, COLLECT_NODES, HITS_BOX = 0x411C50, 0x4C62B0, 0x4C62E0, 0x4C6460
RAY_TRIANGLE, CELL_RAY, PICK_CELL = 0x40B7B0, 0x427230, 0x5147A0
MOUSE_X, MOUSE_Y = 0x628B5C, 0x628B60  # g_DirectInputMouse +0x14 and +0x18
GROUND_SHAPE = (40, 32)  # cells of 10
ATTR_CELL = 5
SLOPES = (5.7027130126953125, 7.6037387847900391)  # SetSize at 800 x 600: +8, +0xc


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def bits(f):
    return struct.unpack("<I", struct.pack("<f", f))[0]


def attr_height(cell, corner):
    """The test map's height at a cell's corner (as camera_vectors.py's, smaller)."""
    return float(10 - (cell * 7 + corner * 13) * 29 % 61)


def source_bound(node, value):
    """The bounds tree a version 2.1 .rsw gives, depth first: min, max, centre and half
    extents, whole numbers from -300 to 300."""
    return float((node * 37 + value * 101) % 601 - 300)


def obb(rng, half_extent):
    """An oriented box: half extents, centre, three unit axes, and its axis-aligned bounds."""
    yaw, pitch, roll = (rng.uniform(0, 2 * math.pi) for _ in range(3))
    cy, sy, cp, sp, cr, sr = (math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch),
                              math.cos(roll), math.sin(roll))
    axes = [(cy * cr + sy * sp * sr, cp * sr, -sy * cr + cy * sp * sr),
            (-cy * sr + sy * sp * cr, cp * cr, sy * sr + cy * sp * cr),
            (sy * cp, -sp, cy * cp)]
    half = [rng.uniform(1, half_extent) for _ in range(3)]
    centre = [rng.uniform(-260, 260), rng.uniform(-40, 10), rng.uniform(-220, 220)]
    reach = [sum(abs(axes[a][k]) * half[a] for a in range(3)) for k in range(3)]
    bounds = [centre[k] - reach[k] for k in range(3)] + [centre[k] + reach[k] for k in range(3)]
    values = half + centre + [c for a in axes for c in a]
    return [f32(v) for v in values], [f32(v) for v in bounds]


def main():
    rng = random.Random(29)
    w, h = GROUND_SHAPE
    data = ground_file(rng, w, h, 4)
    light = ((0.5, -0.7, 0.5), (1.0, 0.9, 0.8), (0.3, 0.3, 0.35))
    emu, ground, _ = build(data, 1, light)
    u = emu.read_u32

    aw, ah = w * 2, h * 2
    attr = emu.scratch(size=0x130)
    emu.write_u32(attr + 0x110, aw)
    emu.write_u32(attr + 0x114, ah)
    emu.write_u32(attr + 0x118, ATTR_CELL)
    cells = b"".join(struct.pack("<4fi", *(attr_height(c, k) for k in range(4)), 0) for c in range(aw * ah))
    emu.write_u32(attr + 0x120, emu.scratch(cells))

    models = [obb(rng, rng.choice((3, 12, 40))) for _ in range(30)]
    instances = []
    for values, bounds in models:
        m = emu.scratch(size=0x200)
        emu.mem_write(m + 0x130, struct.pack("<15f", *values))
        emu.mem_write(m + 0x1CC, struct.pack("<6f", *bounds))
        instances.append(m)
    vector = emu.scratch(struct.pack(f"<{len(instances)}I", *instances))

    def new_tree():
        root = emu.alloc(NODE_SIZE)
        emu.call(NODE_CTOR, this=root)
        return root

    def nodes_of(root):
        out = []

        def visit(n, depth):
            out.append(n)
            if depth < 5:
                for k in range(4):
                    visit(u(n + 4 + 4 * k), depth + 1)

        visit(root, 0)
        return out

    # A bounds tree as C3dWorldRes keeps it, random boxes.
    source = new_tree()
    emu.mem_write(source + 0x14, struct.pack("<6f", -240, -999999, -200, 239, 999999, 199))
    emu.call(SUBDIVIDE, 0, this=source)
    for k, n in enumerate(nodes_of(source)):
        emu.mem_write(n + 0x14, struct.pack("<12f", *(source_bound(k, i) for i in range(12))))

    lines = ["// Generated by tools/scene_graph_vectors.py from the original in the emulation oracle."]
    lines.append("static const char* const kSceneGround[] = {")
    hexed = data.hex()
    lines += [f'    "{hexed[i:i + 96]}",' for i in range(0, len(hexed), 96)]
    lines += ["    nullptr,", "};"]
    lines.append(f"static constexpr std::int32_t kSceneGroundWidth = {w}, kSceneGroundHeight = {h};")
    lines.append("static const std::uint32_t kSceneLight[9] = {"
                 + ", ".join(f"0x{bits(f32(x)):08x}" for v in light for x in v) + "};")
    lines.append("// The models' oriented boxes (half extents, centre, axes) and world bounds.")
    lines.append("static const std::uint32_t kSceneModels[][21] = {")
    for values, bounds in models:
        lines.append("    {" + ", ".join(f"0x{bits(v):08x}" for v in values + bounds) + "},")
    lines.append("};")

    trees = []
    for copied in (0, 1):
        world = emu.scratch(size=0x100)
        emu.write_u32(world + 0x38, ground)
        emu.write_u32(world + 0x40, attr)
        emu.write_u32(world + 0x48, vector)
        emu.write_u32(world + 0x4C, vector + 4 * len(instances))
        root = world + 0x68  # the root node is CWorld's own member
        emu.call(NODE_CTOR, this=root)
        emu.write_u32(world + 0xE8, source if copied else 0)
        emu.call(BUILD_SCENE_GRAPH, this=world)
        nodes = nodes_of(root)
        index = {n: k for k, n in enumerate(nodes)}
        lines.append(f"// BuildSceneGraph {'copying the bounds' if copied else 'from the heights'}: "
                     "{min, max, centre, half extents, dirty, ground cells, attribute cells}, models.")
        lines.append(f"static const SceneNodeVector kSceneNodes{copied}[] = {{")
        model_lines = []
        for k, n in enumerate(nodes):
            box = struct.unpack("<12I", emu.mem_read(n + 0x14, 48))
            rect = struct.unpack("<4i", emu.mem_read(n + 0x5C, 16))
            attr_rect = struct.unpack("<4i", emu.mem_read(n + 0x70, 16))
            members = [instances.index(u(p)) for p in range(u(n + 0x4C), u(n + 0x50), 4)]
            lines.append(f"    {{{{{', '.join(f'0x{v:08x}' for v in box)}}}, {u(n + 0x44)}, "
                         f"{{{', '.join(map(str, rect))}}}, {{{', '.join(map(str, attr_rect))}}}}},")
            if members:
                model_lines.append(f"    {k}, {len(members)}, {', '.join(map(str, members))},")
        lines.append("};")
        lines.append(f"// {{node, count, models...}}, then -1.")
        lines.append(f"static const std::int32_t kSceneMembers{copied}[] = {{")
        lines += model_lines
        lines += ["    -1,", "};"]
        trees.append((root, index))

    root, index = trees[0]
    frustum = emu.scratch(size=FRUSTUM_SIZE)
    emu.call(FRUSTUM_CTOR, this=frustum)
    views, eyes = [], []
    for yaw, pitch, distance, at in [(0.4, 0.8, 300.0, (0, 0, 0)), (2.0, 0.6, 120.0, (-150, 0, 80)),
                                     (3.5, 1.2, 600.0, (100, -10, -120)), (5.0, 0.4, 60.0, (220, 0, 180)),
                                     (1.2, 0.9, 30.0, (-230, 0, -190))]:
        eye = (at[0] + distance * math.sin(yaw) * math.cos(pitch), at[1] - distance * math.sin(pitch),
               at[2] - distance * math.cos(yaw) * math.cos(pitch))
        view = look_at(eye, at)
        eyes.append([f32(c) for c in eye])
        matrix = emu.scratch(struct.pack("<12f", *view))
        emu.call(CULL, bits(f32(SLOPES[0])), bits(f32(SLOPES[1])), matrix, root, this=frustum)
        planes = list(struct.unpack("<42I", emu.mem_read(frustum, 0xA8)))

        def listed(head):
            out, node = [], u(head)
            while node != head:
                out.append(index[u(node + 8)])
                node = u(node)
            return out

        inside, crossing = listed(u(frustum + 0xAC)), listed(u(frustum + 0xB8))
        classes = [emu.call(CLASSIFY_OBB, m + 0x130, this=frustum).eax & 0xFF for m in instances]
        emu.call(CLEAR_VISIBLE, this=frustum)
        views.append((view, planes, inside, crossing, classes))
    lines.append(f"static const float kSceneSlopes[2] = {{{SLOPES[0]!r}f, {SLOPES[1]!r}f}};")
    for k, (view, planes, inside, crossing, classes) in enumerate(views):
        lines.append(f"static const std::int32_t kSceneInside{k}[] = {{{', '.join(map(str, inside)) or '-1'}}};")
        lines.append(f"static const std::int32_t kSceneCrossing{k}[] = {{{', '.join(map(str, crossing)) or '-1'}}};")
    lines.append("// {view, planes and normals (+0 to +0xa4), inside, crossing, each model's class}.")
    lines.append("static const FrustumVector kFrustumVectors[] = {")
    for k, (view, planes, inside, crossing, classes) in enumerate(views):
        lines.append(f"    {{{{{', '.join(f'0x{bits(v):08x}' for v in view)}}},")
        lines.append(f"     {{{', '.join(f'0x{v:08x}' for v in planes)}}},")
        lines.append(f"     kSceneInside{k}, {len(inside)}, kSceneCrossing{k}, {len(crossing)}, "
                     f"{{{', '.join(map(str, classes))}}}}},")
    lines.append("};")

    lines.append("// {oriented box, axis-aligned box (min, max), OrientedBoxIntersectsBox}.")
    lines.append("static const IntersectVector kIntersectVectors[] = {")
    for k in range(300):
        values, _ = obb(rng, rng.choice((3, 20, 60)))
        lo = [rng.uniform(-260, 260), rng.uniform(-60, 20), rng.uniform(-220, 220)]
        size = [rng.uniform(1, 120) for _ in range(3)]
        box = [f32(v) for v in lo + [lo[i] + size[i] for i in range(3)]]
        if k % 3 == 0:  # near the oriented box, where the axes decide
            box = [f32(values[3 + i % 3] + (rng.uniform(-30, 5) if i < 3 else rng.uniform(-5, 30)))
                   for i in range(6)]
        result = emu.call(OBB_INTERSECTS_BOX, emu.scratch(struct.pack("<15f", *values)),
                          emu.scratch(struct.pack("<6f", *box))).eax & 0xFF
        lines.append(f"    {{{{{', '.join(f'0x{bits(v):08x}' for v in values)}}}, "
                     f"{{{', '.join(f'0x{bits(v):08x}' for v in box)}}}, {result}}},")
    lines.append("};")
    picking(emu, rng, lines, root, attr, aw, ah, [v[0] for v in views], eyes, index)
    print("\n".join(lines))


def unit(rng):
    d = [rng.gauss(0, 1) for _ in range(3)]
    n = math.sqrt(sum(c * c for c in d))
    return [f32(c / n) for c in d]


def picking(emu, rng, lines, root, attr, aw, ah, views, eyes, index):
    u = emu.read_u32
    renderer = emu.scratch(size=SCENE_RENDERER_SIZE)
    emu.call(SET_SIZE, 800, 600, this=renderer)
    emu.write_u32(G_PSCENE_RENDERER, renderer)
    cview = emu.scratch(size=0x1C0)
    mode = emu.scratch(size=0x400)
    emu.write_u32(mode + 0xB4, root - 0x68)
    emu.write_u32(mode + 0xB8, cview)
    out = emu.scratch(size=0x40)

    def floats(addr, n):
        return list(struct.unpack(f"<{n}I", emu.mem_read(addr, 4 * n)))

    def leaves(ray):
        query = emu.scratch(size=0x28)
        emu.call(LIST_CTOR, this=query)
        emu.call(COLLECT_NODES, emu.scratch(struct.pack("<6I", *ray)), root, this=query)
        head, found = u(query + 4), []
        node = u(head)
        while node != head:
            found.append(index[u(node + 8)])
            node = u(node)
        return found

    lines.append("// Picking, per view: {screen x, y; the ray (origin, direction); the leaves it meets;")
    lines.append("// PickCell's answer and cell}.")
    rows = []
    for k, (view, eye) in enumerate(zip(views, eyes)):
        emu.mem_write(cview + 0x7C, struct.pack("<3f", *eye))
        emu.call(INVERT, emu.scratch(struct.pack("<12f", *view)), this=cview + 0xC4)
        inverse = floats(cview + 0xC4, 12)
        for n in range(60):
            x, y = rng.randrange(0, 800), rng.randrange(0, 600)
            if n % 20 == 19:
                x, y = rng.choice([(-40, 300), (900, 10), (400, -30), (0, 0), (799, 599)])
            emu.call(SCREEN_RAY, x, y, cview + 0x7C, cview + 0xC4, out, this=renderer)
            ray = floats(out, 6)
            found = leaves(ray)
            emu.write_u32(MOUSE_X, x & 0xFFFFFFFF)
            emu.write_u32(MOUSE_Y, y & 0xFFFFFFFF)
            cells = emu.scratch(struct.pack("<2i", -1, -1))
            picked = emu.call(PICK_CELL, cells, cells + 4, this=mode).eax & 0xFF
            cx, cz = struct.unpack("<2i", emu.mem_read(cells, 8))
            name = f"kPickLeaves{k}_{n}"
            lines.append(f"static const std::int32_t {name}[] = {{{', '.join(map(str, found)) or '-1'}}};")
            rows.append(f"    {{{k}, {x}, {y}, {{{', '.join(f'0x{v:08x}' for v in ray)}}}, {name}, {len(found)}, "
                        f"{picked}, {cx}, {cz}}},")
        lines.append(f"static const std::uint32_t kPickInverse{k}[12] = {{{', '.join(f'0x{v:08x}' for v in inverse)}}};")
    lines.append("static const std::uint32_t* const kPickInverses[] = {"
                 + ", ".join(f"kPickInverse{k}" for k in range(len(views))) + "};")
    lines.append("static const std::uint32_t kPickEyes[][3] = {"
                 + ", ".join("{" + ", ".join(f"0x{bits(c):08x}" for c in e) + "}" for e in eyes) + "};")
    lines.append("static const PickVector kPickVectors[] = {")
    lines += rows
    lines.append("};")

    # HitsBox: rays from around the map at boxes near and far, some along an axis.
    query = emu.scratch(size=0x28)
    lines.append("// {origin, direction, range, box (minimum, maximum), HitsBox}.")
    lines.append("static const BoxRayVector kBoxRayVectors[] = {")
    for n in range(500):
        origin = [f32(rng.uniform(-300, 300)), f32(rng.uniform(-200, 20)), f32(rng.uniform(-300, 300))]
        lo = [rng.uniform(-250, 250), rng.uniform(-80, 20), rng.uniform(-250, 250)]
        size = [rng.uniform(0.5, 120) for _ in range(3)]
        box = [f32(c) for c in lo + [lo[i] + size[i] for i in range(3)]]
        if n % 2 == 0:  # aimed near the box
            target = [rng.uniform(box[i] - 10, box[i + 3] + 10) for i in range(3)]
            d = [t - o for t, o in zip(target, origin)]
            m = math.sqrt(sum(c * c for c in d))
            direction = [f32(c / m) for c in d]
        else:
            direction = unit(rng)
        if n % 5 == 1:
            direction[rng.randrange(3)] = rng.choice([0.0, f32(5e-6), f32(-2e-5)])
        rng_range = f32(rng.choice([999999.0, rng.uniform(10, 400)]))
        if n >= 400:
            # From inside the box, from a face (the near distance 0), and along an axis at
            # exactly the parallel limit, each with a short range.
            kind = n % 3
            origin = [f32(rng.uniform(box[i] + 0.1 * size[i], box[i + 3] - 0.1 * size[i])) for i in range(3)]
            direction = unit(rng)
            axis = rng.randrange(3)
            if kind == 1:
                origin[axis] = box[axis]
                direction[axis] = f32(abs(direction[axis]) + 0.2)
            elif kind == 2:
                direction[axis] = rng.choice([f32(9.99999974738e-06), f32(-9.99999974738e-06)])
            rng_range = f32(rng.uniform(0.5, 30))
        emu.mem_write(query + 0xC, struct.pack("<7f", *origin, *direction, rng_range))
        hit = emu.call(HITS_BOX, emu.scratch(struct.pack("<6f", *box)), this=query).eax & 0xFF
        values = origin + direction + [rng_range] + box
        lines.append(f"    {{{{{', '.join(f'0x{bits(v):08x}' for v in values)}}}, {hit}}},")
    lines.append("};")

    # RayIntersectsTriangle: triangles about a point near the ray, so many are hit.
    lines.append("// {ray, triangle a, b, c; RayIntersectsTriangle, u, v, t (from 0xdeadbeef)}.")
    lines.append("static const TriangleRayVector kTriangleRayVectors[] = {")
    for n in range(500):
        origin = [f32(rng.uniform(-100, 100)) for _ in range(3)]
        direction = unit(rng)
        along = rng.uniform(-50, 300)
        centre = [origin[i] + direction[i] * along + rng.uniform(-8, 8) for i in range(3)]
        tri = [f32(centre[i % 3] + rng.uniform(-15, 15)) for i in range(9)]
        if n % 10 == 3:  # edge on to the ray
            tri[6:9] = [f32(tri[i] + (tri[3 + i] - tri[i]) * 0.5) for i in range(3)]
        outs = emu.scratch(struct.pack("<3I", 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF))
        hit = emu.call(RAY_TRIANGLE, emu.scratch(struct.pack("<6f", *origin, *direction)),
                       emu.scratch(struct.pack("<3f", *tri[0:3])), emu.scratch(struct.pack("<3f", *tri[3:6])),
                       emu.scratch(struct.pack("<3f", *tri[6:9])), outs, outs + 4, outs + 8).eax
        values = [bits(v) for v in origin + direction + tri] + floats(outs, 3)
        lines.append(f"    {{{{{', '.join(f'0x{v:08x}' for v in values[:15])}}}, {hit}, "
                     f"{{{', '.join(f'0x{v:08x}' for v in values[15:])}}}}},")
    lines.append("};")

    # IntersectCellRay: rays at a cell's middle from above, some cells off the map.
    lines.append("// {ray, cell x, z, IntersectCellRay}.")
    lines.append("static const CellRayVector kCellRayVectors[] = {")
    for n in range(400):
        cx, cz = rng.randrange(-2, aw + 2), rng.randrange(-2, ah + 2)
        target = [(cx - aw // 2 + rng.uniform(-0.5, 1.5)) * ATTR_CELL, rng.uniform(-20, 10),
                  (cz - ah // 2 + rng.uniform(-0.5, 1.5)) * ATTR_CELL]
        origin = [f32(target[0] + rng.uniform(-80, 80)), f32(rng.uniform(-300, -60)),
                  f32(target[2] + rng.uniform(-80, 80))]
        d = [t - o for t, o in zip(target, origin)]
        m = math.sqrt(sum(c * c for c in d))
        direction = [f32(c / m) for c in d]
        emu.call(CELL_RAY, emu.scratch(struct.pack("<6f", *origin, *direction)), cx, cz, this=attr)
        result = bits(f32(emu.st0()))
        lines.append(f"    {{{{{', '.join(f'0x{bits(v):08x}' for v in origin + direction)}}}, {cx}, {cz}, "
                     f"0x{result:08x}}},")
    lines.append("};")


if __name__ == "__main__":
    main()
