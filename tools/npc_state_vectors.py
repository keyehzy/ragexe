"""Test vectors for an NPC's or a monster's states (world::SetGameActorState on a CNpc,
src/world/actor_state.h), taken from the original in the emulation oracle.

usage: .venv/bin/python -I tools/npc_state_vectors.py > tests/unit/npc_state_vectors.inc

CNpc::SetState (0053fca0, vf10) on CNpcs its constructor (0053f950) made, their fields then set:
the jobs about each edge of its tables (the warp portal 0x2d, 0x68, 0x6f, 0x8b, the NPCs, the
monsters from 1000 and those whose attacks are ranged), the states from -1 to 11 and beyond,
from assorted states (4 among them), body states, pets' performances (+0x224) by the pet's id and
ClientState's +0x804 and +0x18d0, sizes about 10 and past 2^24, positions and actions. Recorded:
the fields it writes, the game mode's messages (CGameMode::SendMsg, vf05), the messages the NPC
sends itself (vf02, through a copy of its vtable), the effects CAbleToMakeEffect::LaunchEffect
(00501e10) is asked for, and the arrows for a target (CGameMode::AddArrowEffect, 005101f0) and
Mandragoras' attacks (CGameMode::AddMandraAttackEffect, 00510380) CGameActor::LaunchRangedAttack makes.
ClientState 005a9f40, 005aa3e0 and 005a4bd0 run for real. The body's CActRes is one with the
delays below; CActRes::GetFrameCount (00421a80) answers the vector's frame count and GetFrame
(00421a60) frames without events. CRenderObject::SetSprite and SetAct, which the constructor
calls, are stood in for, and DebugLog (004060f0) is skipped. The x87 runs at single precision.
"""

import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu.emulator import Emulator
from emu.winapi import Stub
from unicorn import UC_PROT_ALL
from unicorn.x86_const import UC_X86_REG_FPCW

NPC_CTOR, NPC_VTABLE, NPC_SIZE, SET_STATE_SLOT, SEND_SLOT = 0x53F950, 0x5E991C, 0x298, 10, 2
SET_SPRITE, SET_ACT, JOB_NAMES = 0x5018A0, 0x501910, 0x686DC8
GET_GAME_MODE, GET_FRAME_COUNT, GET_FRAME, DEBUG_LOG = 0x4CBF30, 0x421A80, 0x421A60, 0x4060F0
LAUNCH_EFFECT, ADD_ARROW, ADD_MANDRA = 0x501E10, 0x5101F0, 0x510380
G_CLIENT_STATE, PET_ID, PET_VALUE, PET_TABLE = 0x686670, 0x7D0, 0x804, 0x18D0
STUBS, MODE_SLOT = 0x7E000000, 5
SERVICE_TYPE = 0x67C9D4
# The body's delays (CActRes+0x138), one per action to 0x47; past them GetActionDelay gives 4.
DELAYS = [1.0 + (a % 7) * 0.75 for a in range(0x48)]
ATTR_W, ATTR_H, CELL = 40, 30, 5
JOBS = [0x2C, 0x2D, 0x2E, 0x67, 0x68, 0x69, 0x6E, 0x6F, 0x70, 0x8A, 0x8B, 0x8C, 0x3E7, 1000, 1002, 0x3F8,
        0x3FC, 0x42C, 0x505, 0x57F, 5, 7]
STATES = list(range(-1, 12)) + [13, 0x7FFFFFFF]


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def bits(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


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
        self.now = 0
        emu.api.stubs["WINMM.DLL!timeGetTime"] = Stub(0, lambda: self.now)
        attr = emu.scratch(size=0x130)
        emu.write_u32(attr + 0x110, ATTR_W)
        emu.write_u32(attr + 0x114, ATTR_H)
        emu.write_u32(attr + 0x118, CELL)
        self.mode, world, self.view = (emu.scratch(size=0x400) for _ in range(3))
        emu.write_u32(self.mode + 0xB4, world)
        emu.write_u32(self.mode + 0xB8, self.view)
        emu.write_u32(world + 0x40, attr)
        emu.replace_thiscall(GET_GAME_MODE, 0, lambda this: self.mode)
        emu.uc.mem_map(STUBS, 0x1000, UC_PROT_ALL)
        emu.mem_write(STUBS, b"\xC3" * 0x40)
        mode_vtable = emu.scratch(size=0x100)
        emu.write_u32(mode_vtable + 4 * MODE_SLOT, STUBS)
        emu.write_u32(self.mode, mode_vtable)
        self.vtable = emu.scratch(emu.mem_read(NPC_VTABLE, 0x100))
        emu.write_u32(self.vtable + 4 * SEND_SLOT, STUBS + 0x10)
        self.calls = {}
        emu.replace_thiscall(STUBS, 4, lambda this, *v: self.calls["mode"].append(list(v)))
        emu.replace_thiscall(STUBS + 0x10, 5, lambda this, sender, message, a, b, c:
                             self.calls["self"].append(message))
        emu.replace_thiscall(LAUNCH_EFFECT, 5, lambda this, effect, *rest: self.calls["launched"].append(effect) or 0)
        # AddArrowEffect makes an arrow only for a target.
        emu.replace_thiscall(ADD_ARROW, 3, lambda this, at, target, speed:
                             target and self.calls["arrows"].append([target, speed]) or 0)
        emu.replace_thiscall(ADD_MANDRA, 3, lambda this, target, job, zero:
                             self.calls["mandra"].append([target, job]) or 0)
        self.frames = 1
        emu.replace_thiscall(GET_FRAME_COUNT, 1, lambda this, action: self.frames if self.frames > 1 else 1)
        frame = emu.scratch(size=0x40)
        emu.write_u32(frame + 0x34, 0xFFFFFFFF)
        emu.replace_thiscall(GET_FRAME, 2, lambda this, action, k: frame)
        self.act = emu.scratch(size=0x144)
        delays = emu.scratch(b"".join(struct.pack("<f", d) for d in DELAYS))
        emu.write_u32(self.act + 0x138, delays)
        emu.write_u32(self.act + 0x13C, delays + 4 * len(DELAYS))

    def run(self, v):
        emu = self.emu
        self.now = v["now"]
        npc = emu.alloc(NPC_SIZE)
        emu.call(NPC_CTOR, this=npc)
        emu.write_u32(npc, self.vtable)
        emu.write_u32(npc + 0x9C, self.act)
        w = emu.write_u32
        for offset, key in ((0x1B8, "job"), (0x58, "old"), (0x218, "body"), (0x224, "f224"), (0x1EC, "f1ec"),
                            (0x1F0, "f1f0"), (0x294, "portal"), (0x24, "action"), (0x20, "base"), (0x28, "frame"),
                            (0x1B4, "id"), (0x1A8, "target"), (0x21C, "effect"), (0x54, "mode"), (0x70, "start"),
                            (0x1D4, "talk"), (0x1CC, "gauge")):
            w(npc + offset, v[key] & 0xFFFFFFFF)
        for offset, key in ((0x38, "facing"), (0x4, "x"), (0x8, "y"), (0xC, "z"), (0x228, "tx"), (0x22C, "tz"),
                            (0x44, "delay")):
            w(npc + offset, v[key])
        for offset, key in ((0x81, "finished"), (0x82, "held"), (0x89, "pickable"), (0x80, "depth_up"),
                            (0x85, "shown")):
            emu.mem_write(npc + offset, bytes([v[key]]))
        w(self.view + 0x20, v["yaw"])
        w(G_CLIENT_STATE + PET_ID, v["pet"] & 0xFFFFFFFF)
        w(G_CLIENT_STATE + PET_VALUE, v["f804"] & 0xFFFFFFFF)
        w(G_CLIENT_STATE + PET_TABLE, v["f18d0"] & 0xFFFFFFFF)
        self.frames = v["frames"]
        self.now = v["at"]
        self.calls = {"mode": [], "self": [], "launched": [], "arrows": [], "mandra": []}
        emu.call(emu.read_u32(NPC_VTABLE + 4 * SET_STATE_SLOT), v["state"] & 0xFFFFFFFF, this=npc)
        u = emu.read_u32
        out = [u(npc + o) for o in (0x4, 0x8, 0xC, 0x20, 0x24, 0x28, 0x54, 0x44, 0x4C, 0x50, 0x58, 0x70, 0x1D4,
                                     0x224, 0x1EC, 0x1F0, 0x27C, 0x280, 0x284, 0x288, 0x294, 0x1AC, 0x1CC)]
        out += list(emu.mem_read(npc + 0x80, 3)) + [emu.mem_read(npc + 0x89, 1)[0], emu.mem_read(npc + 0x190, 1)[0]]
        c = self.calls
        out += [len(c["mode"])] + [x for m in c["mode"] for x in m]
        out += [len(c["self"])] + c["self"] + [len(c["launched"])] + c["launched"]
        out += [len(c["arrows"])] + [x for a in c["arrows"] for x in a]
        out += [len(c["mandra"])] + [x for m in c["mandra"] for x in m]
        return out


INPUTS = ["job", "old", "state", "body", "f224", "f1ec", "f1f0", "portal", "shown", "action", "base", "frame",
          "facing", "yaw", "x", "y", "z", "tx", "tz", "id", "target", "delay", "frames", "pet", "f804", "f18d0",
          "effect", "finished", "held", "pickable", "depth_up", "mode", "start", "talk", "gauge", "now", "at"]


def vectors():
    rng = random.Random(0x0053FCA0)

    def coord():
        return bits(f32(rng.choice([rng.uniform(-100, 100), rng.uniform(-1e8, 1e8), 0.5, 12.5, -7.5, 16777217.0])))

    def size():
        return rng.choice([rng.randint(-20, 40), 9, 10, 11, 0, -1, 0x1000001, 0x7FFFFFFF, rng.randint(-2**31, 2**31 - 1)])

    out = []
    for k in range(2000):
        job = JOBS[k % len(JOBS)] if k < 1500 else rng.choice(JOBS)
        state = STATES[(k // len(JOBS)) % len(STATES)] if k < 1500 else rng.choice(STATES)
        id_ = rng.choice([0, 5, 2000001, rng.randint(1, 3000000)])
        v = {
            "job": job, "old": rng.choice([0, 0, 1, 4, 4, 6, 3, 2]), "state": state, "body": rng.choice([0, 0, 1, 2, 3]),
            "f224": rng.choice([0, 0, 1, 2, 3, 4, 5, -1]), "f1ec": size(), "f1f0": size(),
            "portal": rng.choice([0, 0, 1, 7]), "shown": rng.choice([1, 1, 0]),
            "action": rng.choice([0, 8, 0x10, 0x18, 0x27, 0x28, 0x29, 0x40, rng.randint(0, 80)]),
            "base": rng.choice([0, 8, 0x10, 0x20]), "frame": rng.randint(0, 5),
            "facing": bits(f32(rng.choice([0.0, 45.0, 180.0, 337.5, rng.uniform(0, 360)]))),
            "yaw": bits(f32(rng.choice([0.0, 90.0, rng.uniform(0, 360)]))),
            "x": coord(), "y": coord(), "z": coord(), "tx": coord(), "tz": coord(), "id": id_,
            "target": rng.choice([0, 0, 7, 2000001]), "delay": bits(f32(rng.choice([1.0, 4.0, 2.5, 0.5]))),
            "frames": rng.choice([1, 2, 3, 6]), "pet": rng.choice([id_, id_, 0, 99]),
            "f804": rng.choice([0, 10, 11, 25, 26, 75, 76, 90, 91, 100, 101, rng.randint(-50, 150)]),
            "f18d0": rng.choice([0, 1, 1, 2]), "effect": rng.choice([0, 0, 0x20]),
            "finished": rng.randint(0, 1), "held": rng.randint(0, 1), "pickable": rng.randint(0, 1),
            "depth_up": rng.randint(0, 1), "mode": rng.choice([0, 1, 2]), "start": rng.randint(0, 2**32 - 1),
            "talk": rng.choice([0, 12345]), "gauge": 0,
            "now": rng.choice([1000000, 0, 0xFFFFFF00]),
        }
        v["at"] = (v["now"] + rng.choice([0, 16, 5000])) & 0xFFFFFFFF
        out.append(v)
    return out


def main():
    o = Oracle()
    lines = [
        "// Generated by tools/npc_state_vectors.py from the original in the emulation oracle.",
        "// CNpc::SetState (0053fca0) on a CNpc: {" + ", ".join(INPUTS) + "}, then the words written",
        "// (+4, +8, +0xc, +0x20, +0x24, +0x28, +0x54, +0x44, +0x4c, +0x50, +0x58, +0x70, +0x1d4, +0x224,",
        "// +0x1ec, +0x1f0, +0x27c to +0x288, +0x294, +0x1ac, +0x1cc, the bytes +0x80 to +0x82, +0x89 and",
        "// +0x190) and the calls: the game mode's messages (4 words each), the NPC's to itself, the",
        "// effects launched, the arrows (target, speed) and the Mandragoras' attacks (target, job), each",
        "// list after its count.",
        "static const NpcStateVector kNpcStates[] = {",
    ]
    for v in vectors():
        given = ", ".join(f"0x{v[k] & 0xFFFFFFFF:x}" for k in INPUTS)
        got = ", ".join(f"0x{x:x}" for x in o.run(v))
        lines.append(f"    {{{{{given}}},\n     {{{got}}}}},")
    lines.append("};")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
