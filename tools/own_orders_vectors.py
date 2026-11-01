"""Test vectors for the own player's messages (world::SendOwnPlayerMessage, src/world/player_orders.h:
CPlayer::SendMsg), taken from the original in the emulation oracle.

usage: .venv/bin/python -I tools/own_orders_vectors.py > tests/unit/own_orders_vectors.inc

On tools/walk_vectors.py's three small maps, scripts as tools/walk_oracle.py's make_orders makes
them (m), then on the open field and among the walls messages whose results each hang on one
thing: each message in each state, hiding or not; the attack's pursuit about four thirds of the
player's speed, wrapping; 0xf by pursuit, +0x320 and +0x334; the look's sectors at their edges,
the head before, the angles 45° and 315° exactly and a negative direction; a walk queued or
asked; the walk
asked about its second; the jobs 0x1d and 0x28 act on as given and a target's +0x1f4; 0x29 while
hiding; 0x3c's pursuit and +0x338 wrapping; 0x42's window; moves found from the order, from the
player's cell or not at all, at speeds past 2^24 (the path's times rounded once), a move that
leaves the state as it was and the queued move SetState then sends.
"""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import path_oracle
import path_vectors
import walk_oracle
import walk_vectors

PLAYER_FIELDS = 44
OWN_FIELDS = path_oracle.OWN_FIELDS
ORDER_FIELDS = path_oracle.ORDER_FIELDS


def bits(f):
    return struct.unpack("<I", struct.pack("<f", f))[0]


def f32(f):
    return struct.unpack("<f", struct.pack("<f", f))[0]


def deciding(script, width, height, walls):
    """Messages made from the first m line of a generated script, the player standing at the
    centre of cell (px, py)."""
    base = next(q for q in script if q[0] == "m").split()[1:]
    n = PLAYER_FIELDS + len(OWN_FIELDS) + len(ORDER_FIELDS)
    server, tick = (int(base[n + 1 + k]) for k in (11, 12))
    lines = []

    def centre(cx, cy):
        return f32((cx - width // 2) * 5 + 2.5), f32((cy - height // 2) * 5 + 2.5)

    def orders(messages, at=(5, 4), state=0, effect=0, facing=0.0, head=0, f_d0=0, segment=0, xz=None, **o):
        """messages: (id, a, b, c, words, step) each, the words and step optional; xz the player's
        position as bits, otherwise the centre of `at`."""
        x, z = centre(*at)
        f = base[:PLAYER_FIELDS]
        f[0], f[1], f[2] = f"0x{bits(x):08x}", f"0x{bits(z):08x}", f"0x{bits(f32(facing)):08x}"
        if xz:
            f[0], f[1] = (f"0x{v:08x}" for v in xz)
        f[7], f[8], f[17], f[19], f[38], f[40] = "7", str(effect), str(state), str(f_d0), str(segment), str(head)
        f[11], f[12], f[14], f[41] = "0", "0", "0", "0"
        values = dict(goal_x=-1, goal_y=-1, acted_at=tick - 5000, premove_at=tick, pursuit=0, premove=0,
                      attack_mode=0, acted=0, queued=0, target=110001, acts=0, asked_at=tick - 5000, skill=0,
                      range=1, cell_x=at[0], cell_y=at[1], queued_x=-1, queued_y=-1, ping=0, attack_range=1,
                      own=7, found=7, tx=x + 50.0, ty=0.0, tz=z, tstate=0, tcx=at[0] + 10, tcy=at[1], tmodel=0,
                      tjob=0x7E, teffect=0, listed=0, walk_asked=0, walk_asked_at=tick - 5000, level=1,
                      own_speed=150, skill_range=5, tspeed=150, t1f4=0)
        values.update(o)
        for k in ("tx", "ty", "tz"):
            values[k] = f"0x{bits(f32(values[k])):08x}"
        fields = [v if isinstance(v, str) else str(v & 0xFFFFFFFF)
                  for v in (values[k] for k in OWN_FIELDS + ORDER_FIELDS)]
        script, s, t = [], server, tick
        for m in messages:
            message, a, b, c = m[:4]
            words = (m[4] if len(m) > 4 else None) or [0] * 7
            gap = m[5] if len(m) > 5 else 0
            s, t = (s + gap) & 0xFFFFFFFF, (t + gap) & 0xFFFFFFFF
            script += [message, a, b, c, *words, s, t]
        lines.append("m " + " ".join(f + fields + [str(len(messages))] + [str(v & 0xFFFFFFFF) for v in script]))

    def order(sx, sy, gx, gy, late=0, sub=(8, 8)):
        return [(server - late) & 0xFFFFFFFF, sx, sy, sub[0], sub[1], gx, gy]

    x0, z0 = centre(5, 4)
    ids = (0, 8, 9, 0xA, 0xD, 0xF, 0x10, 0x11, 0x12, 0x1D, 0x1E, 0x1F, 0x28, 0x29, 0x2A, 0x3C, 0x41, 0x42,
           0x5A, 0x5B, 0x67)
    if not walls:
        # Each message in each state, hiding (bit 1 of +0x21c) or not.
        for state in (0, 1, 2, 3, 4, 5, 6, 7, 8, 13):
            for effect in (0, 2, 0x20):
                for message in ids:
                    words = order(5, 4, 8, 4) if message == 0 else [9, 0x7D, 2, 3, 0, 0, 0]
                    orders([(message, 110002, 0x89 if message == 0x29 else 7, 2, words)], state=state,
                           effect=effect, pursuit=1, attack_mode=1, acts=1, skill=0x7D)
        # The attack's pursuit about four thirds of the player's speed: the limit wraps (× 4) and
        # is divided signed, the target's speed compared unsigned; the target found or not.
        for speed in (150, 3, 2, 1, 0, -3, -1, 0x40000000, 0x60000000, 0x7FFFFFFF):
            word = (speed * 4) & 0xFFFFFFFF
            limit = int((word - (1 << 32 if word >> 31 else 0)) / 3)
            for tspeed in (limit - 1, limit, limit + 1, 0, -1):
                orders([(0xA, 110002, 2, 0)], own_speed=speed, tspeed=tspeed)
        for found in (0, 1, 6):
            orders([(0xA, 110002, 1, 0)], found=found)
        # 0xf by pursuit, +0x320 and +0x334.
        for pursuit in (0, 1, 2, 3, 4, 5, 6):
            for mode in (0, 1, 2):
                for acts in (0, 1, -1):
                    orders([(0xF, 0, 0, 0)], pursuit=pursuit, attack_mode=mode, acts=acts)
        # The look: the cell straight ahead (5, 3) is at 0°, so the turn (int)(facing - angle) is the
        # facing truncated: each sector's edge, below 0 and the limits past which no sector is
        # left once wrapped, each head before. Then the cells all around at each sector's middle;
        # 315° and 45° put the wraps of 2 and 3, 5 and 6 at their edges.
        for turn in (0.0, 22.0, 22.9, 23.0, 67.0, 68.0, 112.0, 113.0, 157.0, 158.0, 202.0, 203.0, 247.0, 248.0,
                     292.0, 293.0, 337.0, 338.0, 359.0, -0.5, -1.0, -22.0, -23.0, -67.0, -68.0, -338.0, -383.0,
                     -384.0, -787.0, -788.0, 697.0, 698.0, 1000.0):
            for head in (0, 1, 2):
                orders([(0x10, 5, 3, 0)], facing=turn, head=head)
        for cell in ((6, 4), (4, 4), (5, 5), (5, 3), (6, 5), (4, 3), (6, 3), (4, 5), (5, 4), (9, 2)):
            dx, dy = (cell[0] - 5) * 5.0, -(cell[1] - 4) * 5.0
            angle = math.degrees(math.atan2(dx, dy)) % 360.0
            for sector in range(8):
                for head in (0, 1, 2):
                    orders([(0x10, cell[0], cell[1], 0)], facing=angle + 45.0 * sector, head=head)
        # Where the angle is exactly 45° and 315° (π is 0x40490fd8, a little short), so 5 and 6
        # reach 0 and 2 and 3 reach 360.
        for xz, cell, angle in (((0xC0200126, 0xC01FFED5), (6, 3), 45.0), ((0xC01FFFFC, 0xC0200000), (4, 3), 315.0)):
            for sector in range(8):
                for head in (0, 1, 2):
                    orders([(0x10, cell[0], cell[1], 0)], xz=xz, facing=angle + 45.0 * sector, head=head)
        for facing in (-200.0, -300.0, -700.0, 300.0, 700.0):
            for head in (0, 1, 2):
                orders([(0x10, 5, 9, 0), (0x10, 5, 9, 0)], facing=facing, head=head)
        for state in (0, 1, 2, 3, 4, 5, 6, 8):
            orders([(0x10, 6, 4, 0)], state=state, facing=180.0)
        # A walk: queued while attacking, flinching or falling, otherwise asked; asked again about
        # its second, wrapping.
        for state in (0, 1, 2, 4, 5, 6, 7):
            orders([(0x11, 8, 2, 0), (0x11, 3, 3, 0, None, 500)], state=state, pursuit=1, walk_asked=1)
        for asked in (0, 999, 1000, 1001, -1, 0x7FFFFFFF):
            for walk_asked in (0, 1, 2):
                orders([(0x5B, 8, 2, 0)], walk_asked=walk_asked, walk_asked_at=tick - asked)
        orders([(0x5B, 8, 2, 0), (0x5B, 8, 3, 0, None, 1000), (0x5B, 8, 4, 0, None, 1), (0x5B, 8, 5, 0, None, 1000)])
        # The jobs 0x1d and 0x28 act on as given; a target's +0x1f4, found or not.
        for message in (0x1D, 0x28):
            for job in (0x8C, 0x8D, 0x8E, 0x8F, 0x97, 0x98, 0x99, 0):
                orders([(message, 110002, job, 0)])
            for t1f4 in (0x14, 0x13, 0):
                for found in (0, 1):
                    orders([(message, 110002, 0, 0)], t1f4=t1f4, found=found)
        # 0x29 while hiding but for 0x89; 0x3c's pursuit and its +0x338 before 1200 ms.
        for skill in (0x88, 0x89, 0x8A):
            for effect in (0, 2):
                orders([(0x29, 110002, skill, 3)], effect=effect)
        for pursuit in (0, 1, 5, 3):
            orders([(0x3C, 110002, 0, 0)], pursuit=pursuit)
        orders([(0x3C, 110002, 0, 0)], pursuit=1)
        # 0x42 by skill (0x7d asks window 0x54); 0x41's range; 0x2a, 0x5a and 0x67 in turn.
        for skill in (0x7C, 0x7D, 0x7E, 0):
            orders([(0x42, 0, 0, 0)], skill=skill, cell_x=3, cell_y=9, level=4)
            orders([(0x41, skill, 2, 6), (0x42, 0, 0, 0)], skill_range=7)
        orders([(0x5A, 9, 0, 0), (0x2A, 0, 0, 0), (0x67, 110003, 0, 0, [1, 0x7D, 4, 6, 0, 0, 0]), (0x2A, 0, 0, 0),
                (0x42, 0, 0, 0)])
        # Moves: found from the order, at speeds about and past 2^24 (the path's times rounded
        # once), late; the order's start off the map, so from the player's cell, keeping the
        # segment; a goal no one reaches; a move that leaves the state as it was (+0xd0) and the
        # queued move SetState sends then.
        for speed in (150, 1, 0, 16777215, 16777217, 33554435, 0x7FFFFFFF, -1):
            orders([(0, 0, 0, 0, order(5, 4, 9, 7))], own_speed=speed)
            orders([(0, 0, 0, 0, order(5, 4, 1, 4))], own_speed=speed)
        for late in (0, 200, 5000, -300):
            orders([(0, 0, 0, 0, order(5, 4, 9, 4, late))])
        for start in ((-3, 4), (5, 40), (5, 4), (6, 4)):
            for segment in (0, 3):
                orders([(0, 0, 0, 0, order(start[0], start[1], 8, 6))], segment=segment, premove=1, walk_asked=1)
        orders([(0, 0, 0, 0, order(5, 4, 5, 4))])
        orders([(0, 0, 0, 0, order(5, 4, -9, 4))])
        for state in (0, 1, 2, 5, 6, 3):
            for f_d0 in (0, 1):
                for message, words in ((0, order(5, 4, 9, 7)), (8, None)):
                    orders([(message, 0, 0, 0, words or [0] * 7)], state=state, f_d0=f_d0, queued=1, queued_x=2,
                           queued_y=6)
    else:
        # Among the walls: moves whose order starts in a wall, toward walls, around them.
        for at in ((4, 3), (3, 2), (6, 5), (8, 1), (0, 0), (9, 7)):
            for goal in ((1, 1), (2, 4), (7, 4), (4, 7), (12, 3), (9, 7)):
                orders([(0, 0, 0, 0, order(at[0], at[1], *goal))], at=at)
                orders([(0, 0, 0, 0, order(3, 3, *goal))], at=at)
            orders([(0x10, at[0] + 1, at[1] + 1, 0)], at=at, facing=90.0)
    return lines


def main():
    oracle = path_oracle.Oracle()
    maps = [("open", dict(path_vectors.MAPS)["open"], False), ("walls", dict(path_vectors.MAPS)["walls"], False),
            ("hills", walk_vectors.HILLS, True)]
    lines = ["// Generated by tools/own_orders_vectors.py from the original in the emulation oracle."]
    lines.append("// The maps are tools/walk_vectors.py's (walk_vectors.inc's kWalkMaps). Each map's messages in")
    lines.append("// order (tools/walk_oracle.py's script and output), then chosen ones.")
    lines.append("static const StateQueryVector kOwnOrderQueries[] = {")
    for m, (name, rows, hilly) in enumerate(maps):
        width, height = len(rows[0]), len(rows)
        attrs = [path_vectors.ATTRIBUTES[c] for row in rows for c in row]
        script = walk_oracle.make_orders(name, width, height, attrs, 12, updates=(4, 12, 30))
        if name in ("open", "walls"):
            script += deciding(script, width, height, name == "walls")
        for query, result in zip(script, oracle.run(walk_vectors.gat(rows, hilly), script)):
            lines.append(f"    {{{m}, \"{query}\", \"{result}\"}},")
    lines.append("};")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
