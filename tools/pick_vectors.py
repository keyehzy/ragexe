"""Test vectors for the port's picking (src/world/actor_pick.h), taken from the original in the
emulation oracle.

usage: .venv/bin/python -I tools/pick_vectors.py > tests/unit/pick_vectors.inc

  - The records: CGameActor::OnScreenBounds (00502cc0), CItem::vf05 (0053ae40) and
    CSkill::vf05 (005ac5b0) on objects with assorted bounds (ordered, swapped, too small,
    huge), 1/z, jobs (players, the portal, NPCs, 1000, monsters, the mounted 7 and 14),
    +0x1f4, guilds, Shift (00685fec), the aimed skill (00686020), the guild war (CWorld+0x60)
    and the player's guild (CWorld+0x3c), and renderer widths (+0x14) for the least size,
    worked out afresh each time (the flag bytes cleared). Each record is taken as it goes to
    CActorPickNode::Insert; an object that is not pickable (+0x89) gives none.
  - The tree: scripts on a CActorPickNode of its own: Build (004ce8e0) at depths 0 to 2 over
    assorted rectangles, Insert (004ce570) of records allocated with operator new, Find
    (004ce5f0) at points inside, on the edges and outside of them, and Clear (004ce710);
    then a script whose answer hangs on a record's left being just the split.
The x87 runs at single precision, as in the client.
"""
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from unicorn.x86_const import UC_X86_REG_FPCW  # noqa: E402

from emu import Emulator  # noqa: E402

ON_SCREEN_BOUNDS, ITEM_PICK, SKILL_PICK = 0x502CC0, 0x53AE40, 0x5AC5B0
NODE_CTOR, NODE_BUILD, NODE_INSERT, NODE_FIND, NODE_CLEAR = 0x4CE4F0, 0x4CE8E0, 0x4CE570, 0x4CE5F0, 0x4CE710
GET_GAME_MODE, OPERATOR_NEW = 0x4CBF30, 0x5C183B
GAME_ACTOR_VTABLE, G_PSCENE_RENDERER, G_PICK_NODE = 0x5E95C0, 0x625E98, 0x67CA60
SHIFT, AIMING = 0x685FEC, 0x686020
MINIMUMS = {"actor": (0x67CF24, 0x67CF20), "item": (0x686030, 0x68602C), "skill": (0x687FAC, 0x687FA8)}
SINGLE = 0x007F


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def bits(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def hexf(x):
    return f"0x{bits(x):08x}"


class Oracle:
    def __init__(self):
        emu = Emulator().init_crt()
        emu.uc.reg_write(UC_X86_REG_FPCW, SINGLE)
        self.emu = emu
        self.mode, self.world, self.renderer = (emu.scratch(size=0x400) for _ in range(3))
        self.object, self.player = emu.scratch(size=0x300), emu.scratch(size=0x300)
        emu.write_u32(self.mode + 0xB4, self.world)
        emu.write_u32(G_PSCENE_RENDERER, self.renderer)
        emu.replace_thiscall(GET_GAME_MODE, 0, lambda this: self.mode)
        emu.call(NODE_CTOR, this=G_PICK_NODE)

    def picked(self, kind, fields):
        """The record one object's vf05 inserts, or None."""
        emu, o = self.emu, self.object
        emu.mem_write(o, bytes(0x300))
        emu.write_u32(o, GAME_ACTOR_VTABLE)
        emu.mem_write(o + 0x89, bytes([fields["pickable"]]))
        for offset, name in [(0x1B4, "id"), (0x1B8, "job"), (0x21C, "effect"), (0x1F4, "f1f4"), (0x224, "f224"),
                             (0x23C, "guild"), (0xB0, "id"), (0x280, "id")]:
            if kind == "actor" or name in ("id", "job", "effect"):
                emu.write_u32(o + offset, fields[name] & 0xFFFFFFFF)
        emu.write_u32(SHIFT, fields["shift"])
        emu.write_u32(AIMING, fields["aiming"])
        emu.write_u32(self.world + 0x60, fields["war"])
        if fields["player"] is None:
            emu.write_u32(self.world + 0x3C, 0)
        else:
            emu.mem_write(self.player, bytes(0x300))
            emu.write_u32(self.player, GAME_ACTOR_VTABLE)
            emu.write_u32(self.player + 0x23C, fields["player"] & 0xFFFFFFFF)
            emu.write_u32(self.world + 0x3C, self.player)
        flag, _ = MINIMUMS[kind]
        emu.mem_write(flag, bytes([0]))
        emu.mem_write(self.renderer + 0x14, struct.pack("<f", fields["ratio"]))
        mark = emu.heap_mark()
        bounds = emu.scratch(struct.pack("<5f", *fields["bounds"], fields["inverse_z"]))
        emu.call(NODE_BUILD, 0, 0, 0, 800, 600, this=G_PICK_NODE)
        address = {"actor": ON_SCREEN_BOUNDS, "item": ITEM_PICK, "skill": SKILL_PICK}[kind]
        emu.call(address, bounds, bits(1.5), bits(2.5), this=o)
        out = None
        if emu.read_u32(G_PICK_NODE + 0x2C):
            last = emu.read_u32(emu.read_u32(G_PICK_NODE + 0x28) + 4)
            out = struct.unpack("<6f4i", emu.mem_read(emu.read_u32(last + 8), 0x28))
        emu.call(NODE_CLEAR, this=G_PICK_NODE)
        emu.heap_release(mark)
        return out

    def node(self):
        emu = self.emu
        node = emu.call(OPERATOR_NEW, 0x30).eax
        emu.call(NODE_CTOR, this=node)
        return node

    def record(self, r):
        emu = self.emu
        p = emu.call(OPERATOR_NEW, 0x28).eax
        emu.mem_write(p, struct.pack("<6f4i", *r))
        return p

    def script(self, ops):
        """Results of a tree script: Find answers the id of the record found, or "-"."""
        emu = self.emu
        mark = emu.heap_mark()
        tree = self.node()
        out = []
        for op in ops:
            kind, *v = op.split()
            if kind == "B":
                emu.call(NODE_BUILD, *(int(x) & 0xFFFFFFFF for x in v), this=tree)
                out.append("")
            elif kind == "C":
                emu.call(NODE_CLEAR, this=tree)
                out.append("")
            elif kind == "I":
                r = [struct.unpack("<f", struct.pack("<I", int(x, 0)))[0] for x in v[:6]] + [int(x) for x in v[6:]]
                emu.call(NODE_INSERT, self.record(r), this=tree)
                out.append("")
            elif kind == "F":
                found = emu.call(NODE_FIND, int(v[0], 0), int(v[1], 0), this=tree).eax
                out.append("-" if found == 0 else str(emu.read_u32(found + 0x18)))
        emu.heap_release(mark)
        return out


def record_text(r):
    if r is None:
        return "-"
    return " ".join([hexf(x) for x in r[:6]] + [str(x) for x in r[6:]])


def pick_cases(rng):
    """The objects whose records the vectors keep, as tools and the test spell them."""
    cases = []
    jobs = [0, 1, 7, 14, 23, 44, 45, 46, 100, 999, 1000, 1001, 1002, 1100, 1500, -1, 0x7FFFFFFF]
    for n in range(400):
        kind = ["actor"] * 6 + ["item", "skill"]
        kind = rng.choice(kind)
        x, y = rng.uniform(-50, 850), rng.uniform(-50, 650)
        w, h = rng.choice([0.0, 3.0, 20.0, 39.0, 40.0, 41.0, 64.5, rng.uniform(0, 200)]), rng.choice(
            [0.0, 5.0, 33.0, 34.0, 35.0, 90.0, rng.uniform(0, 200)])
        bounds = [x, y, x + w, y + h]
        if rng.random() < 0.1:
            bounds[0], bounds[2] = bounds[2], bounds[0]
        if rng.random() < 0.03:
            bounds = [800.0, 600.0, 0.0, 0.0]
        if rng.random() < 0.02:
            bounds = [1e10, -1e10, -1e10, 1e10]
        guild = rng.choice([0, 0, 5, 9, rng.randint(1, 1 << 20)])
        fields = {
            "pickable": int(rng.random() < 0.93), "id": rng.getrandbits(31), "job": rng.choice(jobs + [rng.randint(0, 1600)]),
            "effect": rng.choice([0, 0, 0x20, rng.getrandbits(32)]), "f1f4": rng.choice([0, 0, 20, 20, 19, 21]),
            "f224": rng.getrandbits(31), "guild": guild,
            "shift": rng.choice([0, 1]), "aiming": rng.choice([0, 1]), "war": rng.choice([0, 0, 1]),
            "player": rng.choice([None, 0, 5, guild, rng.randint(1, 1 << 20)]),
            "ratio": f32(rng.choice([1.0, 1.25, 1.6, 2.0, 0.5, rng.uniform(0.3, 3.0)])),
            "bounds": [f32(b) for b in bounds], "inverse_z": f32(rng.choice([0.0, 0.002, 0.01, rng.uniform(0, 0.05)])),
        }
        cases.append((kind, fields))
    return cases


def case_text(kind, f):
    player = "-" if f["player"] is None else str(f["player"])
    return " ".join([kind[0].upper(), str(f["pickable"]), str(f["id"]), str(f["job"]), str(f["effect"]),
                     str(f["f1f4"]), str(f["f224"]), str(f["guild"]), str(f["shift"]), str(f["aiming"]),
                     str(f["war"]), player, hexf(f["ratio"])] + [hexf(b) for b in f["bounds"]] + [hexf(f["inverse_z"])])


def tree_scripts(rng):
    scripts = []
    for n in range(40):
        depth = rng.choice([0, 1, 1, 2])
        right, bottom = rng.choice([(800, 600), (640, 480), (1024, 768), (101, 77)])
        ops = [f"B {depth} 0 0 {right} {bottom}"]
        records = []
        for round_ in range(rng.choice([1, 1, 2])):
            if round_:
                ops.append("C")
                records = []
            for k in range(rng.choice([0, 1, 3, 8, 20])):
                l, t = rng.uniform(-20, right + 20), rng.uniform(-20, bottom + 20)
                w, h = rng.uniform(1, 150), rng.uniform(1, 150)
                z = rng.choice([0.0, -1.0, 50.0, 100.0, 200.0, 400.0]) + rng.choice([0.0, 0.0025, rng.uniform(0, 0.05)])
                z2 = z if rng.random() < 0.9 else rng.uniform(-10, 500)
                r = [f32(l), f32(t), f32(z), f32(l + w), f32(t + h), f32(z2), len(scripts) * 1000 + len(ops), 0, 0, 0]
                records.append(r)
                ops.append("I " + " ".join([hexf(x) for x in r[:6]] + [str(x) for x in r[6:]]))
            for q in range(rng.choice([5, 15, 40])):
                if records and rng.random() < 0.5:
                    r = rng.choice(records)
                    x = rng.choice([r[0], r[3], f32((r[0] + r[3]) / 2), f32(r[0] + 0.5)])
                    y = rng.choice([r[1], r[4], f32((r[1] + r[4]) / 2), f32(r[4] - 0.5)])
                else:
                    x, y = f32(rng.uniform(-30, right + 30)), f32(rng.uniform(-30, bottom + 30))
                ops.append(f"F {hexf(x)} {hexf(y)}")
        scripts.append(ops)
    return scripts


# A script whose answer hangs on one value: a record whose left is just half the tree's right
# is filed left of the split, so a find on the right answers with the shallower record there.
DECIDING = [[
    "B 1 0 0 800 600",
    f"I {hexf(400.0)} {hexf(10.0)} {hexf(300.0)} {hexf(500.0)} {hexf(100.0)} {hexf(300.0)} 1 0 0 0",
    f"I {hexf(401.0)} {hexf(10.0)} {hexf(100.0)} {hexf(500.0)} {hexf(100.0)} {hexf(100.0)} 2 0 0 0",
    f"F {hexf(450.0)} {hexf(50.0)}",
    f"F {hexf(399.5)} {hexf(50.0)}",
]]


def main():
    rng = random.Random(1907)
    oracle = Oracle()
    lines = ["// Generated by tools/pick_vectors.py from the original in the emulation oracle."]
    lines.append("// Each object's vf05: K pickable id job +0x21c +0x1f4 +0x224 +0x23c shift aiming war player")
    lines.append("// ratio left top right bottom 1/z (K: A actor, I item, S skill; the player's guild, or - for")
    lines.append("// none; floats as bits), then the record inserted, or -.")
    lines.append("static const PickVector kPicks[] = {")
    for kind, fields in pick_cases(rng):
        lines.append(f"    {{\"{case_text(kind, fields)}\", \"{record_text(oracle.picked(kind, fields))}\"}},")
    lines.append("};")
    lines.append("// Scripts on one CActorPickNode: B depth left top right bottom, I record (as above), C, and")
    lines.append("// F x y, answered with the found record's id or -.")
    lines.append("static const PickOpVector kPickOps[] = {")
    for n, ops in enumerate(tree_scripts(rng) + DECIDING):
        for op, result in zip(ops, oracle.script(ops)):
            lines.append(f"    {{{n}, \"{op}\", \"{result}\"}},")
    lines.append("};")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
