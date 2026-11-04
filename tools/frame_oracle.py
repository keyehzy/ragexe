"""Compares the port's whole frames with the original's (docs/render/frame.md).

usage: .venv/bin/python -I tools/frame_oracle.py [--sample N] [--jobs J] [--raw] [--show N]
           [--original-only] [--npc JOB DX DY]... [--pc JOB SEX HAIR WEAPON TOP MID BOTTOM SHIELD
           HAIR_COLOR CLOTHES_COLOR ACTION DIRECTION FRAME DX DY]... [MAP...]

Each map is loaded as tools/world_oracle.py loads it, but with the player on the walkable cell
nearest the attribute map's centre, and with the textures made for real:
TextureCache::GetTexture reads each image through CBitmapRes and C3dDevice::CreateTexture
makes a CTexture of the fake Direct3D device. Only CBitmapRes::LoadJpeg (004280d0), whose
decoder is ijl15.dll, is stood in for: it gives the image's size and its pixels (decoded by
Pillow, with the original's magenta rule) and clears the alpha flag, as the original does.

Then three frames, each as CGameMode::OnFrame draws one: the water's step (StepWater), the
view's (CView::Update), CView::DrawWorld, which draws the visible scene graph and the cell cursor
into the scene lists and ends with SceneRenderer::DrawScene, then the frame counted (as Present
counts it) and the primitive pools rewound (00412e70). The first frame looks as PlaceCamera
places the camera; the second and third turn it by 120 and 240 degrees. The cell cursor is over
the player's cell. The frame count is 1 at the first frame, as if the loading had presented one.
IsIndoorMap is answered from indoorRswTable.txt. The fog is set as CGameMode::OnInit sets it with
the FOG setting on, the client's default (00516570, from fogParameterTable.txt read by
ClientState::LoadFogParameterTable), on a device with vertex fog.

With --npc, each JOB is a CNpc in the world's list (CWorld+8), in the order given: made by its
constructor (0053f950), its job at +0x1b8 named by vf16 (0053fae0, which loads its sprite and
actions for real and keeps the job's shadow scale; the job names and shadows of service type
1), in the centre of the cell DX and DY from the player's, its shadow (+0x88) set. DrawWorld
draws it through CNpc::Render, which records its pick bounds in g_CActorPickNode (constructed,
and cleared before each frame as OnFrame clears it). With --pc, each is a CPc after the NPCs:
made by its constructor (00541fb0), dressed by vf14 (00542c50, with the tables of
ClientState::InitNameTables), its base action (+0x20), action (the base action plus the
direction, +0x24) and frame (+0x28) set, placed as an NPC is; DrawWorld draws it through
CPc::Render. The sprites, actions, .imf files and palettes are read from a second cache.
timeGetTime answers 1,000,000 ms in the first frame, 16 more in each after.

Every call DrawWorld makes on the device but SetTransform (SetupProjection's, which the port
leaves out: its vertices come transformed) is recorded and printed as `ragres draw-frame` prints the
port's: "R state value", "S stage state value", "T stage texture", "D type fvf count" and the
vertices' hash (--raw: the vertices). A texture is named by the name it was asked for, or
"#k" for the k-th unnamed one (the renderer's 50 spare sprite page textures, then the lightmap
pages and any sprite pages after). Before the frames, "X k name" lists the textures in the order
they were made, which is the order of their addresses. --sample takes the
smallest maps and a spread of the rest, as world_oracle.py does; 0 takes every map. The
textures and models are read from a cache under build/ (extracted from data.grf and the patches
on first use).
"""
import argparse
import io
import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import oracle_pool  # noqa: E402
import world_oracle  # noqa: E402
from emu import directx  # noqa: E402
from emu.winapi import Stub  # noqa: E402
from unicorn.x86_const import UC_X86_REG_ECX  # noqa: E402
from model_vectors import G_PSCENE_RENDERER  # noqa: E402
from PIL import Image  # noqa: E402

CBITMAPRES_LOAD_JPEG, CTEXTURE_CTOR = 0x4280D0, 0x4165E0
CVIEW_SIZE, CVIEW_CTOR, CVIEW_RESET, CVIEW_UPDATE, DRAW_WORLD = 0x1C0, 0x4CEF80, 0x4CF1D0, 0x4CF260, 0x4CF4D0
PLACE_CAMERA, IS_INDOOR_MAP, GET_CELL_CENTRE, GET_HEIGHT = 0x50D9E0, 0x598BA0, 0x511DE0, 0x427150
STEP_WATER, REWIND_POOLS = 0x4BC450, 0x412E70
LOAD_FOG_TABLE, APPLY_MAP_FOG, G_CLIENT_STATE = 0x598E10, 0x516570, 0x686670
DEVICE_TRI_RASTER_CAPS, RASTERCAPS_FOGVERTEX = 0x623A5C, 0x80  # g_C3dDevice+0x8c
CELL_CURSOR = 0x684E20  # x, z, hidden, colour
CURSOR_COLOR = 0x6600FFAA
TURNS = (None, 120.0, 240.0)
CACHE_PATTERNS = ("*.bmp", "*.tga", "*.jpg", "*.rsm", "*.txt")
ACTOR_PATTERNS = ("*.spr", "*.act", "*.imf", "*.pal")
NPC_CTOR, NPC_SET_JOB, NPC_SIZE = 0x53F950, 0x53FAE0, 0x298
PC_CTOR, PC_DRESS, PC_SIZE = 0x541FB0, 0x542C50, 0x308
INIT_JOB_NAMES, INIT_SHADOWS, INIT_NAME_TABLES, SERVICE_TYPE = 0x58AC70, 0x59A5E0, 0x588BA0, 0x67C9D4
CONSTRUCT_PICK_TREE, CLEAR_PICK_TREE, G_PICK_TREE = 0x4CE470, 0x4CE710, 0x67CA60
FRAME_TIME, FRAME_TIME_STEP = 1000000, 16
PC_ARGS = ("JOB", "SEX", "HAIR", "WEAPON", "TOP", "MID", "BOTTOM", "SHIELD", "HAIR_COLOR", "CLOTHES_COLOR", "ACTION",
           "DIRECTION", "FRAME", "DX", "DY")
RAW = "--raw" in sys.argv

_draw_primitive = directx.Device.DrawPrimitive


def draw_primitive(self, kind, fvf, data, count, flags):
    """The vertices' bytes as well, NaNs and all."""
    self.dx.current.detail["raw"] = bytes(self.dx.emu.mem_read(data, directx.fvf_stride(fvf) * count))
    return _draw_primitive(self, kind, fvf, data, count, flags)


directx.Device.DrawPrimitive = draw_primitive


def build_cache(ragres, install, cache, patterns=CACHE_PATTERNS):
    """Every file of data.grf with each .gpf over it, in name order, that the patterns name:
    by default the images, models and text files."""
    if os.path.isdir(cache):
        return
    work = cache + ".partial"
    os.makedirs(work, exist_ok=True)
    archives = [os.path.join(install, "data.grf")] + sorted(
        os.path.join(install, n) for n in os.listdir(install) if n.lower().endswith(".gpf"))
    for archive in archives:
        subprocess.run([ragres, "extract", archive, work, *patterns], check=True, capture_output=True)
    os.rename(work, cache)


class CachedFiles:
    """Archive paths, lower-cased as CGPak looks them up, to the cache's files, read when asked."""
    LOWER = str.maketrans("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz")

    def __init__(self, cache, extra):
        self.paths = {}
        for root, _, names in os.walk(cache):
            for name in names:
                path = os.path.join(root, name)
                self.paths[world_oracle.archive_key(os.path.relpath(path, cache)).translate(self.LOWER)] = path
        self.extra = {k.translate(self.LOWER): v for k, v in extra.items()}

    def get(self, key):
        if key in self.extra:
            return self.extra[key]
        path = self.paths.get(key)
        if path is None:
            return None
        with open(path, "rb") as f:
            return f.read()

    def __contains__(self, key):
        return key in self.extra or key in self.paths


def stand_in_jpeg(emu):
    def load_jpeg(this, data, size):
        image = Image.open(io.BytesIO(bytes(emu.mem_read(data, size)))).convert("RGB")
        w, h = image.size
        pixels = bytearray()
        for r, g, b in image.getdata():
            magenta = r >= 0xFE and g <= 3 and b >= 0xFE
            pixels += struct.pack("<I", 0x000A0A0A if magenta else 0xFF000000 | r << 16 | g << 8 | b)
        buffer = emu.alloc(len(pixels))
        emu.mem_write(buffer, bytes(pixels))
        emu.write_u32(this + 0x114, w)
        emu.write_u32(this + 0x118, h)
        emu.write_u32(this + 0x11C, buffer)
        emu.write_u32(this + 0x110, 0)
        return 1

    emu.replace_thiscall(CBITMAPRES_LOAD_JPEG, 2, load_jpeg)


def walkable_cell(emu, attr):
    """The walkable cell (type 0) nearest the attribute map's centre, the first in row order of
    those as near; the centre when none is."""
    w, h = emu.read_u32(attr + 0x110), emu.read_u32(attr + 0x114)
    cells = emu.mem_read(emu.read_u32(attr + 0x120), w * h * 0x14)
    centre = (w // 2, h // 2)
    best = None
    for z in range(h):
        for x in range(w):
            if struct.unpack_from("<i", cells, (z * w + x) * 0x14 + 0x10)[0] == 0:
                d = (x - centre[0]) ** 2 + (z - centre[1]) ** 2
                if best is None or d < best[0]:
                    best = (d, (x, z))
    return best[1] if best else centre


def queued_textures(emu, renderer):
    """The textures of the list at SceneRenderer+0x254 (its head +0x258), in order."""
    head = emu.read_u32(renderer + 0x258)
    node, out = emu.read_u32(head), []
    while node != head:
        out.append(emu.read_u32(node + 8))
        node = emu.read_u32(node)
    return out


def place(emu, world, mode, attr, cell, actor, dx, dy):
    """The actor in the centre of the cell dx and dy from cell, on the ground, its shadow (+0x88)
    set, at the end of the world's list."""
    out = emu.scratch(size=8)
    emu.call(GET_CELL_CENTRE, cell[0] + dx, cell[1] + dy, out, out + 4, this=mode)
    x, z = struct.unpack("<ff", emu.mem_read(out, 8))
    y = emu.call(GET_HEIGHT, *struct.unpack("<II", emu.mem_read(out, 8)), this=attr).st0
    emu.mem_write(actor + 4, struct.pack("<fff", x, y, z))
    emu.mem_write(actor + 0x88, b"\1")
    head = emu.read_u32(world + 0xC)
    last = emu.read_u32(head + 4)
    node = emu.alloc(12)
    emu.mem_write(node, struct.pack("<III", head, last, actor))
    emu.write_u32(last, node)
    emu.write_u32(head + 4, node)
    emu.write_u32(world + 0x10, emu.read_u32(world + 0x10) + 1)


def add_npc(emu, world, mode, attr, cell, job, dx, dy):
    """A CNpc of the job, its sprite named by vf16, placed dx and dy from cell."""
    npc = emu.alloc(NPC_SIZE)
    emu.call(NPC_CTOR, this=npc)
    emu.write_u32(npc + 0x1B8, job & 0xFFFFFFFF)
    emu.call(NPC_SET_JOB, job & 0xFFFFFFFF, this=npc)
    place(emu, world, mode, attr, cell, npc, dx, dy)


def add_pc(emu, world, mode, attr, cell, look):
    """A CPc dressed by vf14 (job, sex, hair, weapon kind, the three headgear, shield, hair and
    clothes colours), at the base action plus the direction (+0x20, +0x24) and the frame (+0x28),
    placed dx and dy from cell."""
    *dress, base, direction, frame, dx, dy = look
    pc = emu.alloc(PC_SIZE)
    emu.call(PC_CTOR, this=pc)
    emu.call(PC_DRESS, *(v & 0xFFFFFFFF for v in dress), this=pc)
    emu.write_u32(pc + 0x20, base & 0xFFFFFFFF)
    emu.write_u32(pc + 0x24, (base + direction) & 0xFFFFFFFF)
    emu.write_u32(pc + 0x28, frame & 0xFFFFFFFF)
    place(emu, world, mode, attr, cell, pc, dx, dy)


def frames(item):
    ragres, install, cache, sprites, npcs, pcs, mapname = item
    with tempfile.TemporaryDirectory() as tmp:
        world_oracle.extract_world(ragres, install, tmp, mapname)
        emu = world_oracle.boot()
        emu.serve_data_files({})
        emu.data_files = CachedFiles(cache, world_oracle.served(tmp))
        if npcs or pcs:
            emu.data_files.paths.update(CachedFiles(sprites, {}).paths)
    stand_in_jpeg(emu)
    textures = queued_textures(emu, emu.read_u32(G_PSCENE_RENDERER))
    emu.hook_code(CTEXTURE_CTOR, CTEXTURE_CTOR,
                  lambda uc, address, size, _: textures.append(uc.reg_read(UC_X86_REG_ECX)))
    table = emu.data_files.get("data\\indoorrswtable.txt")
    indoor = f"{mapname}.rsw".encode() in {line.strip().split(b"#")[0] for line in table.splitlines()}
    manager = emu.call(world_oracle.GET_RESOURCE_MANAGER, emu.cstr("x")).eax
    if not emu.call(world_oracle.GET_RESOURCE, emu.cstr(f"{mapname}.rsw"), this=manager).eax:
        return "failed"
    world, _, cell = world_oracle.load(emu, manager, mapname, walkable_cell)
    mode = emu.read_u32(world + 4)
    renderer = emu.read_u32(G_PSCENE_RENDERER)

    out = emu.scratch(size=8)
    emu.call(GET_CELL_CENTRE, cell[0], cell[1], out, out + 4, this=mode)
    x, z = struct.unpack("<ff", emu.mem_read(out, 8))
    attr = emu.read_u32(world + 0x40)
    y = emu.call(GET_HEIGHT, *struct.unpack("<II", emu.mem_read(out, 8)), this=attr).st0
    player = emu.scratch(size=0x20)
    emu.mem_write(player + 4, struct.pack("<fff", x, y, z))
    emu.write_u32(world + 0x3C, player)
    view = emu.alloc(CVIEW_SIZE)
    emu.call(CVIEW_CTOR, this=view)
    emu.write_u32(view + 0x1B4, world)
    emu.write_u32(mode + 0xB8, view)
    emu.call(CVIEW_RESET, this=view)
    emu.replace_thiscall(IS_INDOOR_MAP, 1, lambda this, name: int(indoor))
    emu.call(PLACE_CAMERA, this=mode)
    assert emu.call(LOAD_FOG_TABLE, this=G_CLIENT_STATE).eax == 1
    emu.write_u32(DEVICE_TRI_RASTER_CAPS, emu.read_u32(DEVICE_TRI_RASTER_CAPS) | RASTERCAPS_FOGVERTEX)
    emu.mem_write(mode + 0x50, f"{mapname}.rsw".encode() + b"\0")
    emu.call(APPLY_MAP_FOG, 1, this=mode)
    emu.mem_write(CELL_CURSOR, struct.pack("<iiII", cell[0], cell[1], 0, CURSOR_COLOR))
    emu.write_u32(renderer + 0x34, 1)
    if npcs or pcs:
        emu.write_u32(SERVICE_TYPE, 1)
        emu.call(INIT_JOB_NAMES, this=G_CLIENT_STATE)
        emu.call(INIT_SHADOWS, this=G_CLIENT_STATE)
        if pcs:
            emu.call(INIT_NAME_TABLES, this=G_CLIENT_STATE)
        emu.call(CONSTRUCT_PICK_TREE)
        for job, dx, dy in npcs:
            add_npc(emu, world, mode, attr, cell, job, dx, dy)
        for look in pcs:
            add_pc(emu, world, mode, attr, cell, look)
    now = [FRAME_TIME]
    emu.api.stubs["WINMM.DLL!timeGetTime"] = Stub(0, lambda: now[0])

    lines = []
    calls = []
    placed = struct.unpack("<f", emu.mem_read(view + 0x20, 4))[0]
    for turn in TURNS:
        if turn is not None:
            yaw = struct.pack("<f", placed + turn)
            emu.mem_write(view + 0x20, yaw)
            emu.mem_write(view + 0x38, yaw)
        emu.call(STEP_WATER, this=emu.read_u32(world + 0x38))
        emu.call(CVIEW_UPDATE, this=view)
        if npcs or pcs:
            emu.call(CLEAR_PICK_TREE, this=G_PICK_TREE)
        start = len(emu.directx.calls)
        emu.call(DRAW_WORLD, this=view)
        calls.append([c for c in emu.directx.calls[start:]
                      if c.interface == "IDirect3DDevice7" and c.method != "SetTransform"])
        for offset in (0x34, 0x3C):
            emu.write_u32(renderer + offset, emu.read_u32(renderer + offset) + 1)
        emu.call(REWIND_POOLS, this=renderer)
        now[0] += FRAME_TIME_STEP

    names, unnamed = {}, 0
    for k, t in enumerate(textures):
        name = emu.mem_read(t + 0x20, 256).split(b"\0")[0]
        if not name:
            name = f"#{unnamed}".encode()
            unnamed += 1
        names[emu.read_u32(t + 0xC)] = name
        lines.append(f"X {k} {world_oracle.archive_name(name)}")
    for k, frame in enumerate(calls):
        lines.append(f"frame {k}")
        for c in frame:
            lines.append(row(c, names))
    return "\n".join(lines), cell


def row(c, names):
    if c.method == "SetRenderState":
        return f"R {c.args[0]} {c.args[1]}"
    if c.method == "SetTextureStageState":
        return f"S {c.args[0]} {c.args[1]} {c.args[2]}"
    if c.method == "SetTexture":
        name = names.get(c.args[1], b"-") if c.args[1] else b"-"
        return f"T {c.args[0]} {world_oracle.archive_name(name)}"
    if c.method == "DrawPrimitive":
        raw = c.detail["raw"]
        words = struct.unpack(f"<{len(raw) // 4}I", raw)
        values = " ".join(f"{v:08x}" for v in words) if RAW else f"{world_oracle.fnv1a64(words):016x}"
        return f"D {c.args[0]} {c.args[1]:#x} {c.args[3]} {values}"
    return f"? {c.method} {' '.join(f'{a:#x}' for a in c.args)}"


def init():
    pass


def work(_, item):
    try:
        return frames(item)
    except Exception as e:
        return f"error: {e}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("maps", nargs="*")
    ap.add_argument("--install", default=os.path.join(ROOT, "original", "install"))
    ap.add_argument("--ragres", default=os.path.join(ROOT, "build", "dev", "src", "ragres"))
    ap.add_argument("--cache", default=os.path.join(ROOT, "build", "oracle-data"))
    ap.add_argument("--sample", type=int, default=12, help="maps to compare (0: every map)")
    ap.add_argument("--jobs", type=int, default=0, help="worker processes (0: one per core but one)")
    ap.add_argument("--raw", action="store_true", help="each draw's vertices, not their hash")
    ap.add_argument("--show", type=int, default=0, help="differing lines to print for each map")
    ap.add_argument("--original-only", action="store_true", help="print the original's frames only")
    ap.add_argument("--sprites", default=os.path.join(ROOT, "build", "oracle-actors"),
                    help="the cache of sprites, actions, .imf files and palettes, for --npc and --pc")
    ap.add_argument("--npc", nargs=3, type=int, action="append", default=[], metavar=("JOB", "DX", "DY"),
                    help="a CNpc of the job that far from the player's cell")
    ap.add_argument("--pc", nargs=len(PC_ARGS), type=int, action="append", default=[], metavar=PC_ARGS,
                    help="a CPc so dressed, at the base action plus the direction and the frame, that far from the "
                    "player's cell")
    args = ap.parse_args()
    build_cache(args.ragres, args.install, args.cache)
    if args.npc or args.pc:
        build_cache(args.ragres, args.install, args.sprites, ACTOR_PATTERNS)
    maps = args.maps or world_oracle.sample_maps(args.ragres, args.install, args.sample)
    npcs = [tuple(n) for n in args.npc]
    pcs = [tuple(p) for p in args.pc]
    results = oracle_pool.run([(args.ragres, args.install, args.cache, args.sprites, npcs, pcs, m) for m in maps], init,
                              work,
                              args.jobs or None, label="frames")
    mismatches = 0
    for mapname, result in zip(maps, results):
        if result == "failed":
            print(f"{mapname}: the original does not load it")
            continue
        if isinstance(result, str):
            print(f"{mapname}: the oracle stopped, {result}")
            mismatches += 1
            continue
        want, cell = result
        if args.original_only:
            print(f"{mapname} {cell[0]} {cell[1]}\n{want}")
            continue
        npc_args = [str(v) for n in npcs for v in ("--npc", *n)] + [str(v) for p in pcs for v in ("--pc", *p)]
        got = subprocess.run([args.ragres, "draw-frame", *(["--raw"] if args.raw else []), *npc_args, args.install,
                              mapname, str(cell[0]), str(cell[1])], capture_output=True, text=True).stdout.strip()
        print(f"{mapname}: {summary(got, want)}")
        if got != want:
            mismatches += 1
            diff = [f"    {k}: port     {a}\n    {k}: original {b}"
                    for k, (a, b) in enumerate(zip(got.splitlines(), want.splitlines())) if a != b]
            print("\n".join(diff[:args.show]))
    print("FAILED" if mismatches else "OK")
    sys.exit(1 if mismatches else 0)


def summary(got, want):
    a, b = got.splitlines(), want.splitlines()
    draws = sum(1 for line in b if line.startswith("D "))
    textures = sum(1 for line in b if line.startswith("X "))
    if a == b:
        return f"equal ({textures} textures, {len(b) - textures - len(TURNS)} calls, {draws} draws)"
    differing = sum(1 for x, y in zip(a, b) if x != y) + abs(len(a) - len(b))
    return f"{differing} of {len(b)} lines differ (port {len(a)} lines)"


if __name__ == "__main__":
    main()
