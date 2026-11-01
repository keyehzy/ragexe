"""Test vectors for the actors' whole update (world::UpdateNpc and world::UpdatePlayer,
src/world/actor_update.h: CNpc::Update and CPc::Update), taken from the original in the emulation
oracle.

usage: .venv/bin/python -I tools/update_vectors.py > tests/unit/update_vectors.inc

On tools/walk_vectors.py's three small maps, scripts as tools/walk_oracle.py's make_updates
makes them (u and p), then on the open field updates whose results each hang on one thing: the
NPC's way by job, about each edge of its jump table; Sight's effect by job, mount, period and
effects on; the key colour about its 100 ms; the colour brightened, white or not, tinted or
not; the effect a job 0x68 NPC repeats about its period; the frames' sounds by each event,
before and after a new action, past the last frame and from frame 0 again, of an action the act
lacks; the player's gauge
by the screen's height and +0x48; a cast's gauge ending in state 8; the fade.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import path_oracle
import path_vectors
import walk_oracle
import walk_vectors

FIELDS = path_oracle.UPDATE_FIELDS


def bits(f):
    return struct.unpack("<I", struct.pack("<f", f))[0]


def deciding(script):
    """Updates made from the first u and p lines of a generated script."""
    bases = {k: next(q for q in script if q[0] == k).split() for k in "up"}
    lines = []

    def update(player=False, job=None, state=0, effect=0, frames=4, color=0xFFFFFFFF, speed=150, base=0,
               f230=0, keyed=0, keyed_at=-1000, key=0xFFFF0000, tinted=0, f1ec=0, f290=-100000, f1f4=0, body=0,
               effects_on=1, events=0, f2c=None, f30=0, frame=0, px=0.0, pz=0.0, gauge=0, sx=320, sy=240,
               pixels=0.5, height=480, cast=0, end=10000, begin=0, f0cc=0, f1bc=0, weapon=0, play=0, updates=4,
               step=100, fade=0, action=0):
        tokens = bases["p" if player else "u"]
        n = 44 if player else 39
        f = tokens[1:1 + n]
        f[7] = str(job if job is not None else (7 if player else 1100))
        f[6], f[8], f[10], f[13] = str(base), str(effect), str(frames), str(color & 0xFFFFFFFF)
        f[17], f[18] = str(state), str(speed & 0xFFFFFFFF)
        f[11], f[14], f[19] = "0", "0", "0"
        f[4] = f"0x{bits(4.0):08x}"
        if player:
            f[41] = "0"
        server, tick = (int(tokens[1 + n + len(FIELDS) + k]) for k in (0, 1))
        f[12] = str((tick + fade) & 0xFFFFFFFF) if fade else "0"
        x, z = (struct.unpack("<f", struct.pack("<I", int(f[k], 0) & 0xFFFFFFFF))[0] for k in (0, 1))
        values = [f230, keyed, tick + keyed_at, key, tinted, f1ec, tick + f290, f1f4, body, effects_on, events,
                  base if f2c is None else f2c, f30, frame, f"0x{bits(x + px):08x}", f"0x{bits(z + pz):08x}", gauge,
                  sx, sy, f"0x{bits(pixels):08x}", height, cast, tick + end, tick + begin, f0cc, f1bc, weapon, play,
                  tick, action]
        values = [v if isinstance(v, str) else str(v & 0xFFFFFFFF) for v in values]
        times = []
        for k in range(updates):
            times += [(server + step * k) & 0xFFFFFFFF, (tick + step * k) & 0xFFFFFFFF]
        lines.append(("p " if player else "u ") + " ".join(f + values + [str(t) for t in times]))

    # The NPC's way by job, about the jump table's edges, with everything each way may do: the
    # stone's hold, Sight due, the colour off white, a cast's gauge, sounds and +0x1f4.
    for job in (0x2C, 0x2D, 0x2E, 0x67, 0x68, 0x69, 0x6E, 0x6F, 0x70, 0x8A, 0x8B, 0x8C, 1100, -1):
        for f1f4 in (0, 20):
            update(job=job, body=1, effect=1, f230=270, color=0x80808080, cast=1, events=0o1111, f1f4=f1f4,
                   frames=4, f30=0, frame=0, f1ec=0, speed=0x23)
    # Sight: none, set or not, by job and mount (bit 5), about each period, effects on or off,
    # and the message by the time.
    for effect in (0, 1, 2, 0x21, 0x20):
        for f230 in (0, 199, 200, 269, 270, 0x8CA0, 0xFFFFFFFF):
            update(effect=effect, f230=f230, job=3, updates=2)
    for player in (False, True):
        for job in (3, 4, 7, 8, 14, 15, 0x16):
            for effect in (1, 0x21):
                update(player=player, job=job, effect=effect, f230=199, updates=3)
        for effects_on in (0, 1):
            update(player=player, job=8, effect=1, f230=200, effects_on=effects_on, updates=1)
            update(player=player, job=3, effect=1, f230=270, effects_on=effects_on, updates=1)
        for step in (1, 16, 17, 160, 159):
            update(player=player, job=3, effect=1, f230=270, step=step, updates=1)
    # The key colour about its 100 ms, and the wrap of +0x6c + 100.
    for player in (False, True):
        for keyed_at in (-99, -100, -101, -200, 0, 1):
            for keyed in (0, 1):
                update(player=player, keyed=keyed, keyed_at=keyed_at, key=0x12345678, updates=1)
        update(player=player, keyed=1, keyed_at=-0x7FFFFFFF, updates=1)
    # The colour brightened: white or not, each channel near its edges, tinted or not.
    for player in (False, True):
        for color in (0xFFFFFFFF, 0xFFFFFFFE, 0x7FFFFFFF, 0, 0xF7F8F9FA, 0x80FF00FF, 0x01020304):
            for tinted in (0, 1):
                update(player=player, color=color, tinted=tinted, updates=2)
    # The stone's and the frozen's hold: an NPC's, never a player's.
    for player in (False, True):
        for body in (0, 1, 2, 3, -1):
            update(player=player, body=body, updates=1)
    # Job 0x68's effect about its period, the multiplication's wrap, own effects or not.
    for f1ec in (0, 1, 2, 0x7FFFFFFF, 4294968, -1):
        for f290 in (0, -999, -1000, -1001, -2000, -2001, 50):
            update(job=0x68, f1ec=f1ec, f290=f290 - 100, speed=0x23, updates=3)
    for speed in (0x16, 0x23, 0x7F, -1, 0xA6):
        for effects_on in (0, 1):
            update(job=0x68, f1ec=1, speed=speed, effects_on=effects_on, updates=1)
    # The frames' sounds: each event and name; a new action; the frame back past the last.
    for player in (False, True):
        for code in range(8):
            update(player=player, events=code * 0o111111111, frames=10, frame=3, f30=0, updates=1)
        for frame, f30 in ((2, 0), (0, 2), (2, 2), (3, 9), (3, 3), (0, 0), (7, 1), (1, 7), (5, 20), (2, -1),
                           (-1, 2), (9, 0)):
            for f2c in (None, 1):
                update(player=player, events=0o12345123, frames=6, frame=frame, f30=f30, f2c=f2c, updates=1)
        for frames in (0, 1, 2, 12):
            update(player=player, events=0o1111111111, frames=frames, frame=frames + 1, f30=0, updates=1)
        update(player=player, events=0o1111111111, frames=4, state=2, updates=6)
        update(player=player, events=0o1111111111, frames=4, px=float("inf"), updates=2)
        for base in (31, 32, 40):
            update(player=player, events=0o2222222222, frames=4, base=base, frame=2, f30=0, f2c=0, updates=1)
            update(player=player, events=0o2222222222, frames=4, base=base, frame=1, f30=3, updates=1)
    # +0x24 kept by the NPCs that update no state, about the act's actions.
    for action in (7, 255, 256, 257, -1):
        update(job=0x2D, events=0o2222222222, frames=4, action=action, frame=2, f30=0, updates=1)
        update(job=0x2D, events=0o2222222222, frames=4, action=action, frame=1, f30=3, updates=1)
    # The player's gauge, there or not, by the screen's height and +0x48 and the point on screen.
    for height in (480, 600, 0, 39, 40, 0x7FFFFFFF, 671088640, 1342177281):
        for pixels in (0.5, 1.0, -2.0, float("nan"), float("inf"), 1e30, 3.0000002):
            update(player=True, gauge=1, height=height, pixels=pixels, updates=1)
    for sx, sy in ((0, 0), (30, -30), (-0x80000000, 0x7FFFFFFF), (0x7FFFFFFF, 16777217)):
        update(player=True, gauge=1, sx=sx, sy=sy, pixels=0.75, updates=1)
    update(player=True, gauge=0, updates=1)
    # A cast's gauge closing past its end and standing state 8, before the state's update.
    for player in (False, True):
        for end in (50, 100, 101, 250):
            update(player=player, cast=1, state=8, end=end, updates=3)
    # The fade: over before the first update, and over on the way.
    for player in (False, True):
        for fade in (-1, 0, 150):
            update(player=player, fade=fade, events=0o1111, effect=1, f230=270, keyed=1, keyed_at=-200, updates=3)
    return lines


def main():
    oracle = path_oracle.Oracle()
    maps = [("open", dict(path_vectors.MAPS)["open"], False), ("walls", dict(path_vectors.MAPS)["walls"], False),
            ("hills", walk_vectors.HILLS, True)]
    lines = ["// Generated by tools/update_vectors.py from the original in the emulation oracle."]
    lines.append("// The maps are tools/walk_vectors.py's (walk_vectors.inc's kWalkMaps). Each map's updates in")
    lines.append("// order (tools/walk_oracle.py's script and output), then chosen ones.")
    lines.append("static const StateQueryVector kUpdateQueries[] = {")
    for m, (name, rows, hilly) in enumerate(maps):
        width, height = len(rows[0]), len(rows)
        attrs = [path_vectors.ATTRIBUTES[c] for row in rows for c in row]
        script = walk_oracle.make_updates(name, width, height, attrs, 12, updates=(4, 12, 30, 80))
        if name == "open":
            script += deciding(script)
        for query, result in zip(script, oracle.run(walk_vectors.gat(rows, hilly), script)):
            lines.append(f"    {{{m}, \"{query}\", \"{result}\"}},")
    lines.append("};")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
