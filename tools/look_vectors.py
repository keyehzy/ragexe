"""Test vectors for the players' own messages (world::ArrivePlayer, ArriveWalkingPlayer and
SendPlayerMessage, src/world/player_look.h: CPc::SendMsg), taken from the original in the
emulation oracle.

usage: .venv/bin/python -I tools/look_vectors.py > tests/unit/look_vectors.inc

On tools/walk_vectors.py's three small maps, scripts as tools/walk_oracle.py's make_looks makes
them (c), then on the open field messages whose results each hang on one thing: each message in
each state; the arrivals' states, `how` and bit 6, ids and speeds; each job's weapons by kind and
the glows; the hair outside its styles, the headgear without actions, the shields by kind and id,
the colours 0 that keep the palettes; vf15's weapon word: the own sprite there or not, the two
hands' pairs, the jobs that hold a kind otherwise, 0x63f, the shield in the high word; the
administrators on both sets and sexes, id 0x2100e, the bard's and dancer's sexes, the mounted
jobs; vf37's word below 0 or not with the emotion's slot full or empty; the gauge made, filled,
moved at the screen's edges and closed; walks found or not.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import path_oracle
import path_vectors
import walk_oracle
import walk_vectors

PLAYER_FIELDS = 44
ARRIVE_FIELDS = 12
EFFECT_FIELDS = 3
LOOK_FIELDS = path_oracle.LOOK_FIELDS
STANDING = ["id", "job", "sex", "x", "y", "dir", "speed", "f1ec", "f1f0", "hair", "weapon", "top", "pc_state",
            "body", "health", "effect", "f224", "shield", "mid", "bottom", "hair_color", "body_color", "head",
            "guild", "emblem", "f28c", "f290", "pad"]
WALKING = ["id", "job", "sex", "time", "sx", "sy", "subx", "suby", "gx", "gy", "speed", "hair", "weapon", "top",
           "body", "health", "effect", "f224", "shield", "mid", "bottom", "hair_color", "body_color", "pad",
           "guild", "emblem", "f28c", "f290"]


def bits(f):
    return struct.unpack("<I", struct.pack("<f", f))[0]


def f32(f):
    return struct.unpack("<f", struct.pack("<f", f))[0]


def deciding(script, width, height):
    """Messages made from the first c line of a generated script, the player standing at the
    centre of cell (5, 4)."""
    base = next(q for q in script if q[0] == "c").split()[1:]
    n = PLAYER_FIELDS + ARRIVE_FIELDS + EFFECT_FIELDS + len(LOOK_FIELDS)
    server, tick = (int(base[n + 1 + k]) for k in (31, 32))
    lines = []
    x0, z0 = f32((5 - width // 2) * 5 + 2.5), f32((4 - height // 2) * 5 + 2.5)

    def looks(messages, state=0, job=0, effect=0, slots=0, player_id=7, exists=0, service=1, **o):
        """messages: (id, a, b, record) each, the record a dict of STANDING's or WALKING's names."""
        f = base[:PLAYER_FIELDS]
        f[0], f[1], f[2] = f"0x{bits(x0):08x}", f"0x{bits(z0):08x}", f"0x{bits(0.0):08x}"
        f[7], f[8], f[17], f[19], f[21], f[34] = str(job), str(effect), str(state), "0", str(service), str(player_id)
        arrive = ["0", "0", "0", "0", "11", "0", "0", "5", str(exists), "1", f"0x{bits(x0):08x}",
                  f"0x{bits(z0):08x}"]
        look = dict(sex=1, hair=1, hair_color=0, weapon=0, top=0, mid=0, bottom=0, shield=0, body_color=0, f28c=0,
                    f290=0, f2e0=0, f2e4=0, f1f4=0, gauge=0, screen_x=320, screen_y=240, height=480, admin=0,
                    effects_on=0, msgs=0)
        look.update(o)
        words, s, t = [], server, tick
        for message, a, b, record in messages:
            names = WALKING if message in (5, 6, 0x7A, 0x7B) else STANDING
            values = dict(id=player_id, job=job, sex=1, x=5, y=4, dir=0, speed=150, hair=1, time=s, sx=5, sy=4,
                          subx=8, suby=8, gx=8, gy=4)
            values.update(record or {})
            s, t = (s + 16) & 0xFFFFFFFF, (t + 16) & 0xFFFFFFFF
            words += [message, a, b, *(values.get(k, 0) for k in names), s, t]
        lines.append("c " + " ".join(f + arrive + [str(slots), "0", "0"]
                                     + [str(look[k] & 0xFFFFFFFF) for k in LOOK_FIELDS]
                                     + [str(len(messages))] + [str(v & 0xFFFFFFFF) for v in words]))

    # Each message in each state, with a gauge or none.
    for state in (0, 1, 2, 3, 4, 5, 6, 7, 8, 9):
        for gauge in (0, 1):
            looks([(0x22, 30, 100, None), (0x22, 0, 0, None), (0x23, 0, 0, None), (0x23, 0, 0, None)],
                  state=state, gauge=gauge)
            looks([(0x64, 5, 0, None), (0x4D, 1, 2, None), (0x72, 3, 4, None)], state=state, gauge=gauge)
            looks([(0x69, 0, 0, None), (0x6A, 0, 0, None)], state=state, job=7, effect=0x20, weapon=3, shield=1,
                  hair_color=2, body_color=3)
            for message in (3, 4, 5, 6, 0x79, 0x7A, 0x7B, 0x7C):
                looks([(message, 0, 0, dict(weapon=2))], state=state)
    # The arrivals' states, `how` and bit 6, the ids and speeds kept or not.
    for message in (3, 4, 0x79, 0x7C):
        for pc_state in (0, 1, 2, 3, -1):
            for how in (0, 1, 2):
                for effect in (0, 0x40):
                    looks([(message, 0, how, dict(pc_state=pc_state, effect=effect, id=9, speed=77))])
    for message in (5, 6, 0x7A, 0x7B):
        for goal in ((8, 4), (5, 4), (40, 4), (-3, -3)):
            looks([(message, 0, 0, dict(gx=goal[0], gy=goal[1], id=9, speed=77, subx=3, suby=12))])
    # Each job's weapons by kind, both sexes.
    for job in range(45):
        looks([(3, 0, 0, dict(job=job, weapon=kind)) for kind in range(0, 24)])
        looks([(3, 0, 0, dict(job=job, sex=0, weapon=kind)) for kind in range(0, 24)])
    # The hair, headgear, shields and colours.
    for hair in (-1, 0, 1, 19, 20, 13, 0x10000):
        for sex in (0, 1):
            looks([(3, 0, 0, dict(hair=hair, sex=sex, hair_color=2))])
    for headgear in (0, 1, 0x11, 0x12, 0x13, 0x14, 139):
        for sex in (0, 1):
            looks([(3, 0, 0, dict(top=headgear, mid=headgear, bottom=headgear, sex=sex))])
    for job in (0, 1, 4, 6, 7, 0xC, 0x13):
        for shield in (0, 1, 2, 3, 4, 5, 0x835, 0x836, 0x838, 0x83A, 0x83D, 0x83E, 0x83F, 2000, 2001):
            looks([(3, 0, 0, dict(job=job, shield=shield))])
    for colours in ((0, 0), (1, 0), (0, 1), (5, -1)):
        looks([(3, 0, 0, dict(hair_color=4, body_color=5)), (3, 0, 0, dict(hair_color=colours[0],
                                                                           body_color=colours[1]))])
    # vf15's weapon word: the own sprite there or not; the two hands; the jobs that hold a kind
    # otherwise; 0x63f; the shield in the high word.
    lows = [0, 1, 0x63F, 1100, 1149, 1150, 1200, 1201, 1250, 1300, 1301, 1350, 1400, 1450, 1500, 1550, 1600, 1650,
            1700, 1750, 1800, 1850, 1900, 1950, 1999, 2000, 1099]
    for job in (0, 1, 4, 6, 8, 0xC, 0x11, 0x13):
        for exists in (0, 1):
            looks([(0x79, 0, 0, dict(job=job, weapon=low)) for low in lows], exists=exists)
    for high in (0, 1100, 1149, 1150, 1200, 1249, 1250, 1300, 1349, 1350, 1600, 0x835, 0x83F, 2000, 2001, 5):
        looks([(0x7C, 0, 0, dict(job=0xC, weapon=high << 16 | low)) for low in (0, 1100, 1200, 1300, 1150, 1400)])
    # The administrators on both sets and sexes, id 0x2100e, the bard's and dancer's sexes, the
    # mounted jobs.
    for admin in (7, 0x2C8C7D, 0x2C8C82, 0x2100E):
        for sex in (0, 1):
            looks([(4, 0, 0, dict(id=admin, sex=sex, weapon=1)), (0x79, 0, 0, dict(id=admin, sex=sex, weapon=1150))],
                  admin=admin, player_id=admin, service=0)
    for job in (0x13, 0x14):
        for sex in (0, 1, 2, 3):
            looks([(3, 0, 0, dict(job=job, sex=sex))])
    for job in (7, 14, 0xD, 0x15, 6):
        for effect in (0, 0x20):
            looks([(3, 0, 0, dict(job=job, effect=effect)), (0x69, 0, 0, None)], effect=effect)
    # vf37's word below 0 or not, the emotion's slot full or empty.
    for slots in (0, 1 << 10):
        for f28c in (-1, 0, 5):
            looks([(3, 0, 0, dict(f28c=f28c, f290=9))], slots=slots)
    # The gauge at the screen's edges.
    for height in (480, 0, 479, 481, -480, 1 << 28, 0x7FFFFFFF):
        for x, y in ((320, 240), (0, 0), (-0x7FFFFFF0, 0x7FFFFFF0)):
            looks([(0x22, 1, 2, None)], height=height, screen_x=x, screen_y=y)
    return lines


def fnv(name):
    h = 0xCBF29CE484222325
    for b in name:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def weapons(oracle):
    """CPc::SetWeapon (vf20, 005464b0) called with both words, as the game mode's handlers call it: the
    weapon it keeps and layers 5 and 6's names, hashed, by job, sex, the two hands' ids and
    whether the item's own sprite is there."""
    emu, actor = oracle.emu, oracle.actor
    oracle.looking, oracle.resources = True, {}
    emu.write_u32(path_oracle.ADMINS_B + 4, 0)
    emu.write_u32(path_oracle.ADMINS_B + 8, 0)
    lows = (0, 1100, 1149, 1150, 1200, 1249, 1250, 1300, 1349, 1350, 1400, 0x63F, 5)
    highs = (0, 1100, 1149, 1150, 1200, 1250, 1300, 1349, 1350, 1600, 0x835, 5)
    rows = [(job, 1) for job in (0, 6, 8, 0xC, 0x11)] + [(0xC, 0)]
    lines = []
    for job, sex in rows:
        for low in lows:
            for high in highs:
                for exists in (0, 1):
                    emu.mem_write(actor, bytes(path_oracle.ACTOR_SIZE))
                    emu.write_u32(actor, path_oracle.PC_VTABLE)
                    emu.write_u32(actor + 0x1B8, job)
                    emu.write_u32(actor + 0x1BC, sex)
                    for offset in (path_oracle.LAYER_SPRITES, path_oracle.LAYER_ACTS):
                        layers = emu.scratch(size=4 * path_oracle.LAYER_SLOTS)
                        emu.write_u32(actor + offset + 4, layers)
                        emu.write_u32(actor + offset + 8, layers + 4 * path_oracle.LAYER_SLOTS)
                    oracle.emblem_exists = exists
                    weapon = high << 16 | low
                    emu.call(0x5464B0, weapon, this=actor)
                    names = []
                    for layer in (5, 6):
                        for offset in (path_oracle.LAYER_ACTS, path_oracle.LAYER_SPRITES):
                            res = emu.read_u32(emu.read_u32(actor + offset + 4) + 4 * layer)
                            name = emu.read_cstr(res + 0x14, encoding="latin-1") if res else ""
                            names.append(fnv(name.encode("latin-1")))
                    lines.append(f"    {{{job}, {sex}, 0x{weapon:08x}u, {exists}, 0x{emu.read_u32(actor + 0x2A0):08x}u, "
                                 + ", ".join(f"0x{h:016x}ull" for h in names) + "},")
    oracle.looking = False
    return lines


def main():
    oracle = path_oracle.Oracle()
    maps = [("open", dict(path_vectors.MAPS)["open"], False), ("walls", dict(path_vectors.MAPS)["walls"], False),
            ("hills", walk_vectors.HILLS, True)]
    lines = ["// Generated by tools/look_vectors.py from the original in the emulation oracle."]
    lines.append("// The maps are tools/walk_vectors.py's (walk_vectors.inc's kWalkMaps). Each map's messages in")
    lines.append("// order (tools/walk_oracle.py's script and output), then chosen ones.")
    lines.append("static const StateQueryVector kLookQueries[] = {")
    for m, (name, rows, hilly) in enumerate(maps):
        width, height = len(rows[0]), len(rows)
        attrs = [path_vectors.ATTRIBUTES[c] for row in rows for c in row]
        script = walk_oracle.make_looks(name, width, height, attrs, 12, updates=(4, 12, 30))
        if name == "open":
            script += deciding(script, width, height)
        for query, result in zip(script, oracle.run(walk_vectors.gat(rows, hilly), script)):
            lines.append(f"    {{{m}, \"{query}\", \"{result}\"}},")
    lines.append("};")
    lines.append("")
    lines.append("// CPc::SetWeapon (vf20) with both words: job, sex, the word, the own sprite there, the weapon kept and the")
    lines.append("// FNV-1a of layer 5's actions and sprite, then layer 6's.")
    lines.append("static const WeaponItemVector kWeaponItems[] = {")
    lines += weapons(oracle)
    lines.append("};")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
