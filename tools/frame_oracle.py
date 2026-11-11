"""Compares the port's whole frames with the original's (docs/render/frame.md).

usage: .venv/bin/python -I tools/frame_oracle.py [--sample N] [--jobs J] [--raw] [--show N]
           [--original-only] [--frames N] [--frame-time MS] [--npc JOB DX DY]... [--pc JOB SEX
           HAIR WEAPON TOP MID BOTTOM SHIELD HAIR_COLOR CLOTHES_COLOR ACTION FACING DX DY]...
           [--item ID IDENTIFIED DROPPED SUB_X SUB_Y DX DY]... [--corpse JOB ACTION FRAME FACING DX
           DY]... [--move OBJECT DX DY SPEED]... [MAP...]

Each map is loaded as tools/world_oracle.py loads it, but with the player on the walkable cell
nearest the attribute map's centre, and with the textures made for real:
TextureCache::GetTexture reads each image through CBitmapRes and C3dDevice::CreateTexture
makes a CTexture of the fake Direct3D device. Only CBitmapRes::LoadJpeg (004280d0), whose
decoder is ijl15.dll, is stood in for: it gives the image's size and its pixels (decoded by
Pillow, with the original's magenta rule) and clears the alpha flag, as the original does.

Then three frames (--frames), each as CGameMode::OnFrame draws one: the world steps
GetFrameStepsDue (00420390) gives, each CWorld::UpdateGameObjects (004c4a10), the water's step
(StepWater) and the view's (CView::Update); then CView::DrawWorld, which draws the visible scene
graph and the cell cursor into the scene lists and ends with SceneRenderer::DrawScene, then the
frame counted (as Present counts it) and the primitive pools rewound (00412e70). The first frame
looks as PlaceCamera places the camera; the second and third turn it by 120 and 240 degrees, and
so on round. The cell cursor is over
the player's cell. The frame count is 1 at the first frame, as if the loading had presented one.
IsIndoorMap is answered from indoorRswTable.txt. The fog is set as CGameMode::OnInit sets it with
the FOG setting on, the client's default (00516570, from fogParameterTable.txt read by
ClientState::LoadFogParameterTable), on a device with vertex fog.

With --npc, each JOB is a CNpc in the world's list (CWorld+8), in the order given: made by its
constructor (0053f950), its job at +0x1b8 named by vf16 (0053fae0, which loads its sprite and
actions for real and keeps the job's shadow scale; the job names and shadows of service type
1), in the centre of the cell DX and DY from the player's. DrawWorld
draws it through CNpc::Render, which records its pick bounds in g_CActorPickNode (constructed,
and cleared before each frame as OnFrame clears it). With --pc, each is a CPc after the NPCs:
made by its constructor (00541fb0), dressed by vf14 (00542c50, with the tables of
ClientState::InitNameTables), playing the action in a loop (CGameActor::SetAction, 00502ff0),
facing so many degrees (+0x38), placed as an NPC is; DrawWorld draws it through CPc::Render. With --item, each is a CItem after the players: made by its constructor (0053abb0)
and sent message 4 (CItem::SendMsg, 0053afb0) with the item tables LoadItemTables loads (every
message of msgStringTable empty, the label's format among them); DrawWorld draws it through
CItem::Render. With --corpse, each is a CCorpse after them, made by CGameMode::AddCorpse with the
names GetActName and GetSpriteName give a CNpc of the job, as CGameActor::LeaveCorpse passes them;
DrawWorld draws it through CRenderObject::Render. With --move, the OBJECT-th object of the list (an
NPC or a player, counting from 1) is sent message 0 through its vf02 at SPEED ms a cell (+0xc8):
at the server's time, from the cell and sixteenths under it (CGameMode::GetCellPoint) to the cell
DX and DY from the player's, with g_PathFinder constructed and given the world's attribute map. The
sprites, actions, .imf files and palettes are read from a second cache.
All are made at the first frame's time, and UpdateGameObjects updates them; PlayWave (004352a0),
which the frames' sounds call, is stood in for. timeGetTime answers 1,000,000 ms in the first
frame and 16 more in each after (--frame-time); the frame timer is set to 60 frames a second
(SetFrameRate, 00420360) 16 ms before the first frame.

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
STEP_WATER, REWIND_POOLS, UPDATE_GAME_OBJECTS = 0x4BC450, 0x412E70, 0x4C4A10
SET_FRAME_RATE, GET_FRAME_STEPS_DUE, FRAME_RATE, PLAY_WAVE = 0x420360, 0x420390, 60, 0x4352A0
LOAD_FOG_TABLE, APPLY_MAP_FOG, G_CLIENT_STATE = 0x598E10, 0x516570, 0x686670
DEVICE_TRI_RASTER_CAPS, RASTERCAPS_FOGVERTEX = 0x623A5C, 0x80  # g_C3dDevice+0x8c
CELL_CURSOR = 0x684E20  # x, z, hidden, colour
CURSOR_COLOR = 0x6600FFAA
TURN = 120.0
CACHE_PATTERNS = ("*.bmp", "*.tga", "*.jpg", "*.rsm", "*.txt")
ACTOR_PATTERNS = ("*.spr", "*.act", "*.imf", "*.pal")
NPC_CTOR, NPC_SET_JOB, NPC_SIZE = 0x53F950, 0x53FAE0, 0x298
PC_CTOR, PC_DRESS, PC_SIZE, SET_ACTION = 0x541FB0, 0x542C50, 0x308, 0x502FF0
ITEM_CTOR, ITEM_SEND, ITEM_SIZE, ITEM_ARRIVES = 0x53ABB0, 0x53AFB0, 0xC0, 4
LOAD_ITEM_TABLES, ITEM_MAP_INITS, MSG_STRINGS, LABEL_MESSAGE = 0x4D48E0, (0x4D3440, 0x4D35A0, 0x4D3700), 0x67CCA4, 0xB7
ITEM_FALLBACK, ITEM_GUARD, CARD_FALLBACK, CARD_GUARD, CODE_PAGE = 0x689140, 0x689130, 0x6891C0, 0x6891B8, 0x67C998
ADD_CORPSE, GET_ACT_NAME, GET_SPRITE_NAME = 0x510030, 0x501960, 0x5018E0
INIT_JOB_NAMES, INIT_SHADOWS, INIT_NAME_TABLES, SERVICE_TYPE = 0x58AC70, 0x59A5E0, 0x588BA0, 0x67C9D4
CONSTRUCT_PICK_TREE, CLEAR_PICK_TREE, G_PICK_TREE = 0x4CE470, 0x4CE710, 0x67CA60
FRAME_TIME, FRAME_TIME_STEP, FRAMES = 1000000, 16, 3
G_PATH_FINDER, PATH_FINDER_SIZE, PATH_FINDER_CTOR, SET_ATTR_MAP = 0x684E30, 0x1098, 0x4CC1B0, 0x4CCC50
GET_CELL_POINT, GET_SERVER_TIME, SEND_MSG_SLOT = 0x511C30, 0x593200, 2
PC_ARGS = ("JOB", "SEX", "HAIR", "WEAPON", "TOP", "MID", "BOTTOM", "SHIELD", "HAIR_COLOR", "CLOTHES_COLOR", "ACTION",
           "FACING", "DX", "DY")
ITEM_ARGS = ("ID", "IDENTIFIED", "DROPPED", "SUB_X", "SUB_Y", "DX", "DY")
CORPSE_ARGS = ("JOB", "ACTION", "FRAME", "FACING", "DX", "DY")
MOVE_ARGS = ("OBJECT", "DX", "DY", "SPEED")
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


def append(emu, world, obj):
    """The object at the end of the world's list (CWorld+0xc, its count +0x10)."""
    head = emu.read_u32(world + 0xC)
    last = emu.read_u32(head + 4)
    node = emu.alloc(12)
    emu.mem_write(node, struct.pack("<III", head, last, obj))
    emu.write_u32(last, node)
    emu.write_u32(head + 4, node)
    emu.write_u32(world + 0x10, emu.read_u32(world + 0x10) + 1)


def cell_centre(emu, mode, attr, cell, dx, dy):
    """GetCellCentre of the cell dx and dy from cell, on the ground (C3dAttr::GetHeight)."""
    out = emu.scratch(size=8)
    emu.call(GET_CELL_CENTRE, cell[0] + dx, cell[1] + dy, out, out + 4, this=mode)
    x, z = struct.unpack("<ff", emu.mem_read(out, 8))
    y = emu.call(GET_HEIGHT, *struct.unpack("<II", emu.mem_read(out, 8)), this=attr).st0
    return x, y, z


def place(emu, world, mode, attr, cell, actor, dx, dy):
    """The actor in the centre of the cell dx and dy from cell, on the ground, at the end of the
    world's list."""
    emu.mem_write(actor + 4, struct.pack("<fff", *cell_centre(emu, mode, attr, cell, dx, dy)))
    append(emu, world, actor)


def add_npc(emu, world, mode, attr, cell, job, dx, dy):
    """A CNpc of the job, its sprite named by vf16, placed dx and dy from cell."""
    npc = emu.alloc(NPC_SIZE)
    emu.call(NPC_CTOR, this=npc)
    emu.write_u32(npc + 0x1B8, job & 0xFFFFFFFF)
    emu.call(NPC_SET_JOB, job & 0xFFFFFFFF, this=npc)
    place(emu, world, mode, attr, cell, npc, dx, dy)


def add_pc(emu, world, mode, attr, cell, look):
    """A CPc dressed by vf14 (job, sex, hair, weapon kind, the three headgear, shield, hair and
    clothes colours), the action played in a loop (CGameActor::SetAction, vf07), facing so many
    degrees (+0x38), placed dx and dy from cell."""
    *dress, action, facing, dx, dy = look
    pc = emu.alloc(PC_SIZE)
    emu.call(PC_CTOR, this=pc)
    emu.call(PC_DRESS, *(v & 0xFFFFFFFF for v in dress), this=pc)
    emu.call(SET_ACTION, action & 0xFFFFFFFF, 0, 0, this=pc)
    emu.mem_write(pc + 0x38, struct.pack("<f", facing))
    place(emu, world, mode, attr, cell, pc, dx, dy)


def load_item_tables(emu):
    """The item tables as the client loads them (LoadItemTables, service type 1), from fresh maps
    and fallbacks; every message of msgStringTable empty, the item's label among them."""
    for f in ITEM_MAP_INITS:
        emu.call(f)
    emu.mem_write(ITEM_FALLBACK, bytes(0x78))
    emu.mem_write(CARD_FALLBACK, bytes(0x30))
    emu.write_u32(ITEM_GUARD, 0)
    emu.write_u32(CARD_GUARD, 0)
    emu.write_u32(CODE_PAGE, 0)
    emu.call(LOAD_ITEM_TABLES)
    empty = emu.cstr("")
    emu.write_u32(MSG_STRINGS, emu.scratch(struct.pack(f"<{LABEL_MESSAGE + 1}I", *[empty] * (LABEL_MESSAGE + 1))))


def add_item(emu, world, cell, number, look):
    """A CItem made by its constructor and sent message 4 (CItem::SendMsg) with a record: the id as
    text, the pick id number, the cell dx and dy from cell at the sixteenths given, dropped or
    not, a count of 1 and the identified byte; at the end of the world's list."""
    item_id, identified, dropped, sub_x, sub_y, dx, dy = look
    item = emu.alloc(ITEM_SIZE)
    emu.call(ITEM_CTOR, this=item)
    record = emu.scratch(b"%d" % item_id + bytes(0x20 - len(b"%d" % item_id))
                         + struct.pack("<7I", number, cell[0] + dx, cell[1] + dy, sub_x & 0xFFFFFFFF,
                                       sub_y & 0xFFFFFFFF, dropped, 1) + bytes([identified]))
    emu.call(ITEM_SEND, 0, ITEM_ARRIVES, record, 0, 0, this=item)
    append(emu, world, item)


def add_corpse(emu, mode, attr, cell, look):
    """A monster's corpse as CGameActor::LeaveCorpse leaves one: CGameMode::AddCorpse at the centre
    of the cell dx and dy from cell, with the names GetActName and GetSpriteName give for a CNpc of
    the job (vf16's, lower-cased with their folder), the action, frame and facing given."""
    job, action, frame, facing, dx, dy = look
    npc = emu.alloc(NPC_SIZE)
    emu.call(NPC_CTOR, this=npc)
    emu.write_u32(npc + 0x1B8, job & 0xFFFFFFFF)
    emu.call(NPC_SET_JOB, job & 0xFFFFFFFF, this=npc)
    act, sprite = (emu.call(f, this=npc).eax for f in (GET_ACT_NAME, GET_SPRITE_NAME))
    x, y, z = (struct.unpack("<I", struct.pack("<f", v))[0] for v in cell_centre(emu, mode, attr, cell, dx, dy))
    emu.call(ADD_CORPSE, x, y, z, act, sprite, action & 0xFFFFFFFF, frame & 0xFFFFFFFF,
             struct.unpack("<I", struct.pack("<f", float(facing)))[0], this=mode)


def world_object(emu, world, number):
    """The number-th object of the world's list (CWorld+0xc), counting from 1."""
    node = emu.read_u32(emu.read_u32(world + 0xC))
    for _ in range(number - 1):
        node = emu.read_u32(node)
    return emu.read_u32(node + 8)


def move(emu, world, mode, cell, order):
    """Message 0 (vf02) to the number-th object of the world's list at the speed given (+0xc8): the
    move time the server's (ClientState::GetServerTime), from the cell and sixteenths under it
    (CGameMode::GetCellPoint) to the cell dx and dy from cell."""
    number, dx, dy, speed = order
    actor = world_object(emu, world, number)
    emu.write_u32(actor + 0xC8, speed & 0xFFFFFFFF)
    out = emu.scratch(size=16)
    x, _, z = struct.unpack("<3I", emu.mem_read(actor + 4, 12))
    emu.call(GET_CELL_POINT, x, z, out, out + 4, out + 8, out + 12, this=mode)
    record = emu.scratch(struct.pack("<I", emu.call(GET_SERVER_TIME, this=G_CLIENT_STATE).eax) + emu.mem_read(out, 16)
                         + struct.pack("<2i", cell[0] + dx, cell[1] + dy))
    emu.call(emu.read_u32(emu.read_u32(actor) + 4 * SEND_MSG_SLOT), 0, 0, record, 0, 0, this=actor)


def frames(item):
    ragres, install, cache, sprites, npcs, pcs, items, corpses, moves, (frame_count, frame_time), mapname = item
    with tempfile.TemporaryDirectory() as tmp:
        world_oracle.extract_world(ragres, install, tmp, mapname)
        emu = world_oracle.boot()
        emu.serve_data_files({})
        emu.data_files = CachedFiles(cache, world_oracle.served(tmp))
        if npcs or pcs or items or corpses:
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
    now = [FRAME_TIME - FRAME_TIME_STEP]
    emu.api.stubs["WINMM.DLL!timeGetTime"] = Stub(0, lambda: now[0])
    emu.call(SET_FRAME_RATE, FRAME_RATE)
    now[0] = FRAME_TIME
    emu.hook_code(PLAY_WAVE, PLAY_WAVE, lambda uc, address, size, _: emu.complete(0, emu.read_u32(emu.esp()),
                                                                                  emu.esp() + 4))
    actors = npcs or pcs or items or corpses
    if actors:
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
        if items:
            load_item_tables(emu)
        for number, look in enumerate(items, 1):
            add_item(emu, world, cell, number, look)
        for look in corpses:
            add_corpse(emu, mode, attr, cell, look)
        if moves:
            emu.mem_write(G_PATH_FINDER, bytes(PATH_FINDER_SIZE))
            emu.call(PATH_FINDER_CTOR, this=G_PATH_FINDER)
            emu.call(SET_ATTR_MAP, attr, this=G_PATH_FINDER)
        for order in moves:
            move(emu, world, mode, cell, order)

    lines = []
    calls = []
    placed = struct.unpack("<f", emu.mem_read(view + 0x20, 4))[0]
    for k in range(frame_count):
        if k:
            yaw = struct.pack("<f", placed + TURN * (k % 3))
            emu.mem_write(view + 0x20, yaw)
            emu.mem_write(view + 0x38, yaw)
        for _ in range(emu.call(GET_FRAME_STEPS_DUE).eax):
            emu.call(UPDATE_GAME_OBJECTS, this=world)
            emu.call(STEP_WATER, this=emu.read_u32(world + 0x38))
            emu.call(CVIEW_UPDATE, this=view)
        if actors:
            emu.call(CLEAR_PICK_TREE, this=G_PICK_TREE)
        start = len(emu.directx.calls)
        emu.call(DRAW_WORLD, this=view)
        calls.append([c for c in emu.directx.calls[start:]
                      if c.interface == "IDirect3DDevice7" and c.method != "SetTransform"])
        for offset in (0x34, 0x3C):
            emu.write_u32(renderer + offset, emu.read_u32(renderer + offset) + 1)
        emu.call(REWIND_POOLS, this=renderer)
        now[0] = (now[0] + frame_time) & 0xFFFFFFFF

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
                    help="a CPc so dressed, playing the action in a loop, facing so many degrees, that far from the "
                    "player's cell")
    ap.add_argument("--item", nargs=len(ITEM_ARGS), type=int, action="append", default=[], metavar=ITEM_ARGS,
                    help="a CItem of the id, identified or not and dropped or not, at those sixteenths of the cell "
                    "that far from the player's")
    ap.add_argument("--corpse", nargs=len(CORPSE_ARGS), type=int, action="append", default=[], metavar=CORPSE_ARGS,
                    help="a CCorpse of a monster of the job, at the action, frame and facing (degrees), that far "
                    "from the player's cell")
    ap.add_argument("--move", nargs=len(MOVE_ARGS), type=int, action="append", default=[], metavar=MOVE_ARGS,
                    help="message 0 to the OBJECT-th object (an NPC or a player, counting from 1), to the cell "
                    "that far from the player's, at SPEED ms a cell")
    ap.add_argument("--frames", type=int, default=FRAMES, help="frames to draw")
    ap.add_argument("--frame-time", type=int, default=FRAME_TIME_STEP,
                    help="milliseconds from one frame to the next, %(default)s each world step")
    args = ap.parse_args()
    build_cache(args.ragres, args.install, args.cache)
    if args.npc or args.pc or args.item or args.corpse:
        build_cache(args.ragres, args.install, args.sprites, ACTOR_PATTERNS)
    maps = args.maps or world_oracle.sample_maps(args.ragres, args.install, args.sample)
    npcs = [tuple(n) for n in args.npc]
    pcs = [tuple(p) for p in args.pc]
    items = [tuple(i) for i in args.item]
    corpses = [tuple(c) for c in args.corpse]
    moves = [tuple(m) for m in args.move]
    clock = (args.frames, args.frame_time)
    given = (npcs, pcs, items, corpses, moves, clock)
    results = oracle_pool.run([(args.ragres, args.install, args.cache, args.sprites, *given, m) for m in maps], init,
                              work, args.jobs or None, label="frames")
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
        npc_args = [str(v) for kind, given in (("--npc", npcs), ("--pc", pcs), ("--item", items), ("--corpse", corpses),
                                               ("--move", moves))
                    for one in given for v in (kind, *one)]
        clock_args = ["--frames", str(args.frames), "--frame-time", str(args.frame_time)]
        got = subprocess.run([args.ragres, "draw-frame", *(["--raw"] if args.raw else []), *clock_args, *npc_args,
                              args.install, mapname, str(cell[0]), str(cell[1])],
                             capture_output=True, text=True).stdout.strip()
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
    frame_lines = sum(1 for line in b if line.startswith("frame "))
    if a == b:
        return f"equal ({textures} textures, {len(b) - textures - frame_lines} calls, {draws} draws)"
    differing = sum(1 for x, y in zip(a, b) if x != y) + abs(len(a) - len(b))
    return f"{differing} of {len(b)} lines differ (port {len(a)} lines)"


if __name__ == "__main__":
    main()
