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
the end compare with `ragres find-paths` on the same script.
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


def main():
    args = path_oracle.arguments("walks per map", 40)
    ours, originals = path_oracle.run_maps(args, make_walks, "walks")
    totals = {}
    for (_, lines, _), want in zip(ours, originals):
        for line, result in zip(lines, want):
            key = result.split()[0] if line[0] == "F" else "walk " + result.split()[0]
            totals[key] = totals.get(key, 0) + 1
            if line[0] == "W" and result != "-":
                totals["updates"] = totals.get("updates", 0) + int(result.split()[1])
    return path_oracle.report(ours, originals, totals, "queries")


if __name__ == "__main__":
    sys.exit(main())
