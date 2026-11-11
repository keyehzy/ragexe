"""Test vectors for the own player's making (world::MakeOwnPlayer, src/world/own_player.h), taken
from the original in the emulation oracle.

usage: .venv/bin/python -I tools/own_make_vectors.py > tests/unit/own_make_vectors.inc

CPlayer's constructor (0054b1d0) on tools/walk_vectors.py's three small maps (C3dAttr::Load,
00426ec0), ClientState's session (00686670 +0x5f4 to +0x628, +0xcdc, +0xcec, +0xd94, +0x18d0)
and the guild's globals (0067cb38, 0067cb3c) varied: cells at the maps' edges and on walls and
hills, directions from -100 to 100 (GetDirection, 00502b00, takes 360 off a facing until it is
below 360, which a facing past 2^24 never is), every job about the mounted
ones, sexes, the status words each bit by bit (the mount, the hiding bits, stone), the
player's statuses (00641f98) with each that shows an effect, effects on (00687ea8) or not,
times about 1200 ms and 2^32.

As CPlayer's constructor calls its reset (0054b3a6), the player's vtable becomes a copy whose
vf02 records the messages it sends itself and whose vf14 (Dress, 00542c50) records its words;
both return at once. The body's CActRes is one with tools/npc_state_vectors.py's delays.
Recorded: the fields, those of +0x308 to +0x35c, the game mode's messages (CGameMode::SendMsg,
vf05), CGameMode+0x2a0, the messages to itself, vf14's first words, the effects
CAbleToMakeEffect::LaunchEffect (00501e10) is asked for and, of those it makes (stand-ins from
its 00501e93), +0x3d8's and what they are sent. CRenderObject::SetSprite and SetAct, which
CRenderObject's constructor calls, PlayWave (004352a0) and DebugLog (004060f0) are stood in
for. The x87 runs at single precision.
"""

import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import path_vectors
import walk_vectors
from emu.emulator import Emulator
from emu.winapi import Stub
from unicorn import UC_PROT_ALL
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_EIP, UC_X86_REG_FPCW

PLAYER_CTOR, PLAYER_SIZE, RESET_CALL, PLAYER_VTABLE = 0x54B1D0, 0x3DC, 0x54B3A6, 0x5E9CF4
SEND_SLOT, DRESS_SLOT = 2, 14
SET_SPRITE, SET_ACT, JOB_NAMES = 0x5018A0, 0x501910, 0x686DC8
GET_GAME_MODE, GET_FRAME_COUNT, GET_FRAME, DEBUG_LOG, PLAY_WAVE = 0x4CBF30, 0x421A80, 0x421A60, 0x4060F0, 0x4352A0
LAUNCH_EFFECT, LAUNCH_MAKES, LAUNCH_RETURNS = 0x501E10, 0x501E93, 0x501E80
C3DATTR_CTOR, C3DATTR_LOAD = 0x426BA0, 0x426EC0
CLIENT_STATE, SCENE_RENDERER, SERVICE_TYPE = 0x686670, 0x625E98, 0x67C9D4
OWN_STATUSES, EFFECTS_ON, GUILD, EMBLEM = 0x641F98, 0x687EA8, 0x67CB38, 0x67CB3C
STUBS, MODE_SLOT, MODE_FIELD_2A0 = 0x7E000000, 5, 0x2A0
DELAYS = [1.0 + (a % 7) * 0.75 for a in range(0x48)]
# ClientState's session, by offset: the fields INPUTS names from "cell_x" to "effect".
SESSION = [(0x620, "cell_x"), (0x624, "cell_y"), (0x628, "direction"), (0xCDC, "id"), (0xD94, "speed"),
           (0xCEC, "job"), (0x18D0, "sex"), (0x5F4, "hair"), (0x5F8, "weapon"), (0x5FC, "shield"),
           (0x600, "top"), (0x604, "mid"), (0x608, "bottom"), (0x60C, "body_color"), (0x610, "hair_color"),
           (0x614, "body"), (0x618, "health"), (0x61C, "effect")]
INPUTS = (["map", "now"] + [name for _, name in SESSION]
          + ["guild", "emblem", "origin_x", "origin_y", "effects_on", "statuses", "status0", "status1", "status2"])
MAX_STATUSES = 3
OUTPUTS = 256
# The words recorded of the player, in order.
WORDS = [0x4, 0x8, 0xC, 0x38, 0x1B4, 0x23C, 0x240, 0x194, 0xC8, 0x58, 0x20, 0x24, 0x28, 0x44, 0x54, 0x70, 0x1BC,
         0x218, 0x21C, 0x220, 0x7C, 0x90, 0x94]
BYTES = [0x81, 0x82, 0x84, 0x85, 0x87, 0x88, 0x89]
OWN = [0x308, 0x30C, 0x310, 0x314, 0x318, 0x31C, 0x320, 0x324, 0x328, 0x32C, 0x330, 0x334, 0x338, 0x33C, 0x344,
       0x348, 0x34C, 0x350, 0x354, 0x358, 0x35C]
MAPS = [("open", dict(path_vectors.MAPS)["open"], False), ("walls", dict(path_vectors.MAPS)["walls"], False),
        ("hills", walk_vectors.HILLS, True)]


class Oracle:
    def __init__(self):
        emu = self.emu = Emulator().init_crt()
        emu.uc.reg_write(UC_X86_REG_FPCW, 0x007F)
        names = emu.scratch(size=0x400)
        for k in range(0, 0x400, 4):
            emu.write_u32(names + k, emu.cstr("x"))
        emu.write_u32(JOB_NAMES, names)
        emu.write_u32(SERVICE_TYPE, 1)
        emu.replace_thiscall(SET_SPRITE, 1, lambda this, name: 0)
        emu.replace_thiscall(SET_ACT, 1, lambda this, name: 0)
        emu.hook_code(DEBUG_LOG, DEBUG_LOG, lambda uc, a, s, _: emu.complete(0, emu.read_u32(emu.esp()), emu.esp() + 4))
        emu.hook_code(PLAY_WAVE, PLAY_WAVE, lambda uc, a, s, _: emu.complete(0, emu.read_u32(emu.esp()), emu.esp() + 4))
        self.now = 0
        emu.api.stubs["WINMM.DLL!timeGetTime"] = Stub(0, lambda: self.now)
        self.attr = emu.scratch(size=0x12C)
        self.mode, world, view = (emu.scratch(size=0x400) for _ in range(3))
        emu.write_u32(self.mode + 0xB4, world)
        emu.write_u32(self.mode + 0xB8, view)
        emu.write_u32(world + 0x40, self.attr)
        emu.replace_thiscall(GET_GAME_MODE, 0, lambda this: self.mode)
        emu.uc.mem_map(STUBS, 0x1000, UC_PROT_ALL)
        emu.mem_write(STUBS, b"\xC3" * 0x80)
        mode_vtable = emu.scratch(size=0x100)
        emu.write_u32(mode_vtable + 4 * MODE_SLOT, STUBS)
        emu.write_u32(self.mode, mode_vtable)
        self.vtable = emu.scratch(emu.mem_read(PLAYER_VTABLE, 0x100))
        emu.write_u32(self.vtable + 4 * SEND_SLOT, STUBS + 0x10)
        emu.write_u32(self.vtable + 4 * DRESS_SLOT, STUBS + 0x20)
        self.effect_vtable = emu.scratch(size=0x40)
        emu.write_u32(self.effect_vtable + 4 * SEND_SLOT, STUBS + 0x30)
        self.calls = {}
        emu.replace_thiscall(STUBS, 4, lambda this, *v: self.calls["mode"].append(list(v)))
        emu.replace_thiscall(STUBS + 0x10, 5, lambda this, sender, message, a, b, c:
                             self.calls["self"].append([message, a, b, c]))
        emu.replace_thiscall(STUBS + 0x20, 10, lambda this, *v: self.calls["dress"].append(list(v)))
        emu.replace_thiscall(STUBS + 0x30, 5, lambda this, sender, message, a, b, c:
                             self.calls["sent"].append([self.effects[this], message, a, b, c]))

        def launch(uc, address, size, _):
            self.calls["launched"].append(emu.read_u32(emu.esp() + 4))
        emu.hook_code(LAUNCH_EFFECT, LAUNCH_EFFECT, launch)

        def launch_makes(uc, address, size, _):
            effect = emu.scratch(size=0x120)
            emu.write_u32(effect, self.effect_vtable)
            self.effects[effect] = len(self.calls["launched"])
            uc.reg_write(UC_X86_REG_EAX, effect)
            uc.reg_write(UC_X86_REG_EIP, LAUNCH_RETURNS)
        emu.hook_code(LAUNCH_MAKES, LAUNCH_MAKES, launch_makes)
        self.effects = {}
        emu.replace_thiscall(GET_FRAME_COUNT, 1, lambda this, action: 1)
        frame = emu.scratch(size=0x40)
        emu.write_u32(frame + 0x34, 0xFFFFFFFF)
        emu.replace_thiscall(GET_FRAME, 2, lambda this, action, k: frame)
        self.act = emu.scratch(size=0x144)
        delays = emu.scratch(b"".join(struct.pack("<f", d) for d in DELAYS))
        emu.write_u32(self.act + 0x138, delays)
        emu.write_u32(self.act + 0x13C, delays + 4 * len(DELAYS))

        def reset(uc, address, size, _):
            player = uc.reg_read(UC_X86_REG_EBX)
            emu.write_u32(player, self.vtable)
            emu.write_u32(player + 0x9C, self.act)
        emu.hook_code(RESET_CALL, RESET_CALL, reset)
        self.renderer = emu.scratch(size=0x40)
        emu.write_u32(SCENE_RENDERER, self.renderer)
        self.statuses = emu.scratch(size=4 * MAX_STATUSES)
        self.map = None

    def load(self, m):
        emu = self.emu
        if self.map == m:
            return
        self.map = m
        _, rows, hilly = MAPS[m]
        emu.serve_data_files({"data\\t.gat": walk_vectors.gat(rows, hilly)})
        emu.mem_write(self.attr, bytes(0x12C))
        emu.call(C3DATTR_CTOR, this=self.attr)
        emu.call(C3DATTR_LOAD, emu.cstr("t.gat"), this=self.attr)

    def run(self, v):
        emu = self.emu
        self.load(v["map"])
        for offset, name in SESSION:
            emu.write_u32(CLIENT_STATE + offset, v[name] & 0xFFFFFFFF)
        emu.write_u32(CLIENT_STATE + 0x504, 0)
        emu.write_u32(GUILD, v["guild"] & 0xFFFFFFFF)
        emu.write_u32(EMBLEM, v["emblem"] & 0xFFFFFFFF)
        emu.write_u32(self.renderer + 0x2C, v["origin_x"] & 0xFFFFFFFF)
        emu.write_u32(self.renderer + 0x30, v["origin_y"] & 0xFFFFFFFF)
        emu.write_u32(EFFECTS_ON, v["effects_on"])
        statuses = [v[f"status{k}"] for k in range(v["statuses"])]
        emu.mem_write(self.statuses, struct.pack(f"<{len(statuses)}I", *statuses))
        emu.write_u32(OWN_STATUSES + 4, self.statuses)
        emu.write_u32(OWN_STATUSES + 8, self.statuses + 4 * len(statuses))
        emu.write_u32(self.mode + MODE_FIELD_2A0, 0x5A5A5A5A)
        self.calls = {"mode": [], "self": [], "dress": [], "launched": [], "sent": []}
        self.effects = {}
        self.now = v["now"]
        mark = emu.heap_mark()
        player = emu.alloc(PLAYER_SIZE)
        emu.call(PLAYER_CTOR, this=player)
        u = emu.read_u32
        out = [u(player + o) for o in WORDS] + list(emu.mem_read(player + b, 1)[0] for b in BYTES)
        out += [u(player + o) for o in OWN]
        out += [self.effects.get(u(player + 0x3D8), 0), u(self.mode + MODE_FIELD_2A0)]
        c = self.calls
        out += c["dress"][0] if c["dress"] else [0] * 10
        out += [len(c["mode"])] + [x for m in c["mode"] for x in m]
        out += [len(c["self"])] + [x for m in c["self"] for x in m]
        out += [len(c["launched"])] + c["launched"]
        out += [len(c["sent"])] + [x for m in c["sent"] for x in m]
        emu.heap_release(mark)
        assert len(out) <= OUTPUTS, len(out)
        return [x & 0xFFFFFFFF for x in out]


def vectors():
    rng = random.Random(0x0054B1D0)
    effect_bits = [0, 0, 0, 0x20, 0x20, 1, 2, 4, 0x40, 0x10, 8, 0x80, 0x400, 0x22, 0x46]
    out = []
    for k in range(600):
        m = k % len(MAPS)
        rows = MAPS[m][1]
        width, height = len(rows[0]), len(rows)
        job = rng.choice([0, 1, 4, 6, 7, 7, 7, 0xD, 0xE, 0xE, 0xE, 0x13, 0x14, 0x15, 0x17, rng.randint(0, 25)])
        n = rng.choice([0, 0, 0, 1, 1, 2, 3])
        v = {
            "map": m,
            "now": rng.choice([1000000, 1000000, 0, 1199, 1200, 1201, 0xFFFFFFFF, rng.getrandbits(32)]),
            "cell_x": rng.choice([0, width - 1, rng.randrange(width), rng.randrange(width)]),
            "cell_y": rng.choice([0, height - 1, rng.randrange(height), rng.randrange(height)]),
            "direction": rng.choice([0, 1, 2, 3, 4, 5, 6, 7, rng.randint(0, 7), -1, -4, -5, 8, 9, 100, -100]),
            "id": rng.choice([150001, 2000000, 0x2100E, rng.getrandbits(31)]),
            "speed": rng.choice([150, 150, 100, 200, 0, rng.getrandbits(16)]),
            "job": job,
            "sex": rng.choice([0, 1, 1, 2, -1]),
            "hair": rng.choice([rng.randint(0, 19), -1, 20]),
            "weapon": rng.choice([0, rng.randint(1, 22)]),
            "shield": rng.choice([0, 1, 2, 3, 4]),
            "top": rng.choice([0, rng.randint(1, 139)]),
            "mid": rng.choice([0, rng.randint(1, 139)]),
            "bottom": rng.choice([0, rng.randint(1, 139)]),
            "body_color": rng.choice([0, rng.randint(1, 4)]),
            "hair_color": rng.choice([0, rng.randint(1, 8)]),
            "body": rng.choice([0, 0, 0, 1, 2, 3, 4, 6]),
            "health": rng.choice([0, 0, 1, 2, 4, 0x10, 0x20, 0x40, rng.getrandbits(7)]),
            "effect": rng.choice(effect_bits) | (rng.choice(effect_bits) if rng.random() < 0.3 else 0),
            "guild": rng.choice([0, 0, rng.getrandbits(31)]),
            "emblem": rng.choice([0, 0, rng.randint(1, 50)]),
            "origin_x": rng.choice([0, 0, 320, rng.randint(-50, 800)]),
            "origin_y": rng.choice([0, 0, 240, rng.randint(-50, 600)]),
            "effects_on": rng.choice([0, 0, 1]),
            "statuses": n,
        }
        for s in range(MAX_STATUSES):
            v[f"status{s}"] = rng.choice([2, 0x19, 0x1F, 0x44, 0x56, 0x57, 0x13, 5, 0, 0x1A]) if s < n else 0
        out.append(v)
    return out


def main():
    o = Oracle()
    lines = [
        "// Generated by tools/own_make_vectors.py from the original in the emulation oracle.",
        "// CPlayer's constructor (0054b1d0): {" + ", ".join(INPUTS) + "}, the map",
        "// tools/walk_vectors.py's (kWalkMaps); then the words +4, +8, +0xc, +0x38, +0x1b4, +0x23c,",
        "// +0x240, +0x194, +0xc8, +0x58, +0x20, +0x24, +0x28, +0x44, +0x54, +0x70, +0x1bc, +0x218,",
        "// +0x21c, +0x220, +0x7c, +0x90, +0x94, the bytes +0x81, +0x82, +0x84, +0x85, +0x87, +0x88,",
        "// +0x89, the words +0x308 to +0x35c of OWN in own_make_vectors.py, +0x3d8 as the number of its",
        "// effect (0 none), CGameMode+0x2a0 (0x5a5a5a5a before), vf14's ten words, and the calls: the",
        "// game mode's messages and the player's to itself (4 words each), the effects launched, and",
        "// what those made are sent (their number, the message and three words), each list after its",
        "// count.",
        "static const OwnMakeVector kOwnMakes[] = {",
    ]
    for v in vectors():
        given = ", ".join(f"0x{v[k] & 0xFFFFFFFF:x}" for k in INPUTS)
        got = ", ".join(f"0x{x:x}" for x in o.run(v))
        lines.append(f"    {{{{{given}}},\n     {{{got}}}}},")
    lines.append("};")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
