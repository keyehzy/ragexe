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
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDI,  # noqa: E402
                                UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_FPCW)

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
# V and J: a G and a P that act on a target (docs/world/attack.md): by `how`, vf10's
# SetState(`enter`) (0), CGameActor::SendMsg's 0xb with the record (ProcessActionOnTarget), or
# its 0x1b with the record as a hit and `enter` its total. Before it +0xcc, +0x1a8, whether
# CWorld::GetActorById finds an actor (1 another at tx, tz; 2 the actor itself), the player's
# id, the world's player (1 another at px, pz; 2 the actor itself), +0x1bc, +0x2a0, the act's
# frames' events (3 bits a frame: 1 to 4 the events "atk", "atk2", "at" and "", 5 one past
# them, 6 0x7fffffff, the rest none), +0x54, +0x70 and how many hits each of the three has
# queued (+0x204). Then the record, ATTACK_RECORD words, and the server's time and
# timeGetTime of the message and of each update, until the state changes or the fade ends.
# The other two are CGameActors with only their position, their effects launched and their
# queue. CAbleToMakeEffect::LaunchEffect and CGameMode::AddArrowEffect are recorded, the world
# has no skills (GetSkillById), and the hash takes, after the message and each update,
# fields(), the game mode's messages, extra() and attacked(): +0x1a8, +0xcc, the messages
# logged, the effects launched, the arrows shot and the three queues.
ATTACK_FIELDS = ["how", "enter", "f0cc", "f1a8", "found", "tx", "tz", "own", "player", "px", "pz", "f1bc",
                 "weapon", "events", "play", "start", "queued"]
ATTACK_RECORD = 10
GET_SKILL_BY_ID, LAUNCH_EFFECT, ADD_ARROW = 0x4C4DB0, 0x501E10, 0x5101F0
GET_FRAME, GET_EVENT_NAME, HIT_QUEUE = 0x421A60, 0x420F20, 0x204
EVENT_NAMES = ["atk", "atk2", "at", ""]
# L and O: a G and a P that hits land on (docs/world/hits.md): by `how`, CGameActor 00503f80
# delivering what is queued (0), or the message `how` sent with the first hit through vf02;
# then, update by update, 00503f80 and ProcessState, until the update returns 0. Before it: the
# attacker (0 none; 1 another, which CWorld::GetActorById finds; 2 the player, by its id; 3 the
# actor itself, found; 4 the actor itself as the player), whether the other and the player are
# CPcs (bits 0 and 1 of `pcs`), their job, +0x21c and weapon (+0x2a0); the player's id, its
# statuses (00641f98: 0 none, 1 [0x13], 2 [5], 3 [5, 0x13]), the guild war (CWorld+0x60),
# effects on (00687ea8) and the msg sprites (00687ec0), srand's seed, +0x210, `listed` effects
# in the list before (each a number whose +0x10c is 0x16 by `kinds`' bits, the first in the
# slot `slot` below 11), the act's events as V's, +0x54, +0x70, +0x1a8, the actor's weapon,
# +0x1bc, where the other and the player stand (float bits) and how many hits (each
# ATTACK_RECORD words after the fields, queued unless `how` sends the first). The world always
# has its player. CMsgEffect::CMsgEffect, AddMsgSpriteEffect and CAbleToMakeEffect::LaunchEffect's
# effect are stand-ins whose vf02 is recorded (message 0x16 keeping its second argument at
# +0x10c, as CMsgEffect's does); CGameMode::AddToWorldObjects is skipped and
# CWorld::DeleteGameObject recorded. The hash takes, after the message and each update,
# fields(), the game mode's messages, extra() and landed().
LAND_FIELDS = ["how", "attacker", "pcs", "ajob", "aeffect", "aweapon", "own", "statuses", "war", "effects_on",
               "msgs", "seed", "f210", "listed", "kinds", "slot", "events", "play", "start", "f1a8", "weapon",
               "f1bc", "tx", "tz", "px", "pz", "count"]
DELIVER, MSG_EFFECT_CTOR, ADD_TO_WORLD, DELETE_OBJECT = 0x503F80, 0x5AD910, 0x511150, 0x4C4BC0
ADD_MSG_SPRITE, LAUNCH_MAKES, LAUNCH_RETURNS = 0x510440, 0x501E93, 0x501E80
CLIENT_STATE, PLAYER_HIT_SOUNDS, WEAPON_HIT_SOUNDS = 0x686670, 0x599620, 0x599A40
OWN_STATUSES, EFFECTS_ON, MSG_SPRITES, WORLD_GUILD_WAR = 0x641F98, 0x687EA8, 0x687EC0, 0x60
SRAND, PET_ID, PET_TABLE = 0x5C2546, 0x686E40, 0x686E94
PET_LEVEL_A, PET_LEVEL_B, PET_LINE = 0x5A9F40, 0x5A9FA0, 0x5AA3E0
# C and R: a G and a P casting (docs/world/windows.md): by `how`, CGameActor::SendMsg's message
# 0x52 with `duration`, 0x53, 0x21 or 0x36 through vf02, or none; then vf10's SetState(`enter`)
# unless it is 0; then, update by update, CGameActor 00503210 and ProcessState, until the update
# returns 0 or, out of state 13, no window is left. Before it: +0xcc, the windows the actor has
# (bits 0 to 3 of `windows`: +0x1c0 the balloon, of size bw by bh, +0x1c4, +0x1c8 and +0x1cc),
# +0x1e0, +0x1e4, +0x1a4, +0x1d8, +0x1dc, the point on screen (+0x90, +0x94), +0x48 (float
# bits), the screen's size (g_pSceneRenderer +0x24, +0x28), +0x1bc, the weapon (+0x2a0), +0x54
# and +0x70. The windows are stand-ins: UIRechargeGage's constructor, UIWindow 004a58e0,
# UIWindowMgr 004a7d60, UIRechargeGage::SetAmount and their vf18 are recorded, by the windows'
# numbers, those before from 1 in order, then those made. The statics 0067cf18 and 0067cf10 are
# worked out again for each query. The hash takes, after the message and after each of the two
# calls an update, fields(), the game mode's messages and cast().
CAST_FIELDS = ["how", "duration", "enter", "f0cc", "windows", "bw", "bh", "f1e0", "f1e4", "spoke", "end",
               "begin", "screen_x", "screen_y", "pixels", "width", "height", "f1bc", "weapon", "play", "start"]
STEP_WINDOWS, GAUGE_CTOR, CREATE_WINDOW, ADD_WINDOW, SET_AMOUNT = 0x503210, 0x442D00, 0x4A58E0, 0x4A7D60, 0x442E40
SCENE_RENDERER, WINDOW_STATICS, WINDOW_OFFSETS = 0x625E98, (0x67CF14, 0x67CF1C), (0x1C0, 0x1C4, 0x1C8, 0x1CC)
# u and p: a G and a P updated whole (docs/world/update.md): CNpc::Update (005404d0) on the actor,
# whose job picks its way, and CPc::Update (00542e40) on the player, update by update until it
# returns 0. Before it: +0x230, +0x83, +0x6c, +0x68, +0x87, +0x1ec, +0x290, +0x1f4, +0x218,
# effects on (00687ea8), the act's frames' events as V's but named UPDATE_EVENT_NAMES, +0x2c,
# +0x30, +0x28 (an action from ACT_ACTIONS on the act lacks, its frames empty), where the world's
# player stands (float bits), whether a CPc has its gauge
# (+0x2dc), the point on screen, +0x48 (float bits), the screen's height (g_pSceneRenderer +0x28,
# its width 640), whether the actor has a cast's gauge (+0x1cc) and the cast's times, +0xcc,
# +0x1bc, the weapon (+0x2a0), +0x54, +0x70 and +0x24. No hits are queued. LaunchEffect's effects are
# L's stand-ins, the windows C's. The hash takes, after each update, its result, fields(), the
# game mode's messages and updated().
UPDATE_FIELDS = ["f230", "keyed", "keyed_at", "key", "tinted", "f1ec", "f290", "f1f4", "body", "effects_on",
                 "events", "f2c", "f30", "frame", "px", "pz", "gauge", "screen_x", "screen_y", "pixels", "height",
                 "cast", "end", "begin", "f0cc", "f1bc", "weapon", "play", "start", "action"]
UPDATE_EVENT_NAMES = ["foot.wav", "a.wav.b", "a.wa", "X.WAV"]
NPC_UPDATE, GAUGE_OFFSET, ACT_ACTIONS = 0x5404D0, 0x2DC, 256
# o: a P updated whole as the own player (docs/world/pursuit.md): CPlayer::Update (0054b770) on a
# CPlayer, update by update until it returns 0, the messages to itself CPlayer::SendMsg (0054c5d0)
# handles recorded at its switch instead (the rest go on to CPc::SendMsg), a move's (0) with its
# order's seven words. After p's: +0x308, +0x30c,
# +0x310, +0x314, +0x318, +0x31c, +0x320, +0x324, +0x32c, +0x330, +0x334, +0x338, +0x344,
# +0x348, +0x350, +0x354, +0x358, +0x35c; the ping (ClientState+0x524) and the attack range
# (+0x640); the player's id; which of CWorld::GetActorById, GetItemById and GetSkillById find
# the one stand-in pursued (bits 0 to 2 of `found`; GetActorById's finds it for +0x1a8 too);
# where it stands (float bits), its state, cell (+0xc0, +0xc4), +0x238, job and +0x21c; and
# CGameMode+0x198's list before (0 empty, 1 with +0x330's id, 2 with another). The hash takes,
# after each update, p's and own().
OWN_FIELDS = ["goal_x", "goal_y", "acted_at", "premove_at", "pursuit", "premove", "attack_mode", "acted", "queued",
              "target", "acts", "asked_at", "skill", "range", "cell_x", "cell_y", "queued_x", "queued_y", "ping",
              "attack_range", "own", "found", "tx", "ty", "tz", "tstate", "tcx", "tcy", "tmodel", "tjob", "teffect",
              "listed"]
OWN_OFFSETS = (0x308, 0x30C, 0x310, 0x314, 0x318, 0x31C, 0x320, 0x324, 0x32C, 0x330, 0x334, 0x338, 0x344, 0x348,
               0x350, 0x354, 0x358, 0x35C)
PLAYER_VTABLE, OWN_SWITCH, OWN_SWITCH_INDICES, OWN_SWITCH_LAST, OWN_SWITCH_DEFAULT = 0x5E9CF4, 0x54C5FB, 0x54CF18, 0x67, 20
GET_ITEM_BY_ID, PING, ATTACK_RANGE, PICKED_LIST = 0x4C4D20, 0x686B94, 0x686CB0, 0x198
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
        self.updating = False
        emu.replace_thiscall(GET_FRAME_COUNT, 1, lambda this, action: 1 if self.updating and action >= ACT_ACTIONS
                             else self.frames if self.frames > 1 else 1)
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
        # For o: the messages CPlayer::SendMsg handles itself are recorded at its switch and it
        # returns, a move's (0) with its order; the rest go on to CPc::SendMsg. What CWorld finds
        # of items and skills' objects.
        self.own_messages, self.own_moves, self.pursued, self.found = [], [], emu.scratch(size=0x400), 0
        own_cases = {m for m in range(OWN_SWITCH_LAST + 1)
                     if emu.mem_read(OWN_SWITCH_INDICES + m, 1)[0] != OWN_SWITCH_DEFAULT}

        def own_switch(uc, address, size, _):
            message = uc.reg_read(UC_X86_REG_EAX)
            if message not in own_cases:
                return
            ebp = uc.reg_read(UC_X86_REG_EBP)
            a, b, c = (emu.read_u32(ebp + 0x10 + 4 * k) for k in range(3))
            if message == 0:
                self.own_messages += [0, 0, 0, 0]
                self.own_moves += struct.unpack("<7I", emu.mem_read(a, 28))
            else:
                self.own_messages += [message, a, b, c]
            for reg, offset in ((UC_X86_REG_EBX, -0x14), (UC_X86_REG_ESI, -0x18), (UC_X86_REG_EDI, -0x1C)):
                uc.reg_write(reg, emu.read_u32(ebp + offset))
            uc.reg_write(UC_X86_REG_EBP, emu.read_u32(ebp))
            emu.complete(0, emu.read_u32(ebp + 4), ebp + 8 + 0x14)
        emu.hook_code(OWN_SWITCH, OWN_SWITCH, own_switch)
        emu.replace_thiscall(GET_ITEM_BY_ID, 1, lambda this, v: self.pursued if self.found & 2 else 0)

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
        self.made, self.made_by, self.made_at, self.effect_messages = [], [], [], []

        def effect_message(this, sender, message, a, b, c):
            if self.landing:
                if message == 0x16:
                    emu.write_u32(this + 0x10C, b)
                self.effect_messages += [self.number(this), self.who(sender), message, self.where(a), b, c]
                return
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
        # For V and J: the other actor and the player, the effects launched, the arrows shot
        # and the act's frames' events, only while attacking.
        self.foe, self.pc = emu.scratch(size=ACTOR_SIZE), emu.scratch(size=ACTOR_SIZE)
        self.attacking, self.events, self.launched, self.arrows = False, 0, [], []
        emu.replace_thiscall(GET_SKILL_BY_ID, 1, lambda this, skill_id: self.pursued if self.found & 4 else 0)

        def add_arrow(this, at, target, speed):
            if target:
                self.arrows += [int(at == self.actor + 4), target, speed]
        emu.replace_thiscall(ADD_ARROW, 3, add_arrow)

        def launch(uc, address, size, _):
            if self.landing:
                self.launched.append(emu.read_u32(emu.esp() + 4))
            elif self.attacking:
                esp = emu.esp()
                self.launched.append(emu.read_u32(esp + 4))
                emu.complete(0, emu.read_u32(esp), esp + 4 + 0x14)
        emu.hook_code(LAUNCH_EFFECT, LAUNCH_EFFECT, launch)

        # For L and O: where LaunchEffect would make its effect, a stand-in, returned as it
        # returns.
        def launch_makes(uc, address, size, _):
            if self.landing:
                effect = emu.scratch(size=0x120)
                emu.write_u32(effect, self.launched_vtable)
                self.launched_effects[effect] = len(self.launched)
                uc.reg_write(UC_X86_REG_EAX, effect)
                uc.reg_write(UC_X86_REG_EIP, LAUNCH_RETURNS)
        emu.hook_code(LAUNCH_MAKES, LAUNCH_MAKES, launch_makes)
        self.landing, self.launched_effects, self.launched_messages = False, {}, []
        self.launched_vtable = emu.scratch(size=0x40)
        launched_stub = MESSAGE_STUB + 0x30
        emu.mem_write(launched_stub, b"\xC3")
        emu.write_u32(self.launched_vtable + 4 * SEND_SELF_SLOT, launched_stub)

        def launched_message(this, sender, message, a, b, c):
            self.launched_messages += [self.launched_effects.get(this, 0xFFFFFFFF), self.who(sender), message,
                                       self.where(a), b, c]
        emu.replace_thiscall(launched_stub, 5, launched_message)

        def conditional(address, nargs, fn):
            def hook(uc, _address, _size, _):
                if self.landing:
                    esp = emu.esp()
                    args = [emu.read_u32(esp + 4 + 4 * k) for k in range(nargs)]
                    emu.complete(fn(uc.reg_read(UC_X86_REG_ECX), *args), emu.read_u32(esp), esp + 4 + 4 * nargs)
            emu.hook_code(address, address, hook)

        def msg_effect(this):
            emu.mem_write(this, bytes(0x11C))
            emu.write_u32(this, self.effect_vtable)
            self.made.append(this)
            self.made_by.append([3, 0, 0, 0])
            self.made_at.append(0)
            return this
        conditional(MSG_EFFECT_CTOR, 0, msg_effect)
        conditional(ADD_TO_WORLD, 1, lambda this, effect: 0)
        self.deleted = []
        conditional(DELETE_OBJECT, 1, lambda this, effect: self.deleted.append(self.number(effect)))

        def msg_sprite(this, at, value, actor_id):
            effect = self.make_effect(4, 0, at, actor_id)
            emu.write_u32(effect + 0x7C, value)
            return effect
        conditional(ADD_MSG_SPRITE, 3, msg_sprite)
        # For C and R: the windows, stand-ins whose calls are recorded by number.
        self.casting, self.window_numbers, self.window_calls = False, {}, []
        self.window_vtable = emu.scratch(size=0x40)
        move_stub = MESSAGE_STUB + 0x40
        emu.mem_write(move_stub, b"\xC3")
        emu.write_u32(self.window_vtable + 0x18, move_stub)
        self.renderer = emu.scratch(size=0x40)

        def casting_only(address, nargs, fn):
            def hook(uc, _address, _size, _):
                if self.casting:
                    esp = emu.esp()
                    args = [emu.read_u32(esp + 4 + 4 * k) for k in range(nargs)]
                    emu.complete(fn(uc.reg_read(UC_X86_REG_ECX), *args), emu.read_u32(esp), esp + 4 + 4 * nargs)
            emu.hook_code(address, address, hook)

        def call(kind, window, a=0, b=0):
            self.window_calls += [kind, self.window_number(window), a, b]

        def gauge(this):
            self.new_window(this)
            return this

        def create(this, width, height):
            emu.write_u32(this + 0x1C, width)
            emu.write_u32(this + 0x20, height)
            call(0, this, width, height)
        casting_only(GAUGE_CTOR, 0, gauge)
        casting_only(CREATE_WINDOW, 2, create)
        casting_only(move_stub, 2, lambda this, x, y: call(1, this, x, y))
        casting_only(ADD_WINDOW, 1, lambda this, window: call(2, window))
        casting_only(SET_AMOUNT, 2, lambda this, done, whole: call(3, this, done, whole))
        # The C runtime's thread data, which srand and rand keep their state in, made on first
        # use: made here, below every run's heap, which each run releases.
        emu.call(SRAND, 1)
        # ClientState's tables of the sounds of hits (+0x16d4 and +0x16e4), as 00588a00 fills them.
        emu.call(PLAYER_HIT_SOUNDS, this=CLIENT_STATE)
        emu.call(WEAPON_HIT_SOUNDS, this=CLIENT_STATE)
        self.statuses = emu.scratch(size=8)
        self.frame_records = emu.scratch(size=0x40 * 64)
        self.event_names = self.attack_names = [emu.cstr(name) for name in EVENT_NAMES]
        self.update_names = [emu.cstr(name) for name in UPDATE_EVENT_NAMES]
        no_name = emu.cstr("")

        def get_frame(uc, address, size, _):
            if self.attacking:
                esp = emu.esp()
                frame = emu.read_u32(esp + 8)
                past = frame >= self.frames or (self.updating and emu.read_u32(esp + 4) >= ACT_ACTIONS)
                record = self.frame_records + 0x40 * min(frame, 63)
                code = self.events >> 3 * frame & 7 if frame < min(self.frames, 10) else 0
                event = {0: 0xFFFFFFFF, 5: len(EVENT_NAMES), 6: 0x7FFFFFFF, 7: 0xFFFFFFFF}.get(code, code - 1)
                # CActRes::GetFrame's empty frame past the action's (g_ActFrame) has event 0.
                emu.write_u32(record + 0x34, 0 if past else event)
                emu.complete(record, emu.read_u32(esp), esp + 12)
        emu.hook_code(GET_FRAME, GET_FRAME, get_frame)

        def get_event_name(uc, address, size, _):
            if self.attacking:
                esp = emu.esp()
                event = emu.read_u32(esp + 4)
                emu.complete(self.event_names[event] if event < len(self.event_names) else no_name,
                             emu.read_u32(esp), esp + 8)
        emu.hook_code(GET_EVENT_NAME, GET_EVENT_NAME, get_event_name)

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
                                                         "K", "D", "H", "V", "J", "L", "O", "C", "R", "u", "p",
                                                         "o") else []
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
            elif kind == "V":
                out.append(self.go(values, attack=True))
            elif kind == "J":
                out.append(self.go(values, player=True, attack=True))
            elif kind == "L":
                out.append(self.go(values, land=True))
            elif kind == "O":
                out.append(self.go(values, player=True, land=True))
            elif kind == "C":
                out.append(self.go(values, cast=True))
            elif kind == "R":
                out.append(self.go(values, player=True, cast=True))
            elif kind == "u":
                out.append(self.go(values, update=True))
            elif kind == "p":
                out.append(self.go(values, player=True, update=True))
            elif kind == "o":
                out.append(self.go(values, player=True, update=True, own=True))
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
           die=False, attack=False, land=False, cast=False, update=False, own=False):
        """CGameActor::vf02's message 0 on the actor, then ProcessState update by update, with
        SetState run, until the actor stands again or has faded; for a player, a CPc's."""
        emu, actor = self.emu, self.actor
        v = [int(x, 0) for x in values]
        names = ((PLAYER_FIELDS if player else GO_FIELDS) + (TO_SPEED if to else []) + (STAND_FIELDS if stand else [])
                 + (ENTER_FIELDS if enter else []) + (ARRIVE_FIELDS if arrive else []) + (DEATH_FIELDS if die else [])
                 + (EFFECT_FIELDS if effects else []) + (ATTACK_FIELDS if attack else [])
                 + (LAND_FIELDS if land else []) + (CAST_FIELDS if cast else []) + (UPDATE_FIELDS if update else [])
                 + (OWN_FIELDS if own else []))
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
        if attack:
            return self.attack(g, updates[:ATTACK_RECORD], updates[ATTACK_RECORD:], extra, player)
        if cast:
            return self.cast(g, updates, player)
        if update:
            return self.update(g, updates, player, own)
        if land:
            n = ATTACK_RECORD * g["count"]
            return self.land(g, updates[:n], updates[n:], extra, player)
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

    def attack(self, g, record, updates, extra, player):
        """The message, then ProcessState update by update, until the state changes or the fade
        ends."""
        emu, actor, foe, pc = self.emu, self.actor, self.foe, self.pc
        for offset, name in ((0xCC, "f0cc"), (0x1A8, "f1a8"), (0x54, "play"), (0x70, "start"), (0x1BC, "f1bc"),
                             (0x2A0, "weapon")):
            emu.write_u32(actor + offset, g[name] & 0xFFFFFFFF)
        for offset in (0x1D4, 0x1F4, 0xB8, 0xBC) + (() if player else (0x1CC, 0x218)):
            emu.write_u32(actor + offset, 0)
        self.empty_list(actor + LAUNCHED_LIST)
        self.empty_list(actor + HIT_QUEUE)
        for other, x, z in ((foe, g["tx"], g["tz"]), (pc, g["px"], g["pz"])):
            emu.mem_write(other, bytes(ACTOR_SIZE))
            emu.write_u32(other, GAME_ACTOR_VTABLE)
            emu.write_u32(other + 4, x)
            emu.write_u32(other + 0xC, z)
            self.empty_list(other + LAUNCHED_LIST)
            self.empty_list(other + HIT_QUEUE)
        for at in (actor, foe, pc):
            for k in range(g["queued"]):
                self.push(at + HIT_QUEUE, [(k + 1) << 16 | j for j in range(ATTACK_RECORD)])
        emu.write_u32(OWN_ID, g["own"] & 0xFFFFFFFF)
        emu.write_u32(self.world + 0x3C, {1: pc, 2: actor}.get(g["player"], 0))
        saved = self.target
        self.target, self.target_found = (actor if g["found"] == 2 else foe), g["found"] != 0
        self.events, self.launched, self.arrows, self.attacking = g["events"], [], [], True
        message = emu.scratch(struct.pack(f"<{ATTACK_RECORD}I", *(r & 0xFFFFFFFF for r in record)))
        self.messages, self.states = [], []
        self.server, self.now = updates[0], updates[1]
        if g["how"] == 0:
            emu.call(emu.read_u32(emu.read_u32(actor) + 4 * 10), g["enter"] & 0xFFFFFFFF, this=actor)
        else:
            emu.call(emu.read_u32(emu.read_u32(actor) + 4 * 2), 0, g["how"], message,
                     g["enter"] & 0xFFFFFFFF if g["how"] == 0x1B else 0, 0, this=actor)
        h = fnv1a64([6, *self.fields(), *self.messages, *extra(), *self.attacked()])
        state = emu.read_u32(actor + 0x58)
        last, calls = "staying", 0
        for server, tick in zip(updates[2::2], updates[3::2]):
            self.messages, self.states = [], []
            self.now, self.server = tick, server
            ret = emu.call(emu.read_u32(emu.read_u32(actor) + 4 * 9), this=actor).eax & 0xFF
            calls += 1
            h = fnv1a64([ret, *self.fields(), *self.messages, *extra(), *self.attacked()], h)
            if ret == 0:
                last = "faded"
                break
            if emu.read_u32(actor + 0x58) != state:
                last = "changed"
                break
        self.attacking, self.target, self.target_found = False, saved, False
        emu.write_u32(self.world + 0x3C, 0)
        r = [emu.read_u32(actor + o) for o in (0x4, 0x8, 0xC, 0x38, 0x24, 0x28, 0x58)]
        return f"{last} {calls} {r[0]:08x} {r[1]:08x} {r[2]:08x} {r[3]:08x} {r[4]} {r[5]} {r[6]} {h:016x}"

    def land(self, g, hits, updates, extra, player):
        """The hits, delivered or the first sent, then 00503f80 and ProcessState update by update,
        until the update returns 0."""
        emu, actor, foe, pc = self.emu, self.actor, self.foe, self.pc
        for offset, name in ((0x54, "play"), (0x70, "start"), (0x1A8, "f1a8"), (0x1BC, "f1bc"), (0x2A0, "weapon"),
                             (0x210, "f210")):
            emu.write_u32(actor + offset, g[name] & 0xFFFFFFFF)
        for offset in (0x1D4, 0x1F4, 0xB8, 0xBC, 0xA8, 0x6C, 0x228, 0x22C) + (() if player else (0x1CC, 0x218, 0xCC)):
            emu.write_u32(actor + offset, 0)
        emu.mem_write(actor + 0x83, b"\0")
        self.made, self.made_by, self.made_at, self.effect_messages, self.deleted = [], [], [], [], []
        self.empty_list(actor + EFFECT_LIST)
        self.empty_list(actor + LAUNCHED_LIST)
        self.empty_list(actor + HIT_QUEUE)
        for k in range(EFFECT_SLOT_COUNT):
            emu.write_u32(actor + EFFECT_SLOTS + 4 * k, 0)
        for k in range(g["listed"]):
            effect = self.make_effect(2, 0, 0, 0)
            if g["kinds"] >> k & 1:
                emu.write_u32(effect + 0x10C, 0x16)
            self.link(effect)
            if k == 0 and g["slot"] < EFFECT_SLOT_COUNT:
                emu.write_u32(actor + EFFECT_SLOTS + 4 * g["slot"], effect)
        for other, x, z, bit in ((foe, g["tx"], g["tz"], 1), (pc, g["px"], g["pz"], 2)):
            emu.mem_write(other, bytes(ACTOR_SIZE))
            emu.write_u32(other, PC_VTABLE if g["pcs"] & bit else GAME_ACTOR_VTABLE)
            emu.write_u32(other + 4, x)
            emu.write_u32(other + 0xC, z)
            for offset, name in ((0x1B8, "ajob"), (0x21C, "aeffect"), (0x2A0, "aweapon")):
                emu.write_u32(other + offset, g[name] & 0xFFFFFFFF)
            emu.write_u32(other + 0x1B4, 0x5A5A0000 | bit)
            for at in (EFFECT_LIST, LAUNCHED_LIST, HIT_QUEUE):
                self.empty_list(other + at)
        emu.write_u32(OWN_ID, g["own"] & 0xFFFFFFFF)
        emu.write_u32(self.world + 0x3C, actor if g["attacker"] == 4 else pc)
        emu.write_u32(self.world + WORLD_GUILD_WAR, g["war"])
        statuses = {0: [], 1: [0x13], 2: [5], 3: [5, 0x13]}[g["statuses"]]
        emu.mem_write(self.statuses, struct.pack("<2I", *statuses, *[0] * (2 - len(statuses))))
        emu.write_u32(OWN_STATUSES + 4, self.statuses)
        emu.write_u32(OWN_STATUSES + 8, self.statuses + 4 * len(statuses))
        emu.write_u32(EFFECTS_ON, g["effects_on"])
        emu.write_u32(MSG_SPRITES, g["msgs"])
        emu.call(SRAND, g["seed"] & 0xFFFFFFFF)
        saved = self.target
        self.target, self.target_found = (actor if g["attacker"] == 3 else foe), g["attacker"] in (1, 3)
        self.events, self.launched, self.arrows, self.attacking, self.landing = g["events"], [], [], True, True
        self.launched_effects, self.launched_messages = {}, []
        records = [hits[k:k + ATTACK_RECORD] for k in range(0, len(hits), ATTACK_RECORD)]
        first = None
        for k, record in enumerate(records):
            if k == 0 and g["how"]:
                first = emu.scratch(struct.pack(f"<{ATTACK_RECORD}I", *(r & 0xFFFFFFFF for r in record)))
            else:
                self.push(actor + HIT_QUEUE, [r & 0xFFFFFFFF for r in record])
        self.messages, self.states, self.sounds = [], [], []
        self.server, self.now = updates[0], updates[1]
        if g["how"]:
            emu.call(emu.read_u32(emu.read_u32(actor) + 4 * 2), 0, g["how"], first, 0, 0, this=actor)
        h = fnv1a64([7, *self.fields(), *self.messages, *extra(), *self.landed()])
        last, calls = "staying", 0
        for server, tick in zip(updates[2::2], updates[3::2]):
            self.messages, self.states = [], []
            self.now, self.server = tick, server
            emu.call(DELIVER, this=actor)
            ret = emu.call(emu.read_u32(emu.read_u32(actor) + 4 * 9), this=actor).eax & 0xFF
            calls += 1
            h = fnv1a64([ret, *self.fields(), *self.messages, *extra(), *self.landed()], h)
            if ret == 0:
                last = "faded"
                break
        self.attacking, self.landing, self.target, self.target_found = False, False, saved, False
        emu.write_u32(self.world + 0x3C, 0)
        emu.write_u32(self.world + WORLD_GUILD_WAR, 0)
        for address in (OWN_STATUSES + 4, OWN_STATUSES + 8, EFFECTS_ON, MSG_SPRITES):
            emu.write_u32(address, 0)
        r = [emu.read_u32(actor + o) for o in (0x4, 0x8, 0xC, 0x38, 0x24, 0x28, 0x58)]
        for offset in (0x210, 0x20C):
            emu.write_u32(actor + offset, 0)
        return f"{last} {calls} {r[0]:08x} {r[1]:08x} {r[2]:08x} {r[3]:08x} {r[4]} {r[5]} {r[6]} {h:016x}"

    def cast(self, g, updates, player):
        """The message and the state, then 00503210 and ProcessState update by update, until the
        update returns 0 or, out of state 13, no window is left."""
        emu, actor = self.emu, self.actor
        for offset, name in ((0xCC, "f0cc"), (0x1E0, "f1e0"), (0x1E4, "f1e4"), (0x1A4, "spoke"), (0x1D8, "end"),
                             (0x1DC, "begin"), (0x90, "screen_x"), (0x94, "screen_y"), (0x48, "pixels"), (0x1BC, "f1bc"),
                             (0x2A0, "weapon"), (0x54, "play"), (0x70, "start")):
            emu.write_u32(actor + offset, g[name] & 0xFFFFFFFF)
        for offset in (0x1D4, 0x1F4, 0x1A8) + (() if player else (0x218,)):
            emu.write_u32(actor + offset, 0)
        self.window_numbers, self.window_calls, self.windows = {}, [], []
        for k, offset in enumerate(WINDOW_OFFSETS):
            window = 0
            if g["windows"] >> k & 1:
                window = self.new_window(emu.scratch(size=0x90))
                size = (g["bw"], g["bh"]) if k == 0 else ((0x3C, 6) if k == 3 else (0x8C, 0x22))
                emu.write_u32(window + 0x1C, size[0] & 0xFFFFFFFF)
                emu.write_u32(window + 0x20, size[1] & 0xFFFFFFFF)
            emu.write_u32(actor + offset, window)
        emu.write_u32(SCENE_RENDERER, self.renderer)
        emu.write_u32(self.renderer + 0x24, g["width"] & 0xFFFFFFFF)
        emu.write_u32(self.renderer + 0x28, g["height"] & 0xFFFFFFFF)
        for address in WINDOW_STATICS:
            emu.mem_write(address, bytes([emu.mem_read(address, 1)[0] & 0xFE]))
        self.casting = True
        self.messages, self.states = [], []
        self.server, self.now = updates[0], updates[1]
        if g["how"]:
            emu.call(emu.read_u32(emu.read_u32(actor) + 4 * 2), 0, g["how"], g["duration"] & 0xFFFFFFFF, 0, 0,
                     this=actor)
        if g["enter"]:
            emu.call(emu.read_u32(emu.read_u32(actor) + 4 * 10), g["enter"], this=actor)
        h = fnv1a64([8, *self.fields(), *self.messages, *self.casted(player)])
        last, calls = "staying", 0
        for server, tick in zip(updates[2::2], updates[3::2]):
            self.now, self.server = tick, server
            self.messages, self.states = [], []
            emu.call(STEP_WINDOWS, this=actor)
            h = fnv1a64([9, *self.fields(), *self.messages, *self.casted(player)], h)
            self.messages, self.states = [], []
            ret = emu.call(emu.read_u32(emu.read_u32(actor) + 4 * 9), this=actor).eax & 0xFF
            calls += 1
            h = fnv1a64([ret, *self.fields(), *self.messages, *self.casted(player)], h)
            if ret == 0:
                last = "faded"
                break
            if emu.read_u32(actor + 0x58) != 13 and not any(emu.read_u32(actor + o) for o in WINDOW_OFFSETS):
                last = "settled"
                break
        self.casting = False
        emu.write_u32(SCENE_RENDERER, 0)
        for offset in WINDOW_OFFSETS:
            emu.write_u32(actor + offset, 0)
        r = [emu.read_u32(actor + o) for o in (0x4, 0x8, 0xC, 0x38, 0x24, 0x28, 0x58)]
        return f"{last} {calls} {r[0]:08x} {r[1]:08x} {r[2]:08x} {r[3]:08x} {r[4]} {r[5]} {r[6]} {h:016x}"

    def update(self, g, updates, player, own=False):
        """The actor's whole update, CNpc's, CPc's or CPlayer's vf01, update by update until it
        returns 0."""
        emu, actor = self.emu, self.actor
        if own:
            self.ready_own(g)
        for offset, name in ((0x230, "f230"), (0x6C, "keyed_at"), (0x68, "key"), (0x1EC, "f1ec"), (0x290, "f290"),
                             (0x1F4, "f1f4"), (0x218, "body"), (0x2C, "f2c"), (0x30, "f30"), (0x28, "frame"),
                             (0x90, "screen_x"), (0x94, "screen_y"), (0x48, "pixels"), (0x1D8, "end"),
                             (0x1DC, "begin"), (0xCC, "f0cc"), (0x1BC, "f1bc"), (0x2A0, "weapon"), (0x54, "play"),
                             (0x70, "start"), (0x24, "action")):
            emu.write_u32(actor + offset, g[name] & 0xFFFFFFFF)
        emu.mem_write(actor + 0x83, bytes([g["keyed"]]))
        emu.mem_write(actor + 0x87, bytes([g["tinted"]]))
        for offset in (0x1D4, 0x1A8, 0x1A4, 0x210, 0xA8):
            emu.write_u32(actor + offset, 0)
        self.empty_list(actor + EFFECT_LIST)
        self.empty_list(actor + LAUNCHED_LIST)
        self.empty_list(actor + HIT_QUEUE)
        for k in range(EFFECT_SLOT_COUNT):
            emu.write_u32(actor + EFFECT_SLOTS + 4 * k, 0)
        self.window_numbers, self.window_calls, self.windows = {}, [], []
        for offset in WINDOW_OFFSETS:
            emu.write_u32(actor + offset, 0)
        if g["cast"]:
            gauge = self.new_window(emu.scratch(size=0x90))
            emu.write_u32(gauge + 0x1C, 0x3C)
            emu.write_u32(gauge + 0x20, 6)
            emu.write_u32(actor + 0x1CC, gauge)
        if player:
            emu.write_u32(actor + GAUGE_OFFSET, self.new_window(emu.scratch(size=0xA8)) if g["gauge"] else 0)
            emu.write_u32(self.mode + MODE_FIELD_2A0, 0x5A5A5A5A)
        emu.write_u32(SCENE_RENDERER, self.renderer)
        emu.write_u32(self.renderer + 0x24, 640)
        emu.write_u32(self.renderer + 0x28, g["height"] & 0xFFFFFFFF)
        for address in WINDOW_STATICS:
            emu.mem_write(address, bytes([emu.mem_read(address, 1)[0] & 0xFE]))
        emu.write_u32(self.world + 0x3C, self.player)
        emu.write_u32(self.player + 4, g["px"])
        emu.write_u32(self.player + 0xC, g["pz"])
        emu.write_u32(EFFECTS_ON, g["effects_on"])
        self.events, self.event_names = g["events"], self.update_names
        self.attacking = self.landing = self.casting = self.updating = True
        self.launched, self.launched_effects, self.launched_messages, self.sounds = [], {}, [], []
        update = NPC_UPDATE if not player else emu.read_u32(emu.read_u32(actor) + 4)
        h, last, calls = 0xCBF29CE484222325, "staying", 0
        for server, tick in zip(updates[0::2], updates[1::2]):
            self.now, self.server = tick, server
            self.messages, self.states = [], []
            ret = emu.call(update, this=actor).eax & 0xFF
            calls += 1
            h = fnv1a64([ret, *self.fields(), *self.messages, *self.updated(player),
                         *(self.own() if own else ())], h)
            if ret == 0:
                last = "faded"
                break
        if own:
            self.target, self.target_found = self.saved_target
            self.found = 0
            for address in (PING, ATTACK_RANGE, OWN_ID):
                emu.write_u32(address, 0)
        self.attacking = self.landing = self.casting = self.updating = False
        self.event_names = self.attack_names
        emu.write_u32(self.world + 0x3C, 0)
        emu.write_u32(EFFECTS_ON, 0)
        emu.write_u32(SCENE_RENDERER, 0)
        for offset in WINDOW_OFFSETS + ((GAUGE_OFFSET,) if player else ()):
            emu.write_u32(actor + offset, 0)
        r = [emu.read_u32(actor + o) for o in (0x4, 0x8, 0xC, 0x38, 0x24, 0x28, 0x58)]
        return f"{last} {calls} {r[0]:08x} {r[1]:08x} {r[2]:08x} {r[3]:08x} {r[4]} {r[5]} {r[6]} {h:016x}"

    def updated(self, player):
        """What the whole update writes beyond fields(): +0x230, +0x290, +0x68, +0x6c, +0x2c, +0x30,
        the cast's times, +0xcc, +0x83 and the gauge by number; for a player its gauge by number,
        CGameMode+0x2a0 and +0x294; what was played, launched and sent to what was launched, what
        the windows were told and which were queued for removal since."""
        emu, a = self.emu, self.actor
        words = [emu.read_u32(a + o) for o in (0x230, 0x290, 0x68, 0x6C, 0x2C, 0x30, 0x1D8, 0x1DC, 0xCC)]
        words += [emu.mem_read(a + 0x83, 1)[0], self.window_number(emu.read_u32(a + 0x1CC))]
        if player:
            words += [self.window_number(emu.read_u32(a + GAUGE_OFFSET)), emu.read_u32(self.mode + MODE_FIELD_2A0),
                      emu.read_u32(a + 0x294)]
        words += [len(self.sounds), *self.sounds, len(self.launched), *self.launched,
                  len(self.launched_messages), *self.launched_messages, len(self.window_calls), *self.window_calls,
                  len(self.windows), *(self.window_number(w) for w in self.windows)]
        self.sounds, self.launched, self.launched_messages, self.window_calls, self.windows = [], [], [], [], []
        return words

    def ready_own(self, g):
        """o's CPlayer: its vtable and fields, what is pursued and what CWorld finds, the
        ClientState words and CGameMode+0x198's list."""
        emu, actor, t = self.emu, self.actor, self.pursued
        emu.write_u32(actor, PLAYER_VTABLE)
        names = OWN_FIELDS[:len(OWN_OFFSETS)]
        for offset, name in zip(OWN_OFFSETS, names):
            emu.write_u32(actor + offset, g[name] & 0xFFFFFFFF)
        emu.write_u32(PING, g["ping"] & 0xFFFFFFFF)
        emu.write_u32(ATTACK_RANGE, g["attack_range"] & 0xFFFFFFFF)
        emu.write_u32(OWN_ID, g["own"] & 0xFFFFFFFF)
        emu.mem_write(t, bytes(0x400))
        for offset, name in ((0x4, "tx"), (0x8, "ty"), (0xC, "tz"), (0x58, "tstate"), (0xC0, "tcx"), (0xC4, "tcy"),
                             (0x238, "tmodel"), (0x1B8, "tjob"), (0x21C, "teffect")):
            emu.write_u32(t + offset, g[name] & 0xFFFFFFFF)
        self.found = g["found"]
        self.saved_target = self.target, self.target_found
        self.target, self.target_found = t, g["found"] & 1 != 0
        head = emu.scratch(size=12)
        emu.write_u32(head, head)
        emu.write_u32(head + 4, head)
        emu.write_u32(self.mode + PICKED_LIST, head)
        emu.write_u32(self.mode + PICKED_LIST + 4, 0)
        if g["listed"]:
            node = emu.scratch(struct.pack("<3I", head, head, (g["target"] + (g["listed"] == 2)) & 0xFFFFFFFF))
            emu.write_u32(head, node)
            emu.write_u32(head + 4, node)
            emu.write_u32(self.mode + PICKED_LIST + 4, 1)
        self.own_messages, self.own_moves = [], []

    def own(self):
        """What o's update writes beyond p's: the CPlayer's fields, what it sent itself and the
        orders of its moves, and CGameMode+0x198's list, its size and ids."""
        emu = self.emu
        words = [emu.read_u32(self.actor + o) for o in OWN_OFFSETS]
        words += [len(self.own_messages), *self.own_messages, len(self.own_moves), *self.own_moves]
        head = emu.read_u32(self.mode + PICKED_LIST)
        ids, node = [], emu.read_u32(head)
        while node != head and len(ids) < 16:
            ids.append(emu.read_u32(node + 8))
            node = emu.read_u32(node)
        self.own_messages, self.own_moves = [], []
        return words + [emu.read_u32(self.mode + PICKED_LIST + 4), len(ids), *ids]

    def new_window(self, window):
        """A stand-in window, numbered next."""
        self.emu.mem_write(window, bytes(0x90))
        self.emu.write_u32(window, self.window_vtable)
        self.window_numbers[window] = len(self.window_numbers) + 1
        return window

    def window_number(self, window):
        return self.window_numbers.get(window, 0 if window == 0 else 0xFFFFFFFF)

    def casted(self, player):
        """The windows by number, the cast's times, +0x1a4 and +0xcc; for a player
        CGameMode+0x2a0 and +0x294; what the windows were told and which were queued for
        removal since."""
        emu, a = self.emu, self.actor
        words = [self.window_number(emu.read_u32(a + o)) for o in WINDOW_OFFSETS]
        words += [emu.read_u32(a + o) for o in (0x1A4, 0x1D8, 0x1DC, 0xCC)]
        if player:
            words += [emu.read_u32(self.mode + MODE_FIELD_2A0), emu.read_u32(a + 0x294)]
        words += [len(self.window_calls), *self.window_calls, len(self.windows)]
        words += [self.window_number(w) for w in self.windows]
        self.window_calls, self.windows = [], []
        return words

    def listed(self, at):
        """The effects in the list at `at` by number."""
        emu = self.emu
        head = emu.read_u32(at + 4)
        words, node = [emu.read_u32(at + 8)], emu.read_u32(head)
        while node != head:
            words.append(self.number(emu.read_u32(node + 8)))
            node = emu.read_u32(node)
        return words

    def landed(self):
        """What a hit's landing writes beyond fields(), and the queue; what was played,
        launched and sent to what was launched since; the slots and the three lists of
        effects, every effect's maker, what it was made at and for, +0x44, +0x3c, +0x7c and
        +0x10c, and what they were sent and which the world deleted since."""
        emu, a = self.emu, self.actor
        words = [emu.read_u32(a + o) for o in (0x6C, 0xA8, 0x228, 0x22C, 0x210, 0x1A8, 0xCC)]
        words += [emu.mem_read(a + 0x83, 1)[0], *self.queue(a)]
        words += [len(self.sounds), *self.sounds, len(self.launched), *self.launched,
                  len(self.launched_messages), *self.launched_messages]
        words += [self.number(emu.read_u32(a + EFFECT_SLOTS + 4 * k)) for k in range(EFFECT_SLOT_COUNT)]
        for at in (a, self.foe, self.pc):
            words += self.listed(at + EFFECT_LIST)
        words.append(len(self.made))
        for effect, by, at in zip(self.made, self.made_by, self.made_at):
            words += [by[0], by[1], self.where(at), by[3]]
            words += [emu.read_u32(effect + o) for o in (0x44, 0x3C, 0x7C, 0x10C)]
        words += [len(self.effect_messages), *self.effect_messages, len(self.deleted), *self.deleted]
        self.sounds, self.launched, self.launched_messages, self.effect_messages, self.deleted = [], [], [], [], []
        return words

    def push(self, at, words):
        """A node at the end of the std::list at `at`, as push_back."""
        emu = self.emu
        head = emu.read_u32(at + 4)
        last = emu.read_u32(head + 4)
        node = emu.scratch(struct.pack(f"<2I{len(words)}I", head, last, *words))
        emu.write_u32(last, node)
        emu.write_u32(head + 4, node)
        emu.write_u32(at + 8, emu.read_u32(at + 8) + 1)

    def queue(self, at):
        """An actor's hits (+0x204): its size, then each node's words."""
        emu = self.emu
        head = emu.read_u32(at + HIT_QUEUE + 4)
        words, node = [emu.read_u32(at + HIT_QUEUE + 8)], emu.read_u32(head)
        while node != head:
            words += struct.unpack(f"<{ATTACK_RECORD}I", emu.mem_read(node + 8, 4 * ATTACK_RECORD))
            node = emu.read_u32(node)
        return words

    def attacked(self):
        """+0x1a8, +0xcc, and what was logged, launched and shot since, and the three queues."""
        emu, actor = self.emu, self.actor
        words = [emu.read_u32(actor + 0x1A8), emu.read_u32(actor + 0xCC), len(self.logged),
                 len(self.launched), *self.launched, len(self.arrows), *self.arrows]
        self.logged, self.launched, self.arrows = [], [], []
        for at in (actor, self.foe, self.pc):
            words += self.queue(at)
        return words

    def make_effect(self, by, kind, at, actor_id):
        """An effect as the game mode makes one: its vtable, the rest 0."""
        effect = self.emu.scratch(size=0x120)
        self.emu.write_u32(effect, self.effect_vtable)
        self.made.append(effect)
        self.made_by.append([by, kind, int(at == self.actor + 4), actor_id])
        self.made_at.append(at)
        return effect

    def who(self, sender):
        """A sender for L and O: 0 none, 1 the actor, 2 the other, 3 the player, 4 another."""
        return {0: 0, self.actor: 1, self.foe: 2, self.pc: 3}.get(sender, 4)

    def where(self, value):
        """An argument for L and O, the actor's, the other's and the player's positions as 1 to
        3."""
        return {self.actor + 4: 1, self.foe + 4: 2, self.pc + 4: 3}.get(value, value)

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
        self.made, self.made_by, self.made_at, self.effect_messages = [], [], [], []
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
