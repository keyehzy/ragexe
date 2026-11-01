"""Test vectors for the own player's whole update (world::UpdateOwnPlayer, src/world/actor_update.h:
CPlayer::Update, and world::PursueTarget, src/world/player_pursuit.h), taken from the original in
the emulation oracle.

usage: .venv/bin/python -I tools/own_update_vectors.py > tests/unit/own_update_vectors.inc

On tools/walk_vectors.py's three small maps, scripts as tools/walk_oracle.py's make_own_updates
makes them (o), then on the open field and among the walls updates whose results each hang on
one thing: PreMove about half the ping; each pursuit with its target found or not; the attack's
range at its edge, a big model's 20 off and its floor, an unordered distance; the times about
1200, 450 and 1200 ms; a single attack; the states and job 0x10 that pursue; the cast's aim
beside a big model in each sector; an item at each distance and a step into a wall; the items
asked for; the cast on the player itself, on a cell, on a skill's object attacked or not; a
queued move as an attack ends; the goal forgotten as the player stands.
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
UPDATE_FIELDS = path_oracle.UPDATE_FIELDS
OWN_FIELDS = path_oracle.OWN_FIELDS


def bits(f):
    return struct.unpack("<I", struct.pack("<f", f))[0]


def f32(f):
    return struct.unpack("<f", struct.pack("<f", f))[0]


def step(f, k):
    """The float k floats from f, toward +∞ for k > 0."""
    if k == 0 or f != f:
        return f
    b = struct.unpack("<i", struct.pack("<f", f))[0]
    b += k if f >= 0 else -k
    return struct.unpack("<f", struct.pack("<i", b))[0]


def deciding(script, width, height, walls):
    """Updates made from the first o line of a generated script, the player standing at the
    centre of cell (px, py)."""
    base = next(q for q in script if q[0] == "o").split()[1:]
    n = PLAYER_FIELDS + len(UPDATE_FIELDS)
    server, tick = (int(base[n + len(OWN_FIELDS) + k]) for k in (0, 1))
    lines = []

    def centre(cx, cy):
        return f32((cx - width // 2) * 5 + 2.5), f32((cy - height // 2) * 5 + 2.5)

    def own(at=(5, 4), state=0, job=7, finished=0, play=0, updates=1, step=100, **o):
        x, z = centre(*at)
        f = base[:PLAYER_FIELDS]
        f[0], f[1] = f"0x{bits(x):08x}", f"0x{bits(z):08x}"
        f[7], f[8], f[17], f[41], f[36] = str(job), "0", str(state), "0", str(finished)
        f[11], f[12], f[14], f[19] = "0", "0", "0", "0"
        u = dict(zip(UPDATE_FIELDS, base[PLAYER_FIELDS:n]))
        u.update(f230=0, keyed=0, effects_on=0, body=0, cast=0, gauge=0, f1f4=0, events=0, play=play, start=tick)
        values = dict(goal_x=-1, goal_y=-1, acted_at=tick - 5000, premove_at=tick, pursuit=0, premove=0,
                      attack_mode=0, acted=0, queued=0, target=110001, acts=0, asked_at=tick - 5000, skill=0,
                      range=1, cell_x=at[0], cell_y=at[1], queued_x=-1, queued_y=-1, ping=0, attack_range=1,
                      own=7, found=7, tx=x + 50.0, ty=0.0, tz=z, tstate=0, tcx=at[0] + 10, tcy=at[1], tmodel=0,
                      tjob=0x7E, teffect=0, listed=0)
        values.update(o)
        for k in ("tx", "ty", "tz"):
            values[k] = f"0x{bits(f32(values[k])):08x}"
        for k in ("acted_at", "premove_at", "asked_at"):
            values[k] &= 0xFFFFFFFF
        ordered = [str(u[k] & 0xFFFFFFFF) if isinstance(u[k], int) else u[k] for k in UPDATE_FIELDS]
        ordered += [v if isinstance(v, str) else str(v & 0xFFFFFFFF) for v in (values[k] for k in OWN_FIELDS)]
        times = []
        for k in range(updates):
            times += [(server + step * k) & 0xFFFFFFFF, (tick + step * k) & 0xFFFFFFFF]
        lines.append("o " + " ".join(f + ordered + [str(t) for t in times]))

    x0, z0 = centre(5, 4)
    if not walls:
        # PreMove about half the ping, odd and even, and its wrap.
        for ping in (0, 1, 300, 301, 0xFFFFFFFF):
            for late in (-1, 0, 1):
                own(premove=1, ping=ping, premove_at=tick - (ping >> 1) - late + 1, goal_x=3, goal_y=2)
        own(premove=0, ping=0, premove_at=tick - 100)
        own(premove=1, ping=0, premove_at=0x7FFFFFFF + tick)
        # Each pursuit, its target found or not.
        for pursuit in (0, 1, 2, 3, 4, 5, 6, 7, -1):
            for found in (0, 1, 2, 4, 7):
                own(pursuit=pursuit, found=found, updates=2)
        # The attack's range at its edge (5.5 past range × 5 units, wrapping), a float either side,
        # with a big model's 20 off and its floor at 0, and the distance unordered or infinite; x
        # apart, y apart, z apart.
        for rng, model in ((1, 0), (2, 0), (0, 0), (-1, 0), (1, 1), (14, 0), (0x7FFFFFFF, 0), (-2, 0), (-2, 1),
                           (0x1999999A, 0), (0x1999999A, 1)):
            units = (rng * 5) & 0xFFFFFFFF
            limit = f32(units - (1 << 32 if units >> 31 else 0) + 5.5)
            reach = f32(limit + (20 if model else 0))
            for k in (-1, 0, 1):
                for pursuit in (1, 5):
                    own(pursuit=pursuit, attack_range=rng, tmodel=model, tx=step(f32(x0 + reach), k), tz=z0)
                own(pursuit=3, range=rng, tmodel=model, tx=step(f32(x0 + reach), k), tz=z0)
            for d in (3.0, 0.0, 30.0):
                for pursuit in (1, 5):
                    own(pursuit=pursuit, attack_range=rng, tmodel=model, tx=x0 + d, tz=z0)
                own(pursuit=3, range=rng, tmodel=model, tx=x0 + d, tz=z0)
            own(pursuit=1, attack_range=rng, tmodel=model, tx=x0, tz=step(f32(z0 - reach), 1), ty=0.0)
            own(pursuit=4, range=rng, cell_x=5 + 2 * max(min(rng, 3), 0), cell_y=4)
            own(pursuit=4, range=rng, cell_x=9, cell_y=7)
            own(pursuit=6, range=rng, tx=x0 + 30.0)
        for t in (float("nan"), float("inf"), -float("inf")):
            for model in (0, 1):
                for rng in (1, -2):
                    own(pursuit=1, attack_range=rng, tmodel=model, tx=t, tz=z0)
                    own(pursuit=3, range=rng, tmodel=model, tx=t, tz=z0)
        for ty in (4.0, 10.0, 11.0):
            own(pursuit=1, tmodel=1, tx=x0 + 15.0, tz=z0, ty=ty)
            own(pursuit=1, tx=x0 + 3.0, ty=ty)
        own(pursuit=1, tmodel=1, tx=x0 + 3.0, tz=z0 + 4.0)
        # Where the squares' sum rounded to a float decides the range (10.5), and where the
        # products rounded to floats decide the free cell.
        for tx, tz in ((-4.712126731872559, -12.764331817626953), (-4.841893196105957, 7.735503673553467),
                       (-12.9944429397583, -2.158418655395508), (-5.56308126449585, 7.543282985687256)):
            own(pursuit=1, tx=tx, tz=tz)
            own(pursuit=3, tx=tx, tz=tz)
        for tx, tz, steps in ((24.999921798706055, -2.3491029739379883, 1), (-15.0, -2.5042614936828613, 1),
                              (29.999996185302734, -2.5132739543914795, 2),
                              (-29.999990463256836, -2.556692123413086, 1)):
            own(pursuit=5, attack_range=steps, tx=tx, tz=tz)
            own(pursuit=3, range=steps, tx=tx, tz=tz)
        # The attack: at its goal, acted, single, about 1200 ms since it acted, wrapping.
        for goal in ((5, 4), (6, 4), (5, 5)):
            own(pursuit=1, goal_x=goal[0], goal_y=goal[1], updates=3, step=700)
        for acted_at in (1199, 1200, 1201, 0, -1, 0x7FFFFFFF):
            for mode in (0, 1, 2):
                own(pursuit=1, tx=x0 + 3.0, acted_at=tick - acted_at, attack_mode=mode)
        own(pursuit=1, tx=x0 + 3.0, acted=1)
        own(pursuit=5, tx=x0 + 3.0, acts=0x7FFFFFFF)
        own(pursuit=1, tx=x0 + 3.0, acted_at=tick - 1201, updates=4, step=400)
        # Asking to walk: 450 ms, or 1200 for a faster target, since the last ask; the target's
        # cell or where it stands; the same cell asked again; attacking, acted.
        for pursuit in (1, 5):
            for asked in (449, 450, 451, 1199, 1200, 1201):
                own(pursuit=pursuit, asked_at=tick - asked, tx=x0 + 30.0, tcx=12, tcy=1)
            own(pursuit=pursuit, tx=x0 + 30.0, tz=z0 + 7.0, tcx=8, tcy=7, updates=3, step=500)
            own(pursuit=pursuit, tx=x0 + 30.0, acted=1)
        own(pursuit=1, tx=x0 + 30.0, tstate=3)
        own(pursuit=1, tx=x0 + 3.0, tstate=3)
        own(pursuit=1, tx=x0 + 30.0, tstate=2)
        # The states that pursue, and job 0x10 in any.
        for state in (0, 2, 4, 5, 6, 8, 12, 13, 0x10, 0x11):
            for job in (7, 0x10):
                own(pursuit=1, state=state, job=job, tx=x0 + 3.0)
                own(pursuit=1, state=state, job=job, tx=x0 + 30.0)
            own(pursuit=3, state=state, tx=x0 + 30.0)
            own(pursuit=3, state=state, tx=x0 + 3.0)
            own(pursuit=4, state=state, cell_x=9)
            own(pursuit=4, state=state, cell_x=5)
            own(pursuit=6, state=state, tx=x0 + 30.0)
            own(pursuit=6, state=state, tx=x0 + 3.0)
            own(pursuit=2, state=state, tx=x0 + 30.0)
            own(pursuit=2, state=state, tx=x0 + 5.0)
        # The cast's aim beside a big model at each sector's edge, a float either side (25° falls
        # between two floats), and all around.
        for tx, tz in ((53.881507873535156, -23.021207809448242), (51.878509521484375, 22.85709571838379),
                       (18.021238327026367, 53.88155746459961), (-27.85713768005371, 51.87846755981445),
                       (-58.88164138793945, 18.021207809448242), (-56.878475189208984, -27.85709571838379),
                       (-23.021221160888672, -58.88155746459961)):
            for k in (-1, 0, 1):
                own(pursuit=3, tmodel=1, tx=step(f32(tx), k), tz=tz)
        for deg in list(range(0, 360, 15)) + [24.9, 25.1, 69.9, 70.1, 114.9, 115.1, 159.9, 160.1, 204.9, 205.1,
                                              249.9, 250.1, 294.9, 295.1, 339.9, 340.1]:
            a = math.radians(deg)
            for model in (0, 1, 2):
                own(pursuit=3, tmodel=model, tx=x0 + 60.0 * math.sin(a), tz=z0 - 60.0 * math.cos(a))
            own(pursuit=1, tmodel=1, tx=x0 + 40.0 * math.sin(a), tz=z0 - 40.0 * math.cos(a), tcx=5, tcy=4)
        # The cast on the player itself, on another found or not, at its goal.
        for target, own_id, found in ((7, 7, 0), (7, 7, 7), (7, 8, 7), (7, 8, 0)):
            own(pursuit=3, target=target, own=own_id, found=found, tx=x0 + 30.0)
        own(pursuit=3, tx=x0 + 30.0, goal_x=5, goal_y=4)
        own(pursuit=3, tx=x0 + 30.0, updates=3, step=300)
        # The cast on a cell: in range or not, at its goal, the cell asked again.
        for cell in ((5, 4), (6, 4), (7, 4), (8, 5), (11, 9), (0, 0), (-3, 4), (5, 40)):
            for rng in (0, 1, 2):
                own(pursuit=4, cell_x=cell[0], cell_y=cell[1], range=rng, updates=2)
        own(pursuit=4, cell_x=9, cell_y=4, goal_x=5, goal_y=4)
        # A skill's object: attacked (jobs 0x8d, 0x8f, 0x98) without a skill, or cast on.
        for job in (0x8C, 0x8D, 0x8E, 0x8F, 0x97, 0x98, 0x99, 7):
            for skill in (0, 1):
                own(pursuit=6, tjob=job, skill=skill, tx=x0 + 3.0)
                own(pursuit=6, tjob=job, skill=skill, tx=x0 + 30.0, state=2)
            own(pursuit=6, tjob=job, teffect=0x20, tx=x0 + 3.0)
        for d in (11.5, -11.5, 9.0, -9.0):
            own(pursuit=6, tx=x0 + d)
        own(pursuit=6, tx=x0 + 30.0, goal_x=5, goal_y=4)
        own(pursuit=6, tx=x0 + 30.0, updates=3, step=300)
        # An item: on the player's cell, each cell around and two away, the items asked for.
        for dx in (-2, -1, 0, 1, 2):
            for dy in (-2, -1, 0, 1, 2):
                own(pursuit=2, tx=x0 + 5.0 * dx, tz=z0 + 5.0 * dy)
        for listed in (0, 1, 2):
            own(pursuit=2, tx=x0 + 5.0, listed=listed, updates=3)
        own(pursuit=2, tx=x0 + 15.0, goal_x=6, goal_y=4)
        own(pursuit=2, tx=x0 + 15.0, updates=3, step=300)
        # A move queued as an attack, a flinch or a fall ends, and the goal forgotten as the
        # player stands.
        for state in (2, 5, 6, 7, 0):
            for queued in (0, 1):
                own(state=state, finished=1, play=1, queued=queued, queued_x=3, queued_y=7, goal_x=4, goal_y=4,
                    updates=2)
        own(state=13, queued=1, queued_x=3, queued_y=7, goal_x=4, goal_y=4, updates=2)
    else:
        # Among the walls: the free cell toward the target blocked, or off the map, and the item's
        # step into a wall.
        for at in ((4, 3), (3, 2), (6, 5), (8, 1), (0, 0), (9, 7)):
            ax, az = centre(*at)
            for tx, tz in ((ax + 30.0, az), (ax - 30.0, az), (ax, az + 30.0), (ax, az - 30.0), (ax + 80.0, az + 80.0),
                           (ax - 200.0, az), (ax + 9.0, az + 9.0)):
                for rng in (1, 2):
                    own(at=at, pursuit=1, attack_range=rng, tx=tx, tz=tz, tcx=at[0] + 3, tcy=at[1])
                    own(at=at, pursuit=5, attack_range=rng, tx=tx, tz=tz)
                    own(at=at, pursuit=3, range=rng, tx=tx, tz=tz)
            for dx in (-2, -1, 1, 2):
                for dy in (-2, -1, 0, 1, 2):
                    own(at=at, pursuit=2, tx=ax + 5.0 * dx, tz=az + 5.0 * dy)
            for cell in ((1, 1), (2, 4), (7, 4), (4, 7), (12, 3)):
                own(at=at, pursuit=4, cell_x=cell[0], cell_y=cell[1])
                own(at=at, pursuit=6, tx=centre(*cell)[0], tz=centre(*cell)[1], range=2)
    return lines


def main():
    oracle = path_oracle.Oracle()
    maps = [("open", dict(path_vectors.MAPS)["open"], False), ("walls", dict(path_vectors.MAPS)["walls"], False),
            ("hills", walk_vectors.HILLS, True)]
    lines = ["// Generated by tools/own_update_vectors.py from the original in the emulation oracle."]
    lines.append("// The maps are tools/walk_vectors.py's (walk_vectors.inc's kWalkMaps). Each map's updates in")
    lines.append("// order (tools/walk_oracle.py's script and output), then chosen ones.")
    lines.append("static const StateQueryVector kOwnUpdateQueries[] = {")
    for m, (name, rows, hilly) in enumerate(maps):
        width, height = len(rows[0]), len(rows)
        attrs = [path_vectors.ATTRIBUTES[c] for row in rows for c in row]
        script = walk_oracle.make_own_updates(name, width, height, attrs, 12, updates=(4, 12, 30))
        if name in ("open", "walls"):
            script += deciding(script, width, height, name == "walls")
        for query, result in zip(script, oracle.run(walk_vectors.gat(rows, hilly), script)):
            lines.append(f"    {{{m}, \"{query}\", \"{result}\"}},")
    lines.append("};")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
