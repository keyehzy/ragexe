"""Compares the port's path finder with the original on every map (docs/world/path.md).

usage: .venv/bin/python -I tools/path_oracle.py [--queries N] [--jobs J] [--install DIR] [NAME...]

For each .gat of data.grf and the .gpf patches (the last archive's copy), the original's
C3dAttr::Load (00426ec0) loads it in the emulation oracle and g_PathFinder (00684e30) takes
it, as CGameMode does (004ccc50). A script of queries, made from the map's cells with a
seed of its name, then runs on that finder and one actor's MovePath (CGameActor+0x170):
  - PathFinder::FindPath (004cc5c0) between walkable cells near each other, far apart,
    onto cells that cannot be walked, from the map's edges and from outside it, from a
    cell to itself, at assorted sub-cell points, speeds and times;
  - CGameActor::FindMovePath (00506b20), the server's time (ClientState 00593200) given,
    both before and after the path would have ended;
  - CGameMode::IsPathFound (00511ec0), timeGetTime given;
  - MovePath::FindSegment (004cd1f0) at times before, along and after the path.
Oracle.run also takes "Z value", which sets the path's +0x10, for tools/path_vectors.py, and
"W ...", a walk of the path with CGameActor::ProcessState, for tools/walk_oracle.py.
Every result, the message logged (no path, or the nodes run out), and the whole path after
each search are compared with `ragres find-paths` on the same script.
"""
import argparse
import os
import random
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import oracle_pool  # noqa: E402
import world_oracle  # noqa: E402
from emu import Emulator  # noqa: E402
from emu.winapi import Stub  # noqa: E402
from unicorn import UC_PROT_ALL  # noqa: E402
from unicorn.x86_const import UC_X86_REG_FPCW  # noqa: E402

C3DATTR_CTOR, C3DATTR_LOAD = 0x426BA0, 0x426EC0
PATH_FINDER_CTOR, SET_ATTR_MAP, FIND_PATH = 0x4CC1B0, 0x4CCC50, 0x4CC5C0
G_PATH_FINDER, PATH_FINDER_SIZE = 0x684E30, 0x1098
MOVE_PATH_CTOR, FIND_SEGMENT = 0x4CCF10, 0x4CD1F0
FIND_MOVE_PATH, SERVER_TIME, IS_PATH_FOUND = 0x506B20, 0x593200, 0x511EC0
DEBUG_LOG = 0x4060F0
NO_PATH, OVERFLOW = 0x60C1D0, 0x60C1F4
ACTOR_PATH, ACTOR_MOVE_TIME, ACTOR_SIZE = 0x170, 0x18C, 0x400
GAME_ACTOR_VTABLE, PROCESS, SET_STATE = 0x5E95C0, 0x5038C0, 0x5034E0
GET_GAME_MODE, GET_FRAME_COUNT = 0x4CBF30, 0x421A80
SEND_MESSAGE_SLOT, MESSAGE_STUB, SERVICE_TYPE, MESSAGE = 5, 0x7E000000, 0x67C9D4, 0x504460
PC_VTABLE, CLOSE_WINDOW, MODE_FIELD_2A0 = 0x5E9B94, 0x4A90C0, 0x2A0
# W: the CGameActor fields a walk starts from, in order: +4 and +0xc (float bits), +0x38,
# the camera's yaw, +0x44 and +0x198 (float bits), +0x20, +0x1b8, +0x21c, +0x190, the frames
# of every action, +0x1d0, +0x234, +0x7c, +0x82, +0x1a0 and +0x19c; then the updates, each
# the server's time and timeGetTime.
WALK_FIELDS = ["x", "z", "facing", "yaw", "delay", "travelled", "base", "job", "effect", "pushed",
               "frames", "hold", "fade", "color", "held", "server_at", "processed_at"]
# G: a walk's fields, then the state before (+0x58), the speed (+0xc8), +0xd0, +0xd8, the
# service type, the .act's delays of actions 0 and 8 and +0x50 (float bits), the target of
# state 4 (+0x228, +0x22c), and message 0's order: its time, the start cell and its sixteenths,
# the goal, the actor's id (+0x1b4), +0x4c (float bits), +0x81, how many delays the .act
# has, and +0x194; then the message's server time and timeGetTime, and the updates'.
GO_FIELDS = WALK_FIELDS + ["state", "speed", "f_d0", "f_d8", "service", "delay0", "delay8", "scale",
                           "target_x", "target_z", "time", "sx", "sy", "subx", "suby", "gx", "gy", "id",
                           "repeats", "finished", "delay_count", "segment"]
# P: a G for a CPc, then its +0x218, +0x294, +0x1cc, +0x224 and +0xcc; the hash also takes,
# each step, CGameMode+0x2a0 (0x5a5a5a5a before), +0x1cc, +0x294, +0xcc and the windows closed.
PLAYER_FIELDS = GO_FIELDS + ["f218", "f294", "f1cc", "f224", "fcc"]
# T and U: a G and a P whose move is message 0x13 from where the actor stands, to the goal
# (gx, gy) at the speed given last (the order's time and start unread).
TO_SPEED = ["to_speed"]
# N and Y: a G and a P whose actor stands (state 0) with no message, its updates
# CGameActor::ProcessState's standing: then srand's seed, ClientState's pet (+0x7d0) and the two
# lines its tables give (005aa3e0's, then the table +0x824's), +0x1d4, the play mode +0x54 and
# +0x1f4. The hash takes +0x1d4 too.
STAND_FIELDS = ["seed", "pet_id", "pet_first", "pet_second", "talk_at", "play", "f1f4"]
SRAND, PET_ID, PET_TABLE = 0x5C2546, 0x686E40, 0x686E94
PET_LEVEL_A, PET_LEVEL_B, PET_LINE = 0x5A9F40, 0x5A9FA0, 0x5AA3E0
SINGLE = 0x007F
SPEEDS = [150, 150, 150, 100, 200, 75, 300, 120, 1, 7, 999]


def fnv1a64(values, h=0xCBF29CE484222325):
    for v in values:
        for b in struct.pack("<I", v & 0xFFFFFFFF):
            h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def cells(data):
    """The width, height and attributes of a .gat as C3dAttr::Load reads it, or None."""
    if data[:4] != b"GRAT" or data[4] != 1 or data[5] >= 3:
        return None
    width, height = struct.unpack_from("<ii", data, 6)
    count = width * height
    raw = data[14:14 + 20 * count]
    if len(raw) < 20 * count:
        return width, height, [0] * count
    return width, height, [struct.unpack_from("<i", raw, 20 * k + 16)[0] for k in range(count)]


def make_queries(name, width, height, attrs, count):
    """The query script for a map: count searches, with FindSegment queries after some."""
    rng = random.Random(zlib.crc32(name.encode()))
    walkable = [(k % width, k // width) for k, a in enumerate(attrs) if a not in (1, 5)]
    blocked = [(k % width, k // width) for k, a in enumerate(attrs) if a in (1, 5)]
    if not walkable:
        walkable = [(0, 0)]

    def near(x, y, r):
        return x + rng.randint(-r, r), y + rng.randint(-r, r)

    def time():
        return rng.choice([rng.getrandbits(32), rng.randint(0, 100000), 0xFFFFFFFF - rng.randint(0, 3000)])

    def sub():
        return rng.choice([rng.randint(0, 15), rng.randint(0, 15), 0, 8, rng.randint(-20, 40)])

    lines = []
    for _ in range(count):
        sx, sy = rng.choice(walkable)
        kind = rng.random()
        if kind < 0.45:
            gx, gy = near(sx, sy, rng.choice([1, 2, 4, 8, 12, 16]))
        elif kind < 0.6:
            gx, gy = rng.choice(walkable)
        elif kind < 0.7 and blocked:
            gx, gy = rng.choice(blocked)
            sx, sy = near(gx, gy, 6)
        elif kind < 0.75:
            gx, gy = sx, sy
        elif kind < 0.85:
            sx, sy = rng.choice([(0, sy), (sx, 0), (0, 0), (width - 1, sy), (sx, height - 1)])
            gx, gy = near(sx, sy, 6)
        else:
            sx, sy = rng.choice([(-1, sy), (sx, -1), (width, sy), (sx, height), (-3, -2)])
            gx, gy = near(sx, sy, 5)
        speed = rng.choice(SPEEDS)
        what = rng.random()
        if what < 0.2:
            move_time = time()
            now = (move_time + rng.choice([0, rng.randint(0, 3000), rng.randint(0, 20000)])) & 0xFFFFFFFF
            lines.append(f"M {move_time} {now} {sx} {sy} {sub()} {sub()} {gx} {gy} {speed}")
        elif what < 0.3:
            lines.append(f"I {time()} {sx} {sy} {sub()} {sub()} {gx} {gy}")
            continue
        else:
            start = time()
            lines.append(f"F {start} {sx} {sy} {sub()} {sub()} {gx} {gy} {speed}")
        for _ in range(rng.choice([0, 0, 1, 3])):
            lines.append(f"S {time()}")
    return lines


class Oracle:
    def __init__(self):
        emu = Emulator(max_instructions=200_000_000).init_crt()
        emu.uc.reg_write(UC_X86_REG_FPCW, SINGLE)
        self.emu = emu
        self.attr = emu.scratch(size=0x12C)
        self.actor = emu.scratch(size=ACTOR_SIZE)
        self.out = emu.scratch(size=16)
        self.now = 0
        self.logged = []
        emu.mem_write(G_PATH_FINDER, bytes(PATH_FINDER_SIZE))
        emu.call(PATH_FINDER_CTOR, this=G_PATH_FINDER)
        self.server = 0
        emu.replace_thiscall(SERVER_TIME, 0, lambda this: self.server)
        emu.api.stubs["WINMM.DLL!timeGetTime"] = Stub(0, lambda: self.now)

        def debug_log(uc, address, size, _):
            esp = emu.esp()
            self.logged.append(emu.read_u32(esp + 4))
            emu.complete(0, emu.read_u32(esp), esp + 4)
        emu.hook_code(DEBUG_LOG, DEBUG_LOG, debug_log)
        # CGameActor::ProcessState reads the world through the game mode (+0xb4, its attribute map
        # at +0x40) and the camera's yaw through its view (+0xb8, +0x20).
        self.mode, self.world, self.view = (emu.scratch(size=0x400) for _ in range(3))
        emu.write_u32(self.mode + 0xB4, self.world)
        emu.write_u32(self.mode + 0xB8, self.view)
        emu.write_u32(self.world + 0x40, self.attr)
        emu.replace_thiscall(GET_GAME_MODE, 0, lambda this: self.mode)
        self.frames = 1
        emu.replace_thiscall(GET_FRAME_COUNT, 1, lambda this, action: self.frames if self.frames > 1 else 1)
        # SetState is recorded; for W it is skipped, for G it runs.
        self.states, self.skip_states = [], True

        def set_state(uc, address, size, _):
            esp = emu.esp()
            self.states.append(emu.read_u32(esp + 4))
            if self.skip_states:
                emu.complete(0, emu.read_u32(esp), esp + 8)
        emu.hook_code(SET_STATE, SET_STATE, set_state)
        # The game mode's messages (CGameMode::SendMsg, vf05) are recorded.
        emu.uc.mem_map(MESSAGE_STUB, 0x1000, UC_PROT_ALL)
        emu.mem_write(MESSAGE_STUB, b"\xC3")
        mode_vtable = emu.scratch(size=0x100)
        emu.write_u32(mode_vtable + 4 * SEND_MESSAGE_SLOT, MESSAGE_STUB)
        emu.write_u32(self.mode, mode_vtable)
        self.messages = []
        emu.replace_thiscall(MESSAGE_STUB, 4, lambda this, *v: self.messages.extend(v))
        self.windows = []
        emu.replace_thiscall(CLOSE_WINDOW, 1, lambda this, window: self.windows.append(window))
        # The pet's lines (ClientState's tables): both levels 0, so the table's first record.
        self.pet_line = 0
        emu.replace_thiscall(PET_LEVEL_A, 1, lambda this, v: 0)
        emu.replace_thiscall(PET_LEVEL_B, 1, lambda this, v: 0)
        emu.replace_thiscall(PET_LINE, 3, lambda this, a, b, c: self.pet_line)
        # The body's CActRes: GetActionDelay reads the delays at +0x138..+0x13c.
        self.act = emu.scratch(size=0x144)
        self.delays = emu.scratch(size=4 * 9)
        emu.write_u32(self.act + 0x138, self.delays)
        emu.write_u32(self.act + 0x13C, self.delays + 4 * 9)

    def result(self, found):
        if found:
            return "found"
        if not self.logged:
            return "same"
        return {NO_PATH: "none", OVERFLOW: "overflow"}.get(self.logged[-1], "?")

    def path(self):
        emu, path = self.emu, self.actor + ACTOR_PATH
        begin, end = emu.read_u32(path + 4), emu.read_u32(path + 8)
        steps = struct.unpack(f"<{(end - begin) // 4}I", emu.mem_read(begin, end - begin)) if begin else ()
        field, sx, sy = struct.unpack("<iII", emu.mem_read(path + 0x10, 12))
        return field, sx, sy, len(steps) // 4, fnv1a64(steps), steps

    def run(self, data, lines):
        emu = self.emu
        mark = emu.heap_mark()
        emu.serve_data_files({"data\\t.gat": data})
        emu.mem_write(self.attr, bytes(0x12C))
        emu.call(C3DATTR_CTOR, this=self.attr)
        emu.call(C3DATTR_LOAD, emu.cstr("t.gat"), this=self.attr)
        emu.mem_write(G_PATH_FINDER, bytes(PATH_FINDER_SIZE))
        emu.call(PATH_FINDER_CTOR, this=G_PATH_FINDER)
        emu.call(SET_ATTR_MAP, self.attr, this=G_PATH_FINDER)
        emu.mem_write(self.actor, bytes(ACTOR_SIZE))
        emu.call(MOVE_PATH_CTOR, this=self.actor + ACTOR_PATH)
        out = []
        self.paths = []
        for line in lines:
            kind, *values = line.split()
            v = [int(x) for x in values] if kind not in ("W", "G", "P", "T", "U", "N", "Y") else []
            self.logged = []
            if kind == "F":
                found = emu.call(FIND_PATH, *v, self.actor + ACTOR_PATH, this=G_PATH_FINDER).eax & 0xFF
                field, sx, sy, count, digest, steps = self.path()
                self.paths.append(steps)
                out.append(f"{self.result(found)} - {field} {sx:08x} {sy:08x} {count} {digest:016x}")
            elif kind == "M":
                emu.write_u32(self.actor + ACTOR_MOVE_TIME, v[0])
                self.server = v[1]
                found = emu.call(FIND_MOVE_PATH, *v[2:], this=self.actor).eax & 0xFF
                field, sx, sy, count, digest, steps = self.path()
                self.paths.append(steps)
                move_time = emu.read_u32(self.actor + ACTOR_MOVE_TIME)
                out.append(f"{self.result(found)} {move_time} {field} {sx:08x} {sy:08x} {count} {digest:016x}")
            elif kind == "I":
                self.now = v[0]
                found = emu.call(IS_PATH_FOUND, *v[1:], this=0).eax & 0xFF
                out.append(f"{1 if found else 0}")
            elif kind == "S":
                if self.path()[3] < 2:
                    out.append("-")
                    continue
                index = emu.call(FIND_SEGMENT, v[0], self.out, self.out + 4, self.out + 8,
                                 this=self.actor + ACTOR_PATH).eax
                index -= (index & 0x80000000) << 1
                time, x, y = struct.unpack("<III", emu.mem_read(self.out, 12))
                out.append(f"{index} {time} {x:08x} {y:08x}")
            elif kind == "Z":
                emu.write_u32(self.actor + ACTOR_PATH + 0x10, v[0] & 0xFFFFFFFF)
                out.append(f"{v[0]}")
            elif kind == "W":
                out.append(self.walk(values))
            elif kind == "G":
                out.append(self.go(values))
            elif kind == "P":
                out.append(self.go(values, player=True))
            elif kind == "T":
                out.append(self.go(values, to=True))
            elif kind == "U":
                out.append(self.go(values, player=True, to=True))
            elif kind == "N":
                out.append(self.go(values, stand=True))
            elif kind == "Y":
                out.append(self.go(values, player=True, stand=True))
        emu.heap_release(mark)
        return out

    def walk(self, values):
        """CGameActor::ProcessState on the actor walking its path, update by update until it arrives
        or has faded."""
        emu, actor = self.emu, self.actor
        if self.path()[3] < 2:
            return "-"
        v = [int(x, 0) for x in values]
        w = dict(zip(WALK_FIELDS, v))
        updates = v[len(WALK_FIELDS):]
        self.place(w)
        self.skip_states = True
        h, calls, last = 0xCBF29CE484222325, 0, "walking"
        for server, tick in zip(updates[::2], updates[1::2]):
            self.states = []
            self.now, self.server = tick, server
            ret = emu.call(PROCESS, this=actor).eax & 0xFF
            calls += 1
            state = self.states[0] if self.states else 0xFFFFFFFF
            fields = [ret, *(emu.read_u32(actor + o) for o in (0x4, 0x8, 0xC, 0x38, 0x24, 0x28, 0x194, 0x198,
                                                                0x19C, 0x1A0, 0x7C)),
                      emu.mem_read(actor + 0x82, 1)[0], emu.mem_read(actor + 0x190, 1)[0], state]
            h = fnv1a64(fields, h)
            if ret == 0:
                last = "faded"
                break
            if self.states:
                last = "arrived"
                break
        r = [emu.read_u32(actor + o) for o in (0x4, 0x8, 0xC, 0x38, 0x24, 0x28, 0x198)]
        return (f"{last} {calls} {r[0]:08x} {r[1]:08x} {r[2]:08x} {r[3]:08x} {r[4]} {r[5]} {r[6]:08x} "
                f"{h:016x}")

    def place(self, w):
        """The actor of a W or G query, its path kept."""
        emu, actor = self.emu, self.actor
        emu.write_u32(actor, GAME_ACTOR_VTABLE)
        for offset, name in [(0x4, "x"), (0xC, "z"), (0x38, "facing"), (0x44, "delay"), (0x198, "travelled"),
                             (0x20, "base"), (0x1B8, "job"), (0x21C, "effect"), (0x1D0, "hold"),
                             (0x234, "fade"), (0x7C, "color"), (0x1A0, "server_at"), (0x19C, "processed_at")]:
            emu.write_u32(actor + offset, w[name] & 0xFFFFFFFF)
        for offset in (0x8, 0x24, 0x28, 0x194):
            emu.write_u32(actor + offset, 0)
        emu.write_u32(actor + 0x58, 1)
        emu.write_u32(actor + 0x9C, actor)
        emu.mem_write(actor + 0x190, bytes([w["pushed"]]))
        emu.mem_write(actor + 0x82, bytes([w["held"]]))
        emu.write_u32(self.view + 0x20, w["yaw"])
        self.frames = w["frames"]

    def go(self, values, player=False, to=False, stand=False):
        """CGameActor::vf02's message 0 on the actor, then ProcessState update by update, with
        SetState run, until the actor stands again or has faded; for a player, a CPc's."""
        emu, actor = self.emu, self.actor
        v = [int(x, 0) for x in values]
        names = (PLAYER_FIELDS if player else GO_FIELDS) + (TO_SPEED if to else []) + (STAND_FIELDS if stand else [])
        g = dict(zip(names, v))
        updates = v[len(names):]
        self.place(g)
        self.skip_states = False
        extra = lambda: []
        if player:
            emu.write_u32(actor, PC_VTABLE)
            for offset, name in ((0x218, "f218"), (0x294, "f294"), (0x1CC, "f1cc"), (0x224, "f224"), (0xCC, "fcc")):
                emu.write_u32(actor + offset, g[name] & 0xFFFFFFFF)
            emu.write_u32(self.mode + MODE_FIELD_2A0, 0x5A5A5A5A)
            self.windows = []

            def extra():
                out = [emu.read_u32(self.mode + MODE_FIELD_2A0),
                       *(emu.read_u32(actor + o) for o in (0x1CC, 0x294, 0xCC)), *self.windows]
                self.windows = []
                return out
        # Fields no query writes, at the port's defaults, 0.
        for offset in (0x54, 0x70, 0xC0, 0xC4, 0x18C, 0x1AC):
            emu.write_u32(actor + offset, 0)
        emu.write_u32(actor + 0x4C, g["repeats"])
        emu.mem_write(actor + 0x81, bytes([g["finished"]]))
        emu.write_u32(actor + 0x194, g["segment"] & 0xFFFFFFFF)
        emu.write_u32(actor + 0x58, g["state"])
        emu.write_u32(actor + 0xC8, g["speed"] & 0xFFFFFFFF)
        emu.write_u32(actor + 0xD0, g["f_d0"])
        emu.write_u32(actor + 0xD8, g["f_d8"])
        emu.write_u32(actor + 0x9C, self.act)
        emu.write_u32(actor + 0x50, g["scale"])
        emu.write_u32(actor + 0x228, g["target_x"])
        emu.write_u32(actor + 0x22C, g["target_z"])
        emu.write_u32(SERVICE_TYPE, g["service"])
        emu.write_u32(actor + 0x1B4, g["id"] & 0xFFFFFFFF)
        delays = [0x40800000] * 9
        delays[0], delays[8] = g["delay0"], g["delay8"]
        emu.mem_write(self.delays, struct.pack("<9I", *delays))
        emu.write_u32(self.act + 0x13C, self.delays + 4 * g["delay_count"])
        order = emu.scratch(struct.pack("<I6i", g["time"] & 0xFFFFFFFF, g["sx"], g["sy"], g["subx"], g["suby"],
                                        g["gx"], g["gy"]))
        if stand:
            emu.call(SRAND, g["seed"] & 0xFFFFFFFF)
            emu.write_u32(PET_ID, g["pet_id"] & 0xFFFFFFFF)
            self.pet_line = g["pet_first"] & 0xFFFFFFFF
            emu.write_u32(PET_TABLE, g["pet_second"] & 0xFFFFFFFF)
            emu.write_u32(actor + 0x1D4, g["talk_at"] & 0xFFFFFFFF)
            emu.write_u32(actor + 0x54, g["play"])
            emu.write_u32(actor + 0x1F4, g["f1f4"] & 0xFFFFFFFF)
            return self.stand(updates, extra)
        h = 0xCBF29CE484222325
        self.messages, self.states = [], []
        self.server, self.now = updates[0], updates[1]
        updates = updates[2:]
        if to:
            emu.call(MESSAGE, 0, 0x13, g["gx"] & 0xFFFFFFFF, g["gy"] & 0xFFFFFFFF, g["to_speed"] & 0xFFFFFFFF,
                     this=actor)
        else:
            emu.call(MESSAGE, 0, 0, order, 0, 0, this=actor)
        count, digest = self.path()[3:5]
        h = fnv1a64([2, *self.fields(), count, digest, digest >> 32, *self.messages, *extra()], h)
        last, calls = "walking", 0
        if emu.read_u32(actor + 0x58) != 1:
            last = "standing"
            updates = []
        for server, tick in zip(updates[::2], updates[1::2]):
            self.messages, self.states = [], []
            self.now, self.server = tick, server
            ret = emu.call(PROCESS, this=actor).eax & 0xFF
            calls += 1
            h = fnv1a64([ret, *self.fields(), *self.messages, *extra()], h)
            if ret == 0:
                last = "faded"
                break
            if emu.read_u32(actor + 0x58) != 1:
                last = "arrived"
                break
        r = [emu.read_u32(actor + o) for o in (0x4, 0x8, 0xC, 0x38, 0x24, 0x28, 0x58)]
        return f"{last} {calls} {r[0]:08x} {r[1]:08x} {r[2]:08x} {r[3]:08x} {r[4]} {r[5]} {r[6]} {h:016x}"

    def stand(self, updates, extra):
        """ProcessState update by update on a standing actor, until its fade ends."""
        emu, actor = self.emu, self.actor
        h, calls, last = 0xCBF29CE484222325, 0, "standing"
        for server, tick in zip(updates[::2], updates[1::2]):
            self.messages, self.states = [], []
            self.now, self.server = tick, server
            ret = emu.call(PROCESS, this=actor).eax & 0xFF
            calls += 1
            h = fnv1a64([ret, *self.fields(), *self.messages, *extra(), emu.read_u32(actor + 0x1D4)], h)
            if ret == 0:
                last = "faded"
                break
        r = [emu.read_u32(actor + o) for o in (0x4, 0x8, 0xC, 0x38, 0x24, 0x28, 0x58)]
        return f"{last} {calls} {r[0]:08x} {r[1]:08x} {r[2]:08x} {r[3]:08x} {r[4]} {r[5]} {r[6]} {h:016x}"

    def fields(self):
        """What the move, the walk and the states write, for G's hash."""
        emu, a = self.emu, self.actor
        words = [emu.read_u32(a + o) for o in (0x58, 0x4, 0x8, 0xC, 0x38, 0x20, 0x24, 0x28, 0x44, 0x4C, 0x50,
                                               0x54, 0x70, 0x1AC, 0xC0, 0xC4, 0x194, 0x198, 0x19C, 0x1A0,
                                               0x7C, 0x18C)]
        return words + [emu.mem_read(a + 0x81, 1)[0], emu.mem_read(a + 0x82, 1)[0], emu.mem_read(a + 0x190, 1)[0]]


def init():
    return Oracle()


def work(oracle, item):
    path, lines = item
    with open(path, "rb") as f:
        return oracle.run(f.read(), lines)


def arguments(queries_help, queries=200):
    ap = argparse.ArgumentParser()
    ap.add_argument("names", nargs="*", help="maps to check (all by default)")
    ap.add_argument("--install", default=os.path.join(ROOT, "original", "install"))
    ap.add_argument("--ragres", default=os.path.join(ROOT, "build", "dev", "src", "ragres"))
    ap.add_argument("--queries", type=int, default=queries, help=queries_help)
    ap.add_argument("--jobs", type=int, default=0, help="worker processes (0: one per core but one)")
    return ap.parse_args()


def run_maps(args, make_script, label):
    """Each map's script, from make_script(name, width, height, attrs, count), run by ragres
    and by the original: [(name, lines, port's results)] and the original's results."""
    with tempfile.TemporaryDirectory() as tmp:
        for archive in world_oracle.archives(args.install):
            subprocess.run([args.ragres, "extract", archive, tmp, "*.gat"], check=True, capture_output=True)
        gats = {os.path.splitext(n)[0].lower(): os.path.join(d, n)
                for d, _, ns in os.walk(tmp) for n in ns if n.lower().endswith(".gat")}
        names = sorted(args.names or gats)
        missing = [n for n in names if n not in gats]
        if missing:
            sys.exit(f"no such maps: {', '.join(missing)}")
        items, ours = [], []
        for name in names:
            with open(gats[name], "rb") as f:
                decoded = cells(f.read())
            if decoded is None:
                continue
            lines = make_script(name, *decoded, args.queries)
            script = os.path.join(tmp, name + ".queries")
            with open(script, "w") as f:
                f.write("\n".join(lines) + "\n")
            result = subprocess.run([args.ragres, "find-paths", gats[name], script], capture_output=True, text=True)
            items.append((gats[name], lines))
            ours.append((name, lines, result.stdout.splitlines()))
        originals = oracle_pool.run(items, init, work, args.jobs or None, label=label)
    return ours, originals


def report(ours, originals, totals, what):
    """Prints the maps whose results differ; 0 when none do."""
    differing = []
    for (name, lines, got), want in zip(ours, originals):
        bad = [(line, g, w) for line, g, w in zip(lines, got + [None] * len(lines), want) if g != w]
        if bad or len(got) != len(want):
            differing.append((name, bad))
    queries = sum(len(w) for w in originals)
    print(f"{len(ours)} maps, {queries} {what}: " + ", ".join(f"{k} {v}" for k, v in sorted(totals.items())))
    for name, bad in differing[:20]:
        print(f"MISMATCH {name}: {len(bad)} queries differ")
        for line, g, w in bad[:3]:
            print(f"    {line[:300]}\n        port     {g}\n        original {w}")
    print(f"{len(differing)} maps differ")
    print("FAILED" if differing else "OK")
    return 1 if differing else 0


def main():
    args = arguments("searches per map")
    ours, originals = run_maps(args, make_queries, "paths")
    totals = {}
    for (name, lines, got), want in zip(ours, originals):
        for line, result in zip(lines, want):
            kind = line[0]
            key = result.split()[0] if kind in "FM" else {"I": "IsPathFound", "S": "FindSegment"}[kind]
            totals[key] = totals.get(key, 0) + 1
    return report(ours, originals, totals, "queries")


if __name__ == "__main__":
    sys.exit(main())
