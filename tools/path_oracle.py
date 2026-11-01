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
# E and Q: a G and a P whose actor, from the state before, is given a state through its vf10
# (CGameActor::SetState, CPc::SetState) when `set` is 1, and then updated by ProcessState: the
# state, +0xcc, +0x1a8, whether CWorld::GetActorById finds that actor and where it stands
# (+4, +0xc, float bits), the play mode +0x54 and the state's start +0x70; then the state's
# server time and timeGetTime, and the updates'. +0x1d4 and +0x1f4 are 0. The hash takes +0xcc
# too.
ENTER_FIELDS = ["set", "enter", "f0cc", "f1a8", "found", "tx", "tz", "play", "start"]
GET_ACTOR_BY_ID = 0x4C4CF0
# A: a G whose actor comes into view through CGameActor::SendMsg's message 3, 4, 5 or 6 with
# `how` its next argument, then ProcessState update by update until its state changes or its
# fade ends. Before the message: +0x218 and +0x220, +0x84, +0x85, +0x87 and +0x88 (bits 0 to 3
# of `flags`), +0x23c and +0x240; the player's id (ClientState+0xcdc), whether the emblem's file
# is there, and whether the world has a player (CWorld+0x3c) and where (float bits). Then the
# message's record, ARRIVE_RECORD words. The actor's messages to itself (vf02, through a copy of
# its vtable), the sounds (PlayWave) and the files asked for (ResourceManager::Exists) are
# recorded, and the hash takes them with +0x1b4, +0x1ec to +0x240's fields, the four flags,
# g_groundWaterFade and CGameMode+0x2a0 (both 0x5a5a5a5a before).
ARRIVE_FIELDS = ["message", "how", "body0", "health0", "flags", "guild0", "emblem0", "own", "exists",
                 "player", "px", "pz"]
ARRIVE_RECORD = 28
SERVER_NAME = "Chaos"
OWN_ID, SERVER_NAME_BUFFER, GROUND_WATER_FADE, G_MODE = 0x68734C, 0x686B58, 0x679F9C, 0x67C9EC
PLAY_WAVE, GET_RESOURCE_MANAGER, RESOURCE_EXISTS = 0x4352A0, 0x4DFA10, 0x4E0190
SELF_MESSAGE_STUB, SEND_SELF_SLOT, VTABLE_SIZE = MESSAGE_STUB + 0x10, 2, 0x100
# X and K: a G and a P whose actor is sent messages that make and end its effects
# (CGameActor::SendMsg through its vf02, docs/world/effects.md): EFFECT_FIELDS, then how many
# messages and each one's id, argument and sender (0 none, k the k-th effect made, from 1, or
# past them one the actor never had), then their server time and timeGetTime. Before them the
# slots +0x244 to +0x26c of `slots`' bits hold effects made before, each its own (one for all
# with `same`) and in the list +0x270 in that order, and `listed` more are in the list.
# CGameMode::AddSpriteEffectByType and AddEmotionEffect make effects whose vf02 is recorded.
# The hash takes, after each message, fields(), the game mode's messages, the slots and the
# list by the effects' numbers, what made each effect (2 before, 0 AddSpriteEffectByType, 1
# AddEmotionEffect), its type, whether it follows the actor's position and its id, its +0x44,
# and what each was sent and by whom (0 none, 1 the actor).
# B: an A whose actor's messages to itself go through its own vf02, EFFECT_FIELDS after
# ARRIVE_FIELDS, and the effects hashed too after arrival()'s words.
EFFECT_FIELDS = ["slots", "listed", "same"]
ADD_SPRITE_EFFECT, ADD_EMOTION_EFFECT, EFFECT_MESSAGE_STUB = 0x5106A0, 0x5111B0, MESSAGE_STUB + 0x20
EFFECT_SLOTS, EFFECT_SLOT_COUNT, EFFECT_LIST, LAUNCHED_LIST = 0x244, 11, 0x270, 0xAC
# D and H: a G and a P that die (docs/world/states.md#dying): through vf10's SetState(3) (`how`
# 0), CGameActor::SendMsg's 0xc or its 0x1c; before it +0x218, +0x20c (the size of the list at
# +0x204, its nodes unread), +0x210, whether the body has a sprite (+0x98), and +0xcc; then
# EFFECT_FIELDS, then the server's time and timeGetTime of the death and of each update, until
# the update returns 0 or the state changes. The body's CRes names are ACT_NAME and
# SPRITE_NAME (CRes::GetName, +0x14). CGameMode::AddCorpse is recorded; the hash takes, after
# the death and each update, fields(), the game mode's messages, extra(), effects(), +0x1b4,
# +0x210, +0x80, +0xcc and the corpses (their position, names, action, frame and facing).
DEATH_FIELDS = ["how", "body", "queued", "f210", "sprite", "f0cc"]
ADD_CORPSE, ACT_NAME, SPRITE_NAME = 0x510030, "poring.act", "poring.spr"
SRAND, PET_ID, PET_TABLE = 0x5C2546, 0x686E40, 0x686E94
PET_LEVEL_A, PET_LEVEL_B, PET_LINE = 0x5A9F40, 0x5A9FA0, 0x5AA3E0
SINGLE = 0x007F
SPEEDS = [150, 150, 150, 100, 200, 75, 300, 120, 1, 7, 999]


def fnv1a64(values, h=0xCBF29CE484222325):
    for v in values:
        for b in struct.pack("<I", v & 0xFFFFFFFF):
            h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def name_hash(name):
    """FNV-1a over a name's bytes, as its two halves."""
    h = 0xCBF29CE484222325
    for b in name:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return [h & 0xFFFFFFFF, h >> 32]


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
        # The actor +0x1a8 names, which CWorld::GetActorById finds or not.
        self.target, self.target_found = emu.scratch(size=0x10), False
        emu.replace_thiscall(GET_ACTOR_BY_ID, 1, lambda this, actor_id: self.target if self.target_found else 0)
        # The body's CActRes: GetActionDelay reads the delays at +0x138..+0x13c.
        self.act = emu.scratch(size=0x144)
        self.delays = emu.scratch(size=4 * 9)
        emu.write_u32(self.act + 0x138, self.delays)
        emu.write_u32(self.act + 0x13C, self.delays + 4 * 9)
        # For A: the actor's vtable with its own messages (vf02) recorded, the sounds, and the
        # emblems' files, there or not.
        self.actor_vtable = emu.scratch(emu.mem_read(GAME_ACTOR_VTABLE, VTABLE_SIZE))
        emu.mem_write(SELF_MESSAGE_STUB, b"\xC3")
        emu.write_u32(self.actor_vtable + 4 * SEND_SELF_SLOT, SELF_MESSAGE_STUB)
        self.self_messages, self.sounds, self.asked, self.emblem_exists = [], [], [], 0
        emu.replace_thiscall(SELF_MESSAGE_STUB, 5, lambda this, *v: self.self_messages.extend(v))

        def play_wave(uc, address, size, _):
            esp = emu.esp()
            name, *rest = (emu.read_u32(esp + 4 + 4 * k) for k in range(7))
            self.sounds += [*name_hash(emu.mem_read(name, len(emu.read_cstr(name)))), *rest]
            emu.complete(0, emu.read_u32(esp), esp + 4)
        emu.hook_code(PLAY_WAVE, PLAY_WAVE, play_wave)
        resources = emu.scratch(size=0x10)
        emu.replace_thiscall(GET_RESOURCE_MANAGER, 0, lambda this: resources)

        def exists(this, name):
            self.asked += name_hash(emu.mem_read(name, len(emu.read_cstr(name))))
            return self.emblem_exists
        emu.replace_thiscall(RESOURCE_EXISTS, 1, exists)
        emu.write_u32(SERVER_NAME_BUFFER, emu.cstr(SERVER_NAME))
        emu.write_u32(G_MODE, self.mode)
        self.player = emu.scratch(size=0x10)
        # For X, K and B: the effects the game mode makes, their messages (vf02) recorded.
        self.effect_vtable = emu.scratch(size=0x40)
        emu.mem_write(EFFECT_MESSAGE_STUB, b"\xC3")
        emu.write_u32(self.effect_vtable + 4 * SEND_SELF_SLOT, EFFECT_MESSAGE_STUB)
        self.made, self.made_by, self.effect_messages = [], [], []

        def effect_message(this, sender, message, a, b, c):
            self.effect_messages += [self.number(this), {0: 0, self.actor: 1}.get(sender, 2), message, a, b, c]
        emu.replace_thiscall(EFFECT_MESSAGE_STUB, 5, effect_message)
        emu.replace_thiscall(ADD_SPRITE_EFFECT, 3, lambda this, kind, at, actor_id: self.make_effect(0, kind, at, actor_id))
        emu.replace_thiscall(ADD_EMOTION_EFFECT, 3, lambda this, at, kind, actor_id: self.make_effect(1, kind, at, actor_id))
        # For D and H: the body's names, a sprite to have or not, and the corpses left.
        emu.mem_write(self.act + 0x14, ACT_NAME.encode() + b"\0")
        self.sprite = emu.scratch(size=0x40)
        emu.mem_write(self.sprite + 0x14, SPRITE_NAME.encode() + b"\0")
        self.corpses = []

        def add_corpse(this, *v):
            name = lambda a: name_hash(emu.mem_read(a, len(emu.read_cstr(a)))) if a else [0, 0]
            self.corpses += [*v[:3], *name(v[3]), *name(v[4]), *v[5:]]
        emu.replace_thiscall(ADD_CORPSE, 8, add_corpse)

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
            v = [int(x) for x in values] if kind not in ("W", "G", "P", "T", "U", "N", "Y", "E", "Q", "A", "B", "X",
                                                         "K", "D", "H") else []
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
            elif kind == "E":
                out.append(self.go(values, enter=True))
            elif kind == "Q":
                out.append(self.go(values, player=True, enter=True))
            elif kind == "A":
                out.append(self.go(values, arrive=True))
            elif kind == "B":
                out.append(self.go(values, arrive=True, effects=True))
            elif kind == "X":
                out.append(self.go(values, effects=True))
            elif kind == "K":
                out.append(self.go(values, player=True, effects=True))
            elif kind == "D":
                out.append(self.go(values, die=True, effects=True))
            elif kind == "H":
                out.append(self.go(values, player=True, die=True, effects=True))
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

    def go(self, values, player=False, to=False, stand=False, enter=False, arrive=False, effects=False,
           die=False):
        """CGameActor::vf02's message 0 on the actor, then ProcessState update by update, with
        SetState run, until the actor stands again or has faded; for a player, a CPc's."""
        emu, actor = self.emu, self.actor
        v = [int(x, 0) for x in values]
        names = ((PLAYER_FIELDS if player else GO_FIELDS) + (TO_SPEED if to else []) + (STAND_FIELDS if stand else [])
                 + (ENTER_FIELDS if enter else []) + (ARRIVE_FIELDS if arrive else []) + (DEATH_FIELDS if die else [])
                 + (EFFECT_FIELDS if effects else []))
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
        if enter:
            for offset, name in ((0xCC, "f0cc"), (0x1A8, "f1a8"), (0x54, "play"), (0x70, "start")):
                emu.write_u32(actor + offset, g[name] & 0xFFFFFFFF)
            emu.write_u32(actor + 0x1D4, 0)
            emu.write_u32(actor + 0x1F4, 0)
            emu.write_u32(self.target + 4, g["tx"])
            emu.write_u32(self.target + 0xC, g["tz"])
            self.target_found = g["found"] != 0
            return self.enter(g, updates, extra)
        if effects:
            self.ready_effects(g)
        if die:
            return self.die(g, updates, extra)
        if arrive:
            return self.arrive(g, updates[:ARRIVE_RECORD], updates[ARRIVE_RECORD:], effects)
        if effects:
            return self.send(updates, extra)
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

    def enter(self, g, updates, extra):
        """The state through the actor's vf10, then ProcessState update by update, until the state
        changes or the fade ends."""
        emu, actor = self.emu, self.actor
        self.messages, self.states = [], []
        self.server, self.now = updates[0], updates[1]
        if g["set"]:
            emu.call(emu.read_u32(emu.read_u32(actor) + 4 * 10), g["enter"] & 0xFFFFFFFF, this=actor)
        h = fnv1a64([3, *self.fields(), *self.messages, *extra(), emu.read_u32(actor + 0xCC)])
        state = emu.read_u32(actor + 0x58)
        last, calls = "staying", 0
        for server, tick in zip(updates[2::2], updates[3::2]):
            self.messages, self.states = [], []
            self.now, self.server = tick, server
            ret = emu.call(emu.read_u32(emu.read_u32(actor) + 4 * 9), this=actor).eax & 0xFF
            calls += 1
            h = fnv1a64([ret, *self.fields(), *self.messages, *extra(), emu.read_u32(actor + 0xCC)], h)
            if ret == 0:
                last = "faded"
                break
            if emu.read_u32(actor + 0x58) != state:
                last = "changed"
                break
        r = [emu.read_u32(actor + o) for o in (0x4, 0x8, 0xC, 0x38, 0x24, 0x28, 0x58)]
        return f"{last} {calls} {r[0]:08x} {r[1]:08x} {r[2]:08x} {r[3]:08x} {r[4]} {r[5]} {r[6]} {h:016x}"

    def arrive(self, g, record, updates, effects=False):
        """The message, then ProcessState update by update, until the state changes or the fade
        ends. With effects the actor's messages to itself go through its own vf02."""
        emu, actor = self.emu, self.actor
        emu.write_u32(actor, GAME_ACTOR_VTABLE if effects else self.actor_vtable)
        made = self.effects if effects else lambda: []
        for offset, name in ((0x218, "body0"), (0x220, "health0"), (0x23C, "guild0"), (0x240, "emblem0")):
            emu.write_u32(actor + offset, g[name] & 0xFFFFFFFF)
        for k, offset in enumerate((0x84, 0x85, 0x87, 0x88)):
            emu.mem_write(actor + offset, bytes([g["flags"] >> k & 1]))
        for offset in (0x1D4, 0x1EC, 0x1F0, 0x1F4, 0x224):
            emu.write_u32(actor + offset, 0)
        emu.write_u32(OWN_ID, g["own"] & 0xFFFFFFFF)
        self.emblem_exists = g["exists"]
        emu.write_u32(self.player + 4, g["px"])
        emu.write_u32(self.player + 0xC, g["pz"])
        emu.write_u32(self.world + 0x3C, self.player if g["player"] else 0)
        emu.write_u32(GROUND_WATER_FADE, 0x5A5A5A5A)
        emu.write_u32(self.mode + MODE_FIELD_2A0, 0x5A5A5A5A)
        message = emu.scratch(struct.pack(f"<{ARRIVE_RECORD}I", *(r & 0xFFFFFFFF for r in record)))
        self.messages, self.states, self.self_messages, self.sounds, self.asked = [], [], [], [], []
        self.server, self.now = updates[0], updates[1]
        emu.call(MESSAGE, 0, g["message"], message, g["how"] & 0xFFFFFFFF, 0, this=actor)
        count, digest = self.path()[3:5]
        h = fnv1a64([4, *self.fields(), count, digest, digest >> 32, *self.messages, *self.arrival(), *made()])
        state = emu.read_u32(actor + 0x58)
        last, calls = "staying", 0
        for server, tick in zip(updates[2::2], updates[3::2]):
            self.messages, self.states = [], []
            self.now, self.server = tick, server
            ret = emu.call(PROCESS, this=actor).eax & 0xFF
            calls += 1
            h = fnv1a64([ret, *self.fields(), *self.messages, *self.arrival(), *made()], h)
            if ret == 0:
                last = "faded"
                break
            if emu.read_u32(actor + 0x58) != state:
                last = "changed"
                break
        emu.write_u32(self.world + 0x3C, 0)
        r = [emu.read_u32(actor + o) for o in (0x4, 0x8, 0xC, 0x38, 0x24, 0x28, 0x58)]
        return f"{last} {calls} {r[0]:08x} {r[1]:08x} {r[2]:08x} {r[3]:08x} {r[4]} {r[5]} {r[6]} {h:016x}"

    def send(self, rest, extra):
        """The messages through the actor's vf02, one by one."""
        emu, actor = self.emu, self.actor
        count = rest[0]
        script = rest[1:1 + 3 * count]
        self.server, self.now = rest[1 + 3 * count], rest[2 + 3 * count]
        stranger = emu.scratch(size=0x10)
        h = 0xCBF29CE484222325
        for message, arg, sender in zip(script[::3], script[1::3], script[2::3]):
            self.messages, self.states = [], []
            sender = self.made[sender - 1] if 0 < sender <= len(self.made) else stranger if sender else 0
            emu.call(emu.read_u32(emu.read_u32(actor) + 4 * 2), sender, message & 0xFFFFFFFF, arg & 0xFFFFFFFF, 0,
                     0, this=actor)
            h = fnv1a64([message, *self.fields(), *self.messages, *extra(), *self.effects()], h)
        r = [emu.read_u32(actor + o) for o in (0x4, 0x8, 0xC, 0x38, 0x24, 0x28, 0x58)]
        return f"sent {count} {r[0]:08x} {r[1]:08x} {r[2]:08x} {r[3]:08x} {r[4]} {r[5]} {r[6]} {h:016x}"

    def die(self, g, updates, extra):
        """The death, then ProcessState update by update until it returns 0 or the state changes."""
        emu, actor = self.emu, self.actor
        emu.write_u32(actor + 0x218, g["body"] & 0xFFFFFFFF)
        emu.write_u32(actor + 0x20C, g["queued"])
        emu.write_u32(actor + 0x210, g["f210"])
        emu.write_u32(actor + 0x98, self.sprite if g["sprite"] else 0)
        emu.mem_write(actor + 0x80, b"\0")
        emu.write_u32(actor + 0xCC, g["f0cc"] & 0xFFFFFFFF)
        for offset in (0x1A8, 0x1D4, 0x1F4):
            emu.write_u32(actor + offset, 0)
        self.target_found = False
        self.messages, self.states, self.corpses = [], [], []
        self.server, self.now = updates[0], updates[1]
        if g["how"] == 0:
            emu.call(emu.read_u32(emu.read_u32(actor) + 4 * 10), 3, this=actor)
        else:
            emu.call(emu.read_u32(emu.read_u32(actor) + 4 * 2), 0, g["how"], 0, 0, 0, this=actor)

        def died():
            words = [emu.read_u32(actor + 0x1B4), emu.read_u32(actor + 0x210), emu.mem_read(actor + 0x80, 1)[0],
                     emu.read_u32(actor + 0xCC), len(self.corpses), *self.corpses]
            self.corpses = []
            return words
        h = fnv1a64([5, *self.fields(), *self.messages, *extra(), *self.effects(), *died()])
        state = emu.read_u32(actor + 0x58)
        last, calls = "staying", 0
        for server, tick in zip(updates[2::2], updates[3::2]):
            self.messages, self.states = [], []
            self.now, self.server = tick, server
            ret = emu.call(emu.read_u32(emu.read_u32(actor) + 4 * 9), this=actor).eax & 0xFF
            calls += 1
            corpse = bool(self.corpses)
            h = fnv1a64([ret, *self.fields(), *self.messages, *extra(), *self.effects(), *died()], h)
            if ret == 0:
                last = "gone" if corpse else "faded"
                break
            if emu.read_u32(actor + 0x58) != state:
                last = "changed"
                break
        r = [emu.read_u32(actor + o) for o in (0x4, 0x8, 0xC, 0x38, 0x24, 0x28, 0x58)]
        for offset in (0x98, 0x20C, 0x210):
            emu.write_u32(actor + offset, 0)
        emu.mem_write(actor + 0x80, b"\0")
        return f"{last} {calls} {r[0]:08x} {r[1]:08x} {r[2]:08x} {r[3]:08x} {r[4]} {r[5]} {r[6]} {h:016x}"

    def make_effect(self, by, kind, at, actor_id):
        """An effect as the game mode makes one: its vtable, the rest 0."""
        effect = self.emu.scratch(size=0x120)
        self.emu.write_u32(effect, self.effect_vtable)
        self.made.append(effect)
        self.made_by.append([by, kind, int(at == self.actor + 4), actor_id])
        return effect

    def number(self, effect):
        if effect in self.made:
            return self.made.index(effect) + 1
        return 0 if effect == 0 else 0xFFFFFFFF

    def empty_list(self, at):
        """A std::list at `at` with no nodes: its head links to itself."""
        head = self.emu.scratch(size=12)
        self.emu.write_u32(head, head)
        self.emu.write_u32(head + 4, head)
        self.emu.write_u32(at + 4, head)
        self.emu.write_u32(at + 8, 0)

    def link(self, effect):
        """The effect at the end of the actor's list, as std::list::push_back."""
        emu, at = self.emu, self.actor + EFFECT_LIST
        head = emu.read_u32(at + 4)
        last = emu.read_u32(head + 4)
        node = emu.scratch(struct.pack("<3I", head, last, effect))
        emu.write_u32(last, node)
        emu.write_u32(head + 4, node)
        emu.write_u32(at + 8, emu.read_u32(at + 8) + 1)

    def ready_effects(self, g):
        """The effects of X, K and B before the messages, and the launched effects' list empty."""
        emu, actor = self.emu, self.actor
        self.made, self.made_by, self.effect_messages = [], [], []
        self.empty_list(actor + EFFECT_LIST)
        self.empty_list(actor + LAUNCHED_LIST)
        shared = 0
        for k in range(EFFECT_SLOT_COUNT):
            effect = 0
            if g["slots"] >> k & 1:
                effect = shared if g["same"] and shared else self.make_effect(2, 0, 0, 0)
                if effect != shared:
                    self.link(effect)
                shared = effect
            emu.write_u32(actor + EFFECT_SLOTS + 4 * k, effect)
        for _ in range(g["listed"]):
            self.link(self.make_effect(2, 0, 0, 0))

    def effects(self):
        """The slots and the list by number, the effects made, and what they were sent since."""
        emu, actor = self.emu, self.actor
        words = [self.number(emu.read_u32(actor + EFFECT_SLOTS + 4 * k)) for k in range(EFFECT_SLOT_COUNT)]
        head = emu.read_u32(actor + EFFECT_LIST + 4)
        listed, node = [], emu.read_u32(head)
        while node != head:
            listed.append(self.number(emu.read_u32(node + 8)))
            node = emu.read_u32(node)
        words += [emu.read_u32(actor + EFFECT_LIST + 8), len(listed), *listed, len(self.made)]
        for effect, by in zip(self.made, self.made_by):
            words += [*by, emu.read_u32(effect + 0x44)]
        words += [len(self.effect_messages), *self.effect_messages]
        self.effect_messages = []
        return words

    def arrival(self):
        """What arriving writes beyond fields(), and what it sent, played and asked for since."""
        emu, a = self.emu, self.actor
        words = [emu.read_u32(a + o) for o in (0x1B4, 0x1EC, 0x1F0, 0x1F4, 0x218, 0x21C, 0x220, 0x224, 0x23C,
                                               0x240, 0xC8)]
        words += [emu.mem_read(a + o, 1)[0] for o in (0x84, 0x85, 0x87, 0x88)]
        words += [emu.read_u32(GROUND_WATER_FADE), emu.read_u32(self.mode + MODE_FIELD_2A0)]
        words += [len(self.self_messages), *self.self_messages, len(self.sounds), *self.sounds,
                  len(self.asked), *self.asked]
        self.self_messages, self.sounds, self.asked = [], [], []
        return words

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
