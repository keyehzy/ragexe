"""Compares the port's walk with the original on every map (docs/world/walk.md).

usage: .venv/bin/python -I tools/walk_oracle.py [--queries N] [--jobs J] [--install DIR] [NAME...]

As tools/path_oracle.py does, each map's .gat goes to the original's g_PathFinder in the
emulation oracle. Its script finds paths (PathFinder::FindPath, CGameActor::FindMovePath)
and walks each with CGameActor::ProcessState (005038c0) on an actor in state 1, from a point in the
start cell, update by update until it arrives (SetState(0), recorded) or its fade ends:
  - the updates' server times step by nothing, a few milliseconds, a frame, seconds, and at
    times back; timeGetTime runs alongside with an offset;
  - the facing, the camera's yaw, the delay, the distance already walked, the base action,
    the job (players, the portal, NPCs, monsters, the mounted 7 and 14), +0x190, the frames
    of the actions, the hold (+0x1d0), the fade (+0x234) and the colour vary.
After each update the result, the actor's position, facing, action, frame, segment,
distance, times, colour, +0x82 and +0x190, and whether it stood, are hashed; the hash and
the end compare with `ragres find-paths` on the same script. Then moves (G, P, T, U), standing
(N, Y) and states given (E, Q), as their makers below describe (docs/world/states.md).
"""
import os
import random
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import path_oracle  # noqa: E402

CELL_SIZE = 5.0


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def bits(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def centre(cell, side):
    """GetCellCentre in single precision."""
    return f32(f32(f32(cell - f32(side * 0.5)) * CELL_SIZE) + f32(CELL_SIZE * 0.5))


def make_walks(name, width, height, attrs, count, updates=(8, 40, 200, 600)):
    """The script for a map: count searches, each followed by a walk of the path in one of
    the numbers of updates."""
    rng = random.Random(zlib.crc32(b"walk " + name.encode()))
    walkable = [(k % width, k // width) for k, a in enumerate(attrs) if a not in (1, 5)] or [(0, 0)]
    lines = []
    for _ in range(count):
        sx, sy = rng.choice(walkable)
        r = rng.choice([1, 2, 3, 5, 8, 12])
        gx, gy = sx + rng.randint(-r, r), sy + rng.randint(-r, r)
        if rng.random() < 0.2:
            gx, gy = rng.choice(walkable)
        speed = rng.choice([150, 150, 150, 100, 200, 75, 300, 7, 999])
        start = rng.choice([rng.randint(0, 100000), rng.getrandbits(32), 0xFFFFFFFF - rng.randint(0, 5000)])
        sub_x, sub_y = rng.choice([8, rng.randint(0, 15)]), rng.choice([8, rng.randint(0, 15)])
        lines.append(f"F {start} {sx} {sy} {sub_x} {sub_y} {gx} {gy} {speed}")
        x = f32(centre(sx, width) + f32(f32(CELL_SIZE * 0.0625) * (sub_x - 8)))
        z = f32(centre(sy, height) + f32(f32(CELL_SIZE * 0.0625) * (sub_y - 8)))
        if rng.random() < 0.1:
            x, z = f32(x + rng.uniform(-3, 3)), f32(z + rng.uniform(-3, 3))
        server = (start + rng.choice([0, 0, rng.randint(-300, 60)])) & 0xFFFFFFFF
        offset = rng.choice([72, 72 + rng.randint(0, 5000), rng.getrandbits(32)])
        tick = (server + offset) & 0xFFFFFFFF
        job = rng.choice([rng.randint(0, 44), rng.randint(0, 44), 45, rng.randint(46, 999),
                          rng.randint(1000, 1400), 7, 14])
        effect = rng.choice([0, 0, 0x20, rng.getrandbits(32)])
        hold = rng.choice([0, 0, 0, (tick + rng.randint(0, 800)) & 0xFFFFFFFF])
        fade = rng.choice([0] * 8 + [(tick + rng.randint(0, 3000)) & 0xFFFFFFFF, (tick - 5) & 0xFFFFFFFF])
        fields = [
            f"0x{bits(x):08x}", f"0x{bits(z):08x}",
            f"0x{bits(f32(rng.choice([0.0, 45.0, 180.0, 359.5, rng.uniform(0, 360)]))):08x}",
            f"0x{bits(f32(rng.choice([0.0, 45.0, 135.0, 270.0, rng.uniform(0, 360)]))):08x}",
            f"0x{bits(f32(rng.choice([1.0, 2.0, 4.0, 6.5, 0.75, 12.0]))):08x}",
            f"0x{bits(f32(rng.choice([0.0, 0.0, rng.uniform(0, 500)]))):08x}",
            str(rng.choice([0, 0, 8, 16, 24, 32, 40, 48, 56, 3])), str(job), str(effect),
            str(int(rng.random() < 0.1)), str(rng.choice([1, 2, 4, 8, 8, 13])), str(hold), str(fade),
            str(rng.getrandbits(32)), str(int(rng.random() < 0.2)),
            str((server - rng.choice([0, 16, rng.randint(0, 400)])) & 0xFFFFFFFF),
            str((tick - rng.randint(0, 40)) & 0xFFFFFFFF),
        ]
        steps = []
        for _ in range(rng.choice(updates)):
            step = rng.choice([0, 1, 7, 16, 24, 24, 33, 50, 100, 250, rng.randint(0, 3000), -5, -rng.randint(0, 300)])
            server = (server + step) & 0xFFFFFFFF
            tick = (tick + step + rng.choice([0, 0, 0, 1, 10])) & 0xFFFFFFFF
            steps += [str(server), str(tick)]
        lines.append("W " + " ".join(fields + steps))
    return lines


def make_goes(name, width, height, attrs, count, updates=(8, 40, 200, 600)):
    """The script for a map: count moves (CGameActor::vf02's message 0) each walked to
    standing, now and then after a search, from actors in assorted states."""
    rng = random.Random(zlib.crc32(b"go " + name.encode()))
    walkable = [(k % width, k // width) for k, a in enumerate(attrs) if a not in (1, 5)] or [(0, 0)]
    lines = []
    for _ in range(count):
        if rng.random() < 0.2:
            sx, sy = rng.choice(walkable)
            lines.append(f"F {rng.randint(0, 100000)} {sx} {sy} 8 8 {sx + rng.randint(-4, 4)} {sy + rng.randint(-4, 4)} 150")
        sx, sy = rng.choice(walkable)
        r = rng.choice([0, 1, 2, 3, 5, 8, 12])
        gx, gy = sx + rng.randint(-r, r), sy + rng.randint(-r, r)
        if rng.random() < 0.15:
            gx, gy = rng.choice(walkable)
        sub_x, sub_y = rng.choice([8, rng.randint(0, 15)]), rng.choice([8, rng.randint(0, 15)])
        x = f32(centre(sx, width) + f32(f32(CELL_SIZE * 0.0625) * (sub_x - 8)))
        z = f32(centre(sy, height) + f32(f32(CELL_SIZE * 0.0625) * (sub_y - 8)))
        server = rng.choice([rng.randint(0, 100000), rng.getrandbits(32), 0xFFFFFFFF - rng.randint(0, 5000)])
        time = (server + rng.choice([0, 0, -rng.randint(0, 3000), rng.randint(0, 200), -rng.randint(0, 60000)])) & 0xFFFFFFFF
        tick = (server + rng.choice([72, 72 + rng.randint(0, 5000), rng.getrandbits(32)])) & 0xFFFFFFFF
        job = rng.choice([rng.randint(0, 44), 45, rng.randint(46, 999), rng.randint(1000, 1400), 7, 14])
        hold = rng.choice([0, 0, 0, (tick + rng.randint(0, 800)) & 0xFFFFFFFF])
        fade = rng.choice([0] * 10 + [(tick + rng.randint(0, 3000)) & 0xFFFFFFFF])
        delay = lambda: f32(rng.choice([4.0, 4.0, 0.5, 1.0, 6.0, 12.0, 0.0]))
        fields = [
            f"0x{bits(x):08x}", f"0x{bits(z):08x}",
            f"0x{bits(f32(rng.choice([0.0, 45.0, 180.0, 359.5, rng.uniform(0, 360)]))):08x}",
            f"0x{bits(f32(rng.choice([0.0, 45.0, 135.0, 270.0, rng.uniform(0, 360)]))):08x}",
            f"0x{bits(f32(rng.choice([1.0, 4.0, 6.5]))):08x}",
            f"0x{bits(f32(rng.choice([0.0, 0.0, rng.uniform(0, 500)]))):08x}",
            str(rng.choice([0, 0, 8, 16, 24, 3])), str(job), str(rng.choice([0, 0, 0x20])),
            str(int(rng.random() < 0.1)), str(rng.choice([1, 2, 4, 8, 13])), str(hold), str(fade),
            str(rng.getrandbits(32)), str(int(rng.random() < 0.2)),
            str((server - rng.choice([0, 16, rng.randint(0, 400)])) & 0xFFFFFFFF),
            str((tick - rng.randint(0, 40)) & 0xFFFFFFFF),
            # the state before, the speed, +0xd0, +0xd8, the service type
            str(rng.choice([0, 0, 0, 1, 1, 4, 3])), str(rng.choice([150, 150, 100, 200, 75, 300, 7, 999])),
            str(int(rng.random() < 0.05)), str(int(rng.random() < 0.1)), str(rng.choice([1, 1, 0])),
            f"0x{bits(delay()):08x}", f"0x{bits(delay()):08x}",
            f"0x{bits(f32(rng.choice([1.0, 1.0, 0.5, 2.0]))):08x}",
            f"0x{bits(f32(x + rng.uniform(-10, 10))):08x}", f"0x{bits(f32(z + rng.uniform(-10, 10))):08x}",
            str(time), str(sx), str(sy), str(sub_x), str(sub_y), str(gx), str(gy),
            str(rng.choice([rng.getrandbits(31), 0x2C8C7D, 0x2C8C80, 0x2C8C81, 0x2C8C82, 0])),
            # +0x4c, +0x81, how many delays the .act has, +0x194
            f"0x{bits(f32(rng.choice([1.0, 1.0, 2.0, 3.5]))):08x}", str(int(rng.random() < 0.3)),
            str(rng.choice([9, 9, 9, 0, 1, 8])), str(rng.choice([0, 0, 3, -1])),
            str(server), str(tick),
        ]
        steps = []
        for _ in range(rng.choice(updates)):
            step = rng.choice([0, 1, 7, 16, 24, 24, 33, 50, 100, 250, rng.randint(0, 3000), -5])
            server = (server + step) & 0xFFFFFFFF
            tick = (tick + step + rng.choice([0, 0, 0, 1, 10])) & 0xFFFFFFFF
            steps += [str(server), str(tick)]
        lines.append("G " + " ".join(fields + steps))
    return lines


def make_plays(name, width, height, attrs, count, updates=(8, 40, 200, 600)):
    """The script for a map: moves as make_goes makes them, for players (CPc): a player's job,
    the state before among 2 and 8 too, and CPc's +0x218, +0x294, +0x1cc, +0x224 and +0xcc."""
    rng = random.Random(zlib.crc32(b"play " + name.encode()))
    lines = []
    for line in make_goes("player " + name, width, height, attrs, count, updates):
        if line[0] != "G":
            lines.append(line)
            continue
        tokens = line.split()[1:]
        fields, times = tokens[:39], tokens[39:]
        fields[7] = str(rng.choice([rng.randint(0, 44), 0, 7, 14, 23]))
        fields[17] = str(rng.choice([0, 0, 1, 2, 4, 8, 3]))
        extras = [str(rng.choice([0, 0, 1, 2, 3])), str(rng.choice([0, 0, 2, 5])),
                  str(rng.choice([0, 0, rng.randint(1, 1 << 20)])), str(rng.choice([0, 0, 1])),
                  str(rng.choice([0, 1, rng.getrandbits(16)]))]
        lines.append("P " + " ".join(fields + extras + times))
    return lines


def make_tos(name, width, height, attrs, count, updates=(8, 40, 200, 600)):
    """The script for a map: moves as make_goes and make_plays make them, as message 0x13 from
    where the actor stands (T, and U for players), at a speed of their own, often 0 (pushed)."""
    rng = random.Random(zlib.crc32(b"to " + name.encode()))
    lines = []
    goes = make_goes("to " + name, width, height, attrs, count // 2, updates)
    plays = make_plays("to " + name, width, height, attrs, count - count // 2, updates)
    for line in goes + plays:
        if line[0] not in "GP":
            lines.append(line)
            continue
        tokens = line.split()
        n = 39 if line[0] == "G" else 44
        fields, times = tokens[1:1 + n], tokens[1 + n:]
        speed = str(rng.choice([0, 0, 150, 150, 7, 300, 999]))
        lines.append({"G": "T ", "P": "U "}[line[0]] + " ".join(fields + [speed] + times))
    return lines


def make_stands(name, width, height, attrs, count, updates=(8, 40, 200, 600)):
    """The script for a map: standing actors (N) and players (Y) as make_goes and make_plays
    make them, but in state 0, updated by ProcessState: often a pet (a monster's job, +0x1f4 20,
    its id the pet's) whose talk is due, with srand's seed and its lines, and actions played
    once and finished."""
    rng = random.Random(zlib.crc32(b"stand " + name.encode()))
    lines = []
    goes = make_goes("stand " + name, width, height, attrs, count // 2, updates)
    plays = make_plays("stand " + name, width, height, attrs, count - count // 2, updates)
    for line in goes + plays:
        if line[0] not in "GP":
            lines.append(line)
            continue
        tokens = line.split()
        n = 39 if line[0] == "G" else 44
        fields, times = tokens[1:1 + n], tokens[1 + n:]
        fields[17] = "0"
        if line[0] == "G" and rng.random() < 0.6:
            fields[7] = str(rng.choice([1000, 1002, rng.randint(1000, 1407), 1407, 1408, 999]))
        actor_id = int(fields[34])
        tick = int(times[1])
        stand = [str(rng.getrandbits(31)), str(actor_id if rng.random() < 0.7 else rng.getrandbits(31)),
                 str(rng.getrandbits(16)), str(rng.getrandbits(16)),
                 str(rng.choice([0, (tick - rng.choice([0, 2999, 3000, 3001, 5000])) & 0xFFFFFFFF,
                                 (tick - rng.randint(0, 9000)) & 0xFFFFFFFF])),
                 str(rng.choice([0, 0, 1, 1, 2])), str(rng.choice([20, 20, 20, 0, 19]))]
        lines.append({"G": "N ", "P": "Y "}[line[0]] + " ".join(fields + stand + times))
    return lines


def make_enters(name, width, height, attrs, count, updates=(8, 40, 200, 600)):
    """The script for a map: actors (E) and players (Q) as make_goes and make_plays make them,
    given a state through their vf10 from assorted states, or left in the state they are in,
    and updated by ProcessState: the states 0 and 2 to 17 and some past them (not walking,
    which needs a path), each now and then from state 4 or blocked by state 3 or +0xd0; with a
    start of the state near the first update's time, +0xcc, and for state 8 an actor it faces,
    found or not."""
    rng = random.Random(zlib.crc32(b"enter " + name.encode()))
    lines = []
    goes = make_goes("enter " + name, width, height, attrs, count // 2, updates)
    plays = make_plays("enter " + name, width, height, attrs, count - count // 2, updates)
    states = [0, 2, 3, 4, 5, 6, 7, 8, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 0x7FFFFFFF, 0xFFFFFFFF]
    for line in goes + plays:
        if line[0] not in "GP":
            lines.append(line)
            continue
        player = line[0] == "P"
        tokens = line.split()
        n = 44 if player else 39
        fields, times = tokens[1:1 + n], tokens[1 + n:]
        # The states whose entry (vf10) and update are ported.
        unported_set = {2, 3, 4, 7, 9, 13}
        unported_update = {13} if player else {3, 13}
        before = rng.choice(states)
        while before in unported_update:
            before = rng.choice(states)
        set_state = int(rng.random() < 0.75)
        enter = rng.choice(states)
        while enter in unported_set and not (enter == 7 and before == 6):
            enter = rng.choice(states)
        if rng.random() < 0.08:
            before = 3
        blocked = before == 3 or fields[19] != "0" or (enter == 7 and before == 6)
        if not set_state or blocked:
            state = before
        else:
            state = enter
        if state in unported_update:
            times = times[:2]
        fields[17] = str(before)
        x, z = (struct.unpack("<f", struct.pack("<I", int(fields[k], 0)))[0] for k in (0, 1))
        tick = int(times[1])
        enter_fields = [
            str(set_state), str(enter), str(rng.choice([0, 1, 1, 7])),
            str(rng.choice([0, rng.getrandbits(31), rng.getrandbits(31)])), str(int(rng.random() < 0.7)),
            f"0x{bits(f32(x + rng.uniform(-60, 60))):08x}", f"0x{bits(f32(z + rng.uniform(-60, 60))):08x}",
            str(rng.choice([0, 1, 1, 2])),
            str((tick - rng.choice([0, 0, 24, 100, 199, 200, 201, 816, rng.randint(0, 5000)])) & 0xFFFFFFFF),
        ]
        lines.append({"G": "E ", "P": "Q "}[line[0]] + " ".join(fields + enter_fields + times))
    return lines


def main():
    args = path_oracle.arguments("walks per map", 40)
    ours, originals = path_oracle.run_maps(
        args, lambda *a: make_walks(*a) + make_goes(*a) + make_plays(*a) + make_tos(*a)
        + make_stands(*a) + make_enters(*a), "walks")
    totals = {}
    for (_, lines, _), want in zip(ours, originals):
        for line, result in zip(lines, want):
            key = result.split()[0] if line[0] == "F" else {"W": "walk ", "G": "go ", "P": "player ", "T": "to ", "U": "player to ", "N": "stand ",
                   "Y": "player stand ", "E": "enter ",
                   "Q": "player enter "}[line[0]] + result.split()[0]
            totals[key] = totals.get(key, 0) + 1
            if line[0] in "WGPTUNYEQ" and result != "-":
                totals["updates"] = totals.get("updates", 0) + int(result.split()[1])
    return path_oracle.report(ours, originals, totals, "queries")


if __name__ == "__main__":
    sys.exit(main())
