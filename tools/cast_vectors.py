"""Test vectors for casting and the windows over actors (world::StartGameActorCast,
world::CloseGameActorWindow and world::StepGameActorWindows, src/world/actor_window.h, with
ProcessState's state 13), taken from the original in the emulation oracle.

usage: .venv/bin/python -I tools/cast_vectors.py > tests/unit/cast_vectors.inc

On tools/walk_vectors.py's three small maps, scripts as tools/walk_oracle.py's make_casts makes
them (C and R), then on the open field casts whose results each hang on one thing: every
message, with and without the window it makes or closes; the cast's duration by its edges and
the gauge's amount, a negative whole and the end reached by each of the two updates; state 13's
action by job, +0x1bc and weapon, +0xcc, and the states the end stands or not; the balloon by
its 5 s and each screen edge, its width halved; the titles; the rise by the screen's height and
+0x48.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import path_oracle
import path_vectors
import walk_oracle
import walk_vectors

FIELDS = path_oracle.CAST_FIELDS


def bits(f):
    return struct.unpack("<I", struct.pack("<f", f))[0]


def deciding(script):
    """Casts made from the first C and R lines of a generated script."""
    bases = {k: next(q for q in script if q[0] == k).split() for k in "CR"}
    lines = []

    def cast(how=0x52, duration=1000, enter=13, player=False, job=None, state=0, d0=0, f0cc=0, windows=0,
             size=(100, 40), balloon=(100, 40), spoke=0, end=None, begin=None, sx=320, sy=240, pixels=0.5,
             screen=(640, 480), f1bc=0, weapon=0, play=0, updates=8, step=100, fade=0):
        tokens = bases["R" if player else "C"]
        n = 44 if player else 39
        f = tokens[1:1 + n]
        f[7] = str(job if job is not None else (7 if player else 1100))
        f[17], f[19] = str(state), str(d0)
        f[11], f[14] = "0", "0"
        f[4] = f"0x{bits(4.0):08x}"
        server, tick = (int(tokens[1 + n + len(FIELDS) + k]) for k in (0, 1))
        f[12] = str((tick + fade) & 0xFFFFFFFF) if fade else "0"
        end = tick + 1000 if end is None else tick + end
        begin = tick if begin is None else tick + begin
        values = [how, duration, enter, f0cc, windows, size[0], size[1], balloon[0], balloon[1],
                  (tick + spoke) & 0xFFFFFFFF, end & 0xFFFFFFFF, begin & 0xFFFFFFFF, sx, sy,
                  f"0x{bits(pixels):08x}", screen[0], screen[1], f1bc, weapon, play, tick]
        values = [v if isinstance(v, str) else str(v & 0xFFFFFFFF) for v in values]
        times = [server, tick]
        for k in range(1, updates + 1):
            times += [(server + step * k) & 0xFFFFFFFF, (tick + step * k) & 0xFFFFFFFF]
        lines.append(("R " if player else "C ") + " ".join(f + values + [str(t) for t in times]))

    # Every message, with or without the window it makes or closes, for actors and players.
    for player in (False, True):
        for how in (0, 0x52, 0x53, 0x21, 0x36):
            for windows in (0, 15):
                cast(how=how, windows=windows, enter=0, player=player)
    # The duration by its edges, the gauge filling update by update; the end reached exactly.
    for duration in (-1, 0, 1, 99, 100, 101, 250, 0x7FFFFFFF):
        cast(duration=duration, updates=5)
        cast(duration=duration, updates=5, enter=0, state=8)
    for end, begin in ((300, 0), (300, 301), (300, 300), (300, -5000), (0, 0), (-1, 0), (100, 200)):
        cast(how=0, enter=0, windows=8, end=end, begin=begin, updates=5)
        cast(how=0, enter=0, windows=8, end=end, begin=begin, state=13, updates=5)
    # State 13's action by job, +0x1bc and weapon, and +0xcc; the end by the state.
    for player in (False, True):
        for job in (-1, 0, 1, 7, 0x15, 0x16, 1100):
            for f1bc, weapon in ((0, 0), (1, 0), (0, 0xB), (1, 1105)):
                cast(how=0, enter=0, state=13, job=job, f1bc=f1bc, weapon=weapon, player=player, end=250)
        for f0cc in (0, 1):
            cast(how=0, enter=0, state=13, f0cc=f0cc, player=player, end=50)
        for state in (0, 2, 4, 8, 12, 13):
            cast(how=0, enter=0, state=state, windows=8, end=150, player=player)
        cast(state=3, player=player)
        cast(d0=1, player=player)
    # The balloon by its 5 s, each screen edge and its width halved.
    for spoke in (-4899, -4900, -4901, -4999, -5000, 0, 100):
        cast(how=0, enter=0, windows=1, spoke=spoke, updates=3)
    for sx, sy in ((-100, 240), (0, 240), (2, 240), (700, 240), (580, 240), (320, -50), (320, 0), (320, 2),
                   (320, 600), (320, 500), (-0x80000000, 0x7FFFFFFF)):
        for size in ((100, 40), (8, 8), (700, 500)):
            cast(how=0, enter=0, windows=1, sx=sx, sy=sy, size=size, updates=2)
    for balloon in ((101, 40), (-101, 40), (0, 0), (7, -30), (-0x80000000, 0x7FFFFFFF)):
        cast(how=0, enter=0, windows=1, balloon=balloon, updates=2)
    # The titles, each and both.
    for windows in (2, 4, 6):
        cast(how=0, enter=0, windows=windows, updates=2)
    # The rise by the screen's height and +0x48, and the gauge made there.
    for screen in ((640, 480), (800, 600), (640, 0), (640, 5), (640, 6), (640, 0x7FFFFFFF), (640, 26512144)):
        for pixels in (0.5, 1.0, 0.0, -2.0, float("nan"), float("inf"), 1e30):
            cast(windows=15, screen=screen, pixels=pixels, updates=2)
    for sy in (0, 40, -40, 0x7FFFFFFF, -0x80000000, 16777217):
        cast(windows=15, sy=sy, pixels=0.75, updates=2)
    # The fade ends the updates.
    cast(fade=150)
    return lines


def main():
    oracle = path_oracle.Oracle()
    maps = [("open", dict(path_vectors.MAPS)["open"], False), ("walls", dict(path_vectors.MAPS)["walls"], False),
            ("hills", walk_vectors.HILLS, True)]
    lines = ["// Generated by tools/cast_vectors.py from the original in the emulation oracle."]
    lines.append("// The maps are tools/walk_vectors.py's (walk_vectors.inc's kWalkMaps). Each map's casts in")
    lines.append("// order (tools/walk_oracle.py's script and output), then chosen ones.")
    lines.append("static const StateQueryVector kCastQueries[] = {")
    for m, (name, rows, hilly) in enumerate(maps):
        width, height = len(rows[0]), len(rows)
        attrs = [path_vectors.ATTRIBUTES[c] for row in rows for c in row]
        script = walk_oracle.make_casts(name, width, height, attrs, 12, updates=(4, 12, 30, 80))
        if name == "open":
            script += deciding(script)
        for query, result in zip(script, oracle.run(walk_vectors.gat(rows, hilly), script)):
            lines.append(f"    {{{m}, \"{query}\", \"{result}\"}},")
    lines.append("};")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
