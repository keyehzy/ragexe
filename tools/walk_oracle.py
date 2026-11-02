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
(N, Y) and states given (E, Q), as their makers below describe (docs/world/states.md), and
actors coming into view (A, docs/world/arrival.md), the actors' effects (X, K and B,
docs/world/effects.md), and their deaths (D and H).
"""
import os
import random
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import path_oracle

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


def make_arrivals(name, width, height, attrs, count, updates=(8, 40, 200, 600)):
    """The script for a map: actors as make_goes makes them coming into view (A) through
    CGameActor::SendMsg's messages 3 and 4, standing on a cell, and 5 and 6, walking from one:
    each with status words before and in the record that differ or not, bit by bit (hiding,
    cloaking, the grouped bits, hidden from all) and in the body states 0 to 7; the player's id
    the actor's now and then, or one of those who see hidden actors; jobs whose guilds show an
    emblem, the emblem's file there or not; a player in the world or none; message 4 fading in
    (1) or launching its effect only when hidden (2). Then ProcessState as G's, from states that
    keep a path to walk."""
    rng = random.Random(zlib.crc32(b"arrive " + name.encode()))
    lines = []
    walkable = [(k % width, k // width) for k, a in enumerate(attrs) if a not in (1, 5)] or [(0, 0)]
    effects = [0, 0, 1, 2, 4, 6, 8, 0x10, 0x20, 0x40, 0x42, 0x80, 0x400, 0x788]

    def effect():
        return rng.choice(effects + [rng.getrandbits(11), rng.getrandbits(32)])

    def health():
        return rng.choice([0, 0, 0, 1, 2, 4, 8, 0x10, 0x20, 0x40, 0x13, rng.getrandbits(7), rng.getrandbits(32)])

    def body():
        return rng.choice([0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, -1])

    for line in make_goes("arrive " + name, width, height, attrs, count, updates):
        if line[0] != "G":
            lines.append(line)
            continue
        tokens = line.split()
        fields, times = tokens[1:40], tokens[40:]
        message = rng.choice([3, 4, 4, 5, 6, 6])
        fields[7] = str(rng.choice([rng.randint(0, 0x15), 7, 14, 0x16, 0x505, 0x507, 0x508, 0x2D2,
                                    rng.randint(1000, 1400)]))
        effect0 = effect()
        fields[8] = str(effect0)
        new_effect = rng.choice([effect0, effect0 ^ (1 << rng.randint(0, 10)), effect()])
        body0 = body()
        new_body = rng.choice([body0, body0, body()])
        guild = rng.choice([0, 1, rng.randint(1, 1 << 20), -1])
        emblem = rng.choice([0, 0, 1, rng.randint(2, 50), -1])
        old_id = int(fields[34])
        new_id = rng.choice([old_id, rng.getrandbits(31)]) if message in (4, 6) else old_id
        own = rng.choice([rng.getrandbits(31), new_id, new_id, 0x186B4, 0x2933C9, 0x326B70, 0x2100E])
        how = rng.choice([0, 0, 1, 2])
        if message == 4 and how == 2:
            new_effect |= 0x40
        junk = lambda: rng.choice([0, rng.getrandbits(32)])
        if message in (3, 4):
            cx, cy = rng.choice(walkable) if rng.random() < 0.9 else (rng.randint(-3, width + 3),
                                                                      rng.randint(-3, height + 3))
            job = rng.choice([0x2D2, 0x2D2, int(fields[7]), 7])
            record = [new_id, job, junk(), cx, cy, rng.choice([0, 1, 2, 3, 4, 5, 6, 7, -1, 9]),
                      rng.choice(path_oracle.SPEEDS), junk(), junk(), junk(), junk(), junk(), junk(),
                      new_body, health(), new_effect, junk(), rng.choice([0, 1, rng.randint(2, 50), -1]),
                      rng.choice([0, 1, rng.getrandbits(16)]), rng.choice([0, 7, rng.getrandbits(16)]),
                      junk(), junk(), junk(), guild, emblem, junk(), junk(), junk()]
        else:
            order = [int(fields[k]) for k in range(27, 34)]
            record = [new_id, rng.choice([int(fields[7]), 7, 14]), junk(), *order,
                      rng.choice(path_oracle.SPEEDS), junk(), junk(), junk(), new_body, health(),
                      new_effect, junk(), junk(), junk(), junk(), junk(), junk(), junk(), guild, emblem,
                      junk(), junk()]
        x, z = (struct.unpack("<f", struct.pack("<I", int(fields[k], 0)))[0] for k in (0, 1))
        arrive = [str(message), str(how), str(body0), str(health()),
                  str(rng.choice([0b1011, 0b1011, 0b0011, rng.getrandbits(4)])),
                  str(rng.choice([0, rng.getrandbits(20)])), str(rng.choice([0, rng.randint(1, 9)])),
                  str(own), str(int(rng.random() < 0.5)),
                  # Leaving body states 1 and 2 plays a sound by the player, read without a check.
                  str(int(rng.random() < 0.85 or (body0 != new_body and body0 in (1, 2)))),
                  f"0x{bits(f32(x + rng.uniform(-40, 40))):08x}", f"0x{bits(f32(z + rng.uniform(-40, 40))):08x}"]
        # Message 5 without a path, or a change of state blocked (+0xd0), would leave a walker
        # walking a path it may not have.
        if fields[17] == "1" and (message == 5 or fields[19] != "0"):
            fields[17] = "0"
        if fields[17] == "3":
            times = times[:2]
        lines.append("A " + " ".join(fields + arrive + [str(r) for r in record] + times))
    return lines


EFFECT_STARTS = [0x33, 0x65, 0x38, 0x3A, 0x43, 0x45, 0x47, 0x49, 0x4B, 0x4E, 0x3E, 0x3F, 0x5C, 0x5F, 0x7D]
EFFECT_ENDS = [0x34, 0x66, 0x39, 0x3B, 0x6B, 0x44, 0x46, 0x48, 0x4A, 0x4C, 0x4F]
CARTS = [8, 0x80, 0x100, 0x200, 0x400]


def make_effects(name, width, height, attrs, count, updates=(8, 40, 200, 600)):
    """The script for a map: actors (X) and players (K) as make_goes and make_plays make them,
    sent messages that make and end their effects (docs/world/effects.md): the slots full or
    empty before, sometimes one effect in many, more in the list; each slot's start and end,
    the effects without a slot, emotions (0x22 among them), effects gone (0x30) that the actor
    has, had or never had, and for actors the other messages SetStatus sends; effect states of
    the carts, hiding and cloaked. Then arrivals as make_arrivals makes them (B) with the
    actor's messages to itself through its own vf02 and the carts' effect states."""
    rng = random.Random(zlib.crc32(b"effects " + name.encode()))

    def effect_fields():
        slots = rng.choice([0, 0, rng.getrandbits(11), 0x7FF, 1 << rng.randint(0, 10)])
        return [str(slots), str(rng.choice([0, 0, 1, 3])), str(rng.choice([0, 0, 0, 1]))]

    def effect_state():
        return rng.choice(CARTS + [0, 0, 2, 4, 6, 0x10, 0x88, 0x18, rng.getrandbits(11)])

    def script(player):
        others = [] if player else [0x6D, 0x23, 0x69, 0x6A]
        messages = []
        for _ in range(rng.choice([1, 3, 8, 20, 40])):
            message = rng.choice(EFFECT_STARTS * 2 + EFFECT_ENDS * 2 + [0x15, 0x15, 0x30, 0x30] + others)
            arg, sender = 0, 0
            if message == 0x15:
                arg = rng.choice([0x22, 0x22, rng.randint(0, 0x40), -1])
            elif message == 0x30:
                sender = rng.choice([rng.randint(1, 8), rng.randint(1, 30), 0, 999])
            elif message in (0x33, 0x6D):
                arg = rng.choice([0, rng.getrandbits(11), 0xC8])
            messages += [message, arg, sender]
        return [str(len(messages) // 3), *(str(m) for m in messages)]

    lines = []
    for kind, source in (("X", make_goes("effects " + name, width, height, attrs, count, updates)),
                         ("K", make_plays("effects " + name, width, height, attrs, count, updates))):
        for line in source:
            if line[0] not in "GP":
                lines.append(line)
                continue
            tokens = line.split()[1:]
            n = 39 if kind == "X" else 44
            fields, times = tokens[:n], tokens[n:]
            fields[8] = str(effect_state())
            if kind == "X":
                fields[17] = str(rng.choice([0, 0, 1, 3, 6, 10, 2]))
            lines.append(f"{kind} " + " ".join(fields + effect_fields() + script(kind == "K") + times[:2]))
    for line in make_arrivals("effects " + name, width, height, attrs, count, updates):
        if line[0] != "A":
            lines.append(line)
            continue
        tokens = line.split()[1:]
        fields, arrive, rest = tokens[:39], tokens[39:51], tokens[51:]
        record = rest[:path_oracle.ARRIVE_RECORD]
        if rng.random() < 0.4:
            # Message 4 with `how` 2 launches effect 6 unless bit 6 is set.
            hidden = 0x40 if arrive[:2] == ["4", "2"] else 0
            record[15 if arrive[0] in ("3", "4") else 16] = str(effect_state() | hidden)
        if rng.random() < 0.3:
            fields[8] = str(effect_state())
        lines.append("B " + " ".join(fields + arrive + effect_fields() + record + rest[path_oracle.ARRIVE_RECORD:]))
    return lines


def make_deaths(name, width, height, attrs, count, updates=(8, 40, 200, 600)):
    """The script for a map: actors (D) and players (H) as make_goes and make_plays make them,
    dying (docs/world/states.md#dying) through SetState(3), 0xc or 0x1c: from the states whose
    update is ported, now and then already dying or blocked by +0xd0; monsters, players and
    NPCs, the mounted jobs; frozen or not, with its ice in its slot or not; something queued or
    not, +0x210 set or not; a sprite or none. Then ProcessState update by update until it
    returns 0 or its state changes, often long enough for the action to end."""
    rng = random.Random(zlib.crc32(b"die " + name.encode()))
    lines = []
    for kind, source in (("D", make_goes("die " + name, width, height, attrs, count, updates)),
                         ("H", make_plays("die " + name, width, height, attrs, count, updates))):
        for line in source:
            if line[0] not in "GP":
                lines.append(line)
                continue
            tokens = line.split()[1:]
            n = 39 if kind == "D" else 44
            fields, times = tokens[:n], tokens[n:]
            fields[17] = str(rng.choice([0, 0, 0, 2, 3, 4, 5, 6, 8, 10, 11, 12]))
            if kind == "D":
                fields[7] = str(rng.choice([rng.randint(1001, 1400), 1000, 1001, 0x57F, 0x580, 0x16, 0x17, 7, 14,
                                            rng.randint(0, 0x15), rng.randint(46, 999)]))
            fields[34] = str(rng.choice([int(fields[34]), rng.randint(0, 200000), 100000, 0x7FFFFFFF]))
            body = rng.choice([0, 0, 2, 2, 1, 3])
            death = [str(rng.choice([0, 0xC, 0x1C, 0x1C])), str(body), str(rng.choice([0, 0, 1, 3])),
                     str(rng.choice([0, 0, 1])), str(int(rng.random() < 0.5)), str(rng.choice([0, 0, 1, 7]))]
            slots = rng.choice([0, 0, 1 << 3, 0x7FF, rng.getrandbits(11)])
            effects = [str(slots), str(rng.choice([0, 1])), "0"]
            lines.append(f"{kind} " + " ".join(fields + death + effects + times))
    return lines


def make_attacks(name, width, height, attrs, count, updates=(8, 40, 200, 600)):
    """The script for a map: actors (V) and players (J) as make_goes and make_plays make them,
    acting on a target (docs/world/attack.md): mostly message 0xb with every type, the hits'
    counts and damages by their edges, the target found, the actor itself, the player or none,
    monsters with late hits, players with their weapons; else SetState 2, 4, 7, 9 or 13 through
    vf10, or a hit queued (0x1b) with a total or none. From assorted states, now and then
    blocked; the act's frames with events, "atk" among them; hits queued before. Then
    ProcessState update by update until the state changes or the fade ends."""
    rng = random.Random(zlib.crc32(b"attack " + name.encode()))
    lines = []
    for kind, source in (("V", make_goes("attack " + name, width, height, attrs, count, updates)),
                         ("J", make_plays("attack " + name, width, height, attrs, count, updates))):
        for line in source:
            if line[0] not in "GP":
                lines.append(line)
                continue
            player = kind == "J"
            tokens = line.split()[1:]
            n = 44 if player else 39
            fields, times = tokens[:n], tokens[n:]
            before = rng.choice([0, 0, 0, 2, 4, 5, 6, 6, 7, 8, 9, 10, 11, 12, 3])
            fields[17] = str(before)
            if player:
                fields[7] = str(rng.choice([rng.randint(0, 0x15), rng.randint(0, 0x15), 0, 5, 6, 7, 9, 12, 14, 15,
                                            0x13, 0x14, 23]))
            else:
                fields[7] = str(rng.choice([rng.randint(1001, 1400), 0x3F8, 0x505, 0x506, 0x507, 0x3FC, 0x42C, 7, 14,
                                            rng.randint(0, 0x15), 0x16, rng.randint(46, 999), 999, 1000]))
            if rng.random() < 0.3:
                fields[10] = str(rng.choice([1, 2, 3, 5, 6, 9, 10, 12]))
            actor_id = int(fields[34])
            how = rng.choice([0xB] * 6 + [0, 0, 0x1B])
            enter = rng.choice([2, 2, 4, 7, 9, 9, 13]) if how == 0 else 0
            if how == 0x1B:
                enter = rng.choice([0, 0, rng.randint(1, 1000), -5])
            own = rng.choice([rng.getrandbits(31), actor_id, rng.getrandbits(31)])
            foe = rng.getrandbits(31)
            target = rng.choice([foe, foe, foe, own, actor_id, 0])
            found = rng.choice([1, 1, 1, 0, 2]) if target != actor_id else rng.choice([2, 2, 1, 0])
            f1a8 = rng.choice([target, own, 0])
            player_is = rng.choice([1, 1, 1, 2])
            if how == 0 and f1a8 != own and rng.random() < 0.3:
                player_is = 0
            x, z = (struct.unpack("<f", struct.pack("<I", int(fields[k], 0)))[0] for k in (0, 1))
            tick = int(times[1])
            codes = [rng.choice([0, 0, 0, 1, 1, 2, 3, 4, 5, 6, 7]) for _ in range(10)]
            events = sum(c << 3 * k for k, c in enumerate(codes)) if rng.random() < 0.8 else 0
            if player:
                weapon = rng.choice([rng.randint(0, 0x16), rng.randint(0, 0x16), 0xB, 0xD, 0xE, 0x10, 0x16, 0x17, 0x18,
                                     rng.randint(1100, 1999), rng.randint(1700, 1749), 0x10000 + 1705, -1])
            else:
                weapon = rng.choice([0, rng.randint(0, 0x16)])
            attack_fields = [
                str(how), str(enter), str(rng.choice([0, 0, 1, 7])), str(f1a8), str(found),
                f"0x{bits(f32(x + rng.uniform(-60, 60))):08x}", f"0x{bits(f32(z + rng.uniform(-60, 60))):08x}",
                str(own), str(player_is),
                f"0x{bits(f32(x + rng.uniform(-60, 60))):08x}", f"0x{bits(f32(z + rng.uniform(-60, 60))):08x}",
                str(rng.choice([0, 0, 1, 1, 2])), str(weapon), str(events), str(rng.choice([0, 1, 1, 2])),
                str((tick - rng.choice([0, 0, 24, 100, rng.randint(0, 5000)])) & 0xFFFFFFFF),
                str(rng.choice([0, 0, 0, 1, 2])),
            ]
            record = [
                rng.choice([0, 1, rng.randint(1, 2000), rng.randint(1, 50000), -rng.randint(1, 500), 0x7FFFFFFF]),
                rng.choice([0, 0, 0, rng.randint(1, 2000), -3]),
                rng.choice([0, 0, 0, 8, 8, 9, 9, 4, 10, 11, 1, 2, 3, 5, 6, 7, 12, -1]),
                target, rng.getrandbits(32),
                rng.choice([1, 1, 2, 3, rng.randint(1, 36), 36, 37, 0, -1]),
                rng.choice([-1, -1, rng.randint(0, width)]), rng.choice([-1, -1, rng.randint(0, height)]),
                rng.choice([rng.randint(1, 2000), 432, 433, 0, -5, 300]),
                rng.choice([rng.randint(0, 2000), 0, 288, -7]),
            ]
            if how == 0x1B:
                record[2] = rng.choice([(tick + rng.randint(0, 3000)) & 0xFFFFFFFF, rng.getrandbits(32)])
                record[0] = rng.choice([0x1A, 0x27, 0x5D, 0x5E, 0x6F, rng.getrandbits(32)])
            blocked = before == 3 or fields[19] != "0" or (enter == 7 and before == 6)
            if how == 0 and enter == 13 and not blocked:
                times = times[:2]
            lines.append(f"{kind} " + " ".join(fields + attack_fields + [str(r & 0xFFFFFFFF) for r in record]
                                                 + times))
    return lines



def make_landings(name, width, height, attrs, count, updates=(8, 40, 200, 600)):
    """The script for a map: actors (L) and players (O) as make_goes and make_plays make them,
    that hits land on (docs/world/hits.md): queued and delivered update by update, or the first
    sent at once, by every message, the damages by their edges (none, negative, the numbers'
    digits), the attacker another, the player, the actor itself or none, and their jobs and
    weapons; the sounds, the effects launched with effects on or off, the turn, the guild war
    and the player's statuses, the msg sprites, numbers of type 0x16 listed before; the motion
    by 1; from assorted states, now and then blocked, with +0x210 set or not."""
    rng = random.Random(zlib.crc32(b"land " + name.encode()))
    lines = []
    seers = [0x186B4, 0x2933C9, 0x326B70]
    for kind, source in (("L", make_goes("land " + name, width, height, attrs, count, updates)),
                         ("O", make_plays("land " + name, width, height, attrs, count, updates))):
        for line in source:
            if line[0] not in "GP":
                continue
            player = kind == "O"
            tokens = line.split()[1:]
            n = 44 if player else 39
            fields, times = tokens[:n], tokens[n:]
            fields[17] = str(rng.choice([0, 0, 0, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 3]))
            if player:
                fields[7] = str(rng.choice([rng.randint(0, 0x15), rng.randint(0, 0x15), 0, 7, 14, 0x15, 0x16, 23,
                                            0x2D]))
            else:
                fields[7] = str(rng.choice([rng.randint(1001, 1400), rng.randint(1001, 1400), 0x2C, 0x2D, 0x2E,
                                            rng.randint(0, 0x15), 0x15, 0x16, 7, 14, rng.randint(46, 999), -1]))
            if rng.random() < 0.3:
                fields[10] = str(rng.choice([1, 2, 3, 5, 6, 9, 10, 12]))
            actor_id = int(fields[34])
            how = rng.choice([0] * 6 + [0x1A, 0x27, 0x5D, 0x5E, 0x60, 0x61, 0x6F, 0x6C, 0x81])
            attacker = rng.choice([1, 1, 1, 2, 2, 0, 3, 4])
            own = rng.choice([rng.getrandbits(31), rng.getrandbits(31), actor_id, rng.choice(seers)])
            weapons = [-1, rng.randint(0, 0x16), 0, 0xB, rng.randint(1001, 2100), 1700, 0x16]
            x, z = (struct.unpack("<f", struct.pack("<I", int(fields[k], 0)))[0] for k in (0, 1))
            tick = int(times[1])
            codes = [rng.choice([0, 0, 0, 1, 1, 2, 3, 4, 5, 6, 7]) for _ in range(10)]
            events = sum(c << 3 * k for k, c in enumerate(codes)) if rng.random() < 0.8 else 0
            hits = rng.choice([1, 1, 2, 3, 5]) if how else rng.choice([0, 1, 1, 2, 3, 5])
            near = lambda v: f"0x{bits(f32(v + rng.uniform(-60, 60))):08x}"
            land_fields = [
                how, attacker, rng.randint(0, 3), rng.choice([rng.randint(0, 0x15), 0x2C, 0x2D, rng.randint(1001, 1400),
                                                              7, 14, -1]),
                rng.choice([0, 0, 0x20, rng.getrandbits(32)]), rng.choice(weapons), own, rng.randint(0, 3),
                rng.choice([0, 0, 1]), rng.choice([0, 1, 1]), rng.choice([0, 1, 1]), rng.getrandbits(32),
                rng.choice([0, 0, 1]), rng.choice([0, 0, 1, 2, 3]), rng.randint(0, 7), rng.choice([11, 11, rng.randint(0, 10)]),
                events, rng.choice([0, 1, 1, 2]), (tick - rng.choice([0, 0, 24, 100, rng.randint(0, 5000)])) & 0xFFFFFFFF,
                rng.choice([own, 0, rng.getrandbits(31)]), rng.choice(weapons), rng.choice([0, 0, 1, 2]),
            ]
            land_fields = [str(f) for f in land_fields] + [near(x), near(z), near(x), near(z), str(hits)]
            records = []
            for _ in range(hits):
                motion = rng.choice([rng.randint(0, 2000) / 288, 1.0, 0.5, 2.0, 0.0, 1.0000001, -1.0])
                records += [
                    rng.choice([0x1A, 0x1A, 0x27, 0x5D, 0x5E, 0x60, 0x61, 0x6F, 0x6F, 0x6C, 0x81]),
                    rng.choice([0, 0, 1, rng.randint(1, 2000), rng.randint(1, 50000), -rng.randint(1, 500), 0x7FFFFFFF,
                                -0x80000000, 999999, 1000000, 9, 10, 99999, 100000]),
                    (tick + rng.choice([-1000, 0, rng.randint(0, 1500), rng.randint(0, 1500), 3000]))
                    if rng.random() < 0.95 else rng.getrandbits(32),
                    own if attacker in (2, 4) else rng.getrandbits(31) | 1 << 30,
                    rng.choice([-1, -1, rng.randint(0, width)]), rng.choice([-1, -1, rng.randint(0, height)]),
                    rng.choice([-1, -1, 1, 0x23, 0x7F, 0x50, 60000, 60001, 0, 0xA4, -5]),
                    rng.choice([0, 0, -2, 0x7A, 0xD8, 0xD9, -1, 0x24, 0x7F]),
                    rng.choice([1, rng.randint(1, 5000), 0, 1000000]),
                    bits(f32(motion)),
                ]
            if attacker not in (2, 4):
                for k in range(hits):
                    if records[10 * k + 3] == own:
                        records[10 * k + 3] ^= 1
            lines.append(f"{kind} " + " ".join(fields + land_fields + [str(r & 0xFFFFFFFF) for r in records] + times))
    return lines

def make_casts(name, width, height, attrs, count, updates=(8, 40, 200, 600)):
    """The script for a map: actors (C) and players (R) as make_goes and make_plays make them,
    casting (docs/world/windows.md): message 0x52 with durations about its edges, often then
    SetState(13), or 0x53, 0x21 or 0x36, or nothing, from state 13 and others, with +0xcc set or
    not; the four windows over the actor or not, the balloon about its 5 s and the screen's
    edges, the cast's times about each update's, the point on screen, +0x48 and the screen's
    size about their edges; then the windows and the state update by update."""
    rng = random.Random(zlib.crc32(b"cast " + name.encode()))
    lines = []
    for kind, source in (("C", make_goes("cast " + name, width, height, attrs, count, updates)),
                         ("R", make_plays("cast " + name, width, height, attrs, count, updates))):
        for line in source:
            if line[0] not in "GP":
                continue
            player = kind == "R"
            tokens = line.split()[1:]
            n = 44 if player else 39
            fields, times = tokens[:n], tokens[n:]
            fields[17] = str(rng.choice([13, 13, 13, 13, 13, 8, 8, 0, 2, 4, 12, 3]))
            if player:
                fields[7] = str(rng.choice([rng.randint(0, 0x15), rng.randint(0, 0x15), 0, 7, 14, 0x16, 0x2D]))
                fields[41] = "0"
            else:
                fields[7] = str(rng.choice([rng.randint(0, 0x15), 7, 14, 0x16, rng.randint(46, 999),
                                            rng.randint(1001, 1400), -1]))
            tick = int(times[1])
            how = rng.choice([0x52, 0x52, 0x52, 0x52, 0, 0, 0x53, 0x21, 0x36])
            duration = rng.choice([0, 1, 24, 100, 500, 1000, 1500, 3000, rng.randint(0, 6000), rng.randint(0, 6000),
                                   -1, -500, 0x7FFFFFFF])
            enter = rng.choice([13, 13, 13, 0, 0, 8]) if how == 0x52 else rng.choice([0, 0, 0, 13])
            end = (tick + rng.choice([-100, 0, 1, 50, 500, 2000, rng.randint(0, 6000)])) & 0xFFFFFFFF
            if rng.random() < 0.05:
                end = rng.getrandbits(32)
            begin = (end - rng.choice([0, 1, 500, 2000, rng.randint(0, 6000), -300])) & 0xFFFFFFFF
            spoke = (tick - rng.choice([0, 1000, 4999, 5000, 5001, rng.randint(0, 6000), -50])) & 0xFFFFFFFF
            if rng.random() < 0.05:
                spoke = rng.getrandbits(32)
            bw, bh = rng.randint(8, 300), rng.randint(8, 120)
            screen = rng.choice([(640, 480), (640, 480), (800, 600), (1024, 768), (0, 0), (1, 1),
                                 (rng.randint(0, 2000), rng.randint(0, 2000)), (0x7FFFFFFF, 0x7FFFFFFF)])
            sx = rng.choice([rng.randint(-50, 700), rng.randint(-50, 700), 0, -3, -4, 320, screen[0],
                             rng.randint(-0x80000000, 0x7FFFFFFF)])
            sy = rng.choice([rng.randint(-50, 700), rng.randint(-50, 700), 0, 240, screen[1],
                             rng.randint(-0x80000000, 0x7FFFFFFF)])
            scale = rng.choice([1.0, 0.5, rng.uniform(0, 3), rng.uniform(0, 3), 0.0, -1.0, 1e30, 1e-40,
                                float("inf"), float("nan")])
            cast_fields = [
                how, duration, enter, rng.choice([0, 0, 1, 7]), rng.randint(0, 15), bw, bh,
                rng.choice([bw, bw, bw + 1, rng.randint(-300, 300)]), rng.choice([bh, bh, rng.randint(-120, 120)]),
                spoke, end, begin, sx, sy, f"0x{bits(f32(scale)):08x}", screen[0], screen[1],
                rng.choice([0, 0, 1, 2]), rng.choice([-1, rng.randint(0, 0x16), 0xB, rng.randint(1001, 2100)]),
                rng.choice([0, 1, 1, 2]), (tick - rng.choice([0, 0, 24, 100, rng.randint(0, 5000)])) & 0xFFFFFFFF,
            ]
            lines.append(f"{kind} " + " ".join(fields + [str(f) for f in cast_fields] + times))
    return lines


def make_updates(name, width, height, attrs, count, updates=(8, 40, 200, 600)):
    """The script for a map: actors (u) and players (p) as make_goes and make_plays make them,
    updated whole (docs/world/update.md): CNpc's way by job, Sight's effect about its periods,
    the key colour about its 100 ms, the colour and its brightening, the effect an NPC of job
    0x68 repeats about its period, the frames' sounds before and after a new action and past
    the last frame, the gauges and the screen, a cast's end, and the states."""
    rng = random.Random(zlib.crc32(b"update " + name.encode()))
    lines = []
    for kind, source in (("u", make_goes("update " + name, width, height, attrs, count, updates)),
                         ("p", make_plays("update " + name, width, height, attrs, count, updates))):
        for line in source:
            if line[0] not in "GP":
                continue
            player = kind == "p"
            tokens = line.split()[1:]
            n = 44 if player else 39
            fields, times = tokens[:n], tokens[n:]
            fields[17] = str(rng.choice([0, 0, 0, 0, 13, 13, 8, 2, 4, 12, 3, 6]))
            job = (rng.choice([rng.randint(0, 0x15), 4, 8, 15, 7, 14, 0x16, 0x2D]) if player else
                   rng.choice([0x2D, 0x8B, 0x68, 0x68, 0x6F, 0x6F, rng.randint(0x2E, 0x8A), rng.randint(0, 0x15),
                               4, 8, 15, 7, 14, rng.randint(46, 999), rng.randint(1001, 1400), -1]))
            fields[7] = str(job)
            fields[8] = str(rng.choice([0, 0, 1, 1, 1, 0x21, 0x20, 2, rng.getrandbits(11)]))
            frames = rng.choice([1, 2, 3, 5, 8, 10, 12])
            fields[10] = str(frames)
            fields[13] = str(rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 0xF8F8F8F8, 0x80404040, 0, 0x00FFFFFF,
                                         rng.getrandbits(32)]))
            if job == 0x68:
                fields[18] = str(rng.choice([0x16, 0x23, 0x7F, 0x89, rng.randint(0, 0xA5), -1]))
            if player:
                fields[41] = "0"
            tick = int(times[1])
            x, z = (struct.unpack("<f", struct.pack("<I", int(fields[k], 0) & 0xFFFFFFFF))[0] for k in (0, 1))
            screen = rng.choice([480, 480, 600, 768, 0, 1, rng.randint(0, 2000), 0x7FFFFFFF])
            scale = rng.choice([1.0, 0.5, rng.uniform(0, 3), rng.uniform(0, 3), 0.0, -1.0, 1e30, 1e-40,
                                float("inf"), float("nan")])
            end = (tick + rng.choice([-100, 0, 1, 50, 500, 2000, rng.randint(0, 6000)])) & 0xFFFFFFFF
            update_fields = [
                rng.choice([0, 1, 199, 200, 201, 269, 270, 0x8CA0, rng.randint(0, 300), 0xFFFFFFFF]),
                rng.choice([0, 1, 1]),
                (tick - rng.choice([0, 50, 100, 101, 200, rng.randint(0, 5000), -10])) & 0xFFFFFFFF,
                rng.choice([0xFFFF0000, 0xFFFF0000, 0, rng.getrandbits(32)]),
                rng.choice([0, 0, 0, 1]),
                rng.choice([0, 1, 2, 5, rng.randint(0, 10), 0x7FFFFFFF, 4294968]),
                (tick - rng.choice([0, 999, 1000, 1001, 2000, 5000, rng.randint(0, 20000), -5])) & 0xFFFFFFFF,
                rng.choice([0, 0, 0, 20, 21]),
                rng.choice([0, 0, 0, 1, 2, 3, 6]),
                rng.choice([0, 1]),
                rng.getrandbits(30),
                rng.choice([int(fields[6]), int(fields[6]), rng.randint(0, 16)]),
                rng.choice([0, 0, 1, frames - 1, frames, rng.randint(0, 12), -1]),
                rng.choice([0, 0, rng.randint(0, max(frames - 1, 0)), frames, frames + 2]),
                f"0x{bits(f32(x + rng.uniform(-200, 200))):08x}", f"0x{bits(f32(z + rng.uniform(-200, 200))):08x}",
                rng.choice([0, 1, 1]) if player else 0,
                rng.choice([rng.randint(-50, 700), 0, 320, rng.randint(-0x80000000, 0x7FFFFFFF)]),
                rng.choice([rng.randint(-50, 700), 0, 240, rng.randint(-0x80000000, 0x7FFFFFFF)]),
                f"0x{bits(f32(scale)):08x}", screen,
                rng.choice([0, 0, 1]), end,
                (end - rng.choice([0, 1, 500, 2000, rng.randint(0, 6000), -300])) & 0xFFFFFFFF,
                rng.choice([0, 0, 1, 7]), rng.choice([0, 0, 1, 2]),
                rng.choice([-1, rng.randint(0, 0x16), 0xB, rng.randint(1001, 2100)]),
                rng.choice([0, 1, 1, 2]), (tick - rng.choice([0, 0, 24, 100, rng.randint(0, 5000)])) & 0xFFFFFFFF,
                rng.choice([0, 0, 0, 8 * int(fields[6]), rng.randint(0, 300), 255, 256]),
            ]
            lines.append(f"{kind} " + " ".join(fields + [str(f) for f in update_fields] + times))
    return lines


def make_own_updates(name, width, height, attrs, count, updates=(8, 40, 200, 600)):
    """The script for a map: players updated whole as the own player (o, docs/world/pursuit.md),
    p's queries made afresh with the CPlayer's fields: each pursuit, its target near or far, in
    range or not, a big model or not, found or not; the goal at the cell under the player or
    not; the times about each period; PreMove about half the ping; a queued move; the items
    asked for."""
    rng = random.Random(zlib.crc32(b"own update " + name.encode()))
    lines = []
    half_w, half_h = width // 2, height // 2
    for line in make_updates("own " + name, width, height, attrs, count, updates):
        if line[0] != "p":
            continue
        tokens = line.split()[1:]
        n = 44 + len(path_oracle.UPDATE_FIELDS)
        fields, times = tokens[:n], tokens[n:]
        tick = int(times[1])
        x, z = (struct.unpack("<f", struct.pack("<I", int(fields[k], 0) & 0xFFFFFFFF))[0] for k in (0, 1))
        cx, cy = int(x // 5 + half_w), int(z // 5 + half_h)
        reach = rng.choice([0, 3, 8, 20, 60, 200])
        tx = f32(x + rng.uniform(-reach, reach))
        tz = f32(z + rng.uniform(-reach, reach))
        if rng.random() < 0.05:
            tx = rng.choice([float("inf"), float("nan"), x])
        tcx, tcy = int(tx // 5 + half_w) if tx == tx and abs(tx) < 1e9 else 0, \
            int(tz // 5 + half_h) if abs(tz) < 1e9 else 0
        near = lambda c: c + rng.randint(-3, 3)
        goal = rng.choice([(cx, cy), (cx, cy), (near(cx), near(cy)), (-1, -1), (tcx, tcy), (near(tcx), near(tcy))])
        target = rng.choice([rng.randint(1, 0x7FFFFFFF), 110000 + rng.randint(0, 1000), 2000000 + rng.randint(0, 99)])
        ping = rng.choice([0, 1, 100, 301, rng.randint(0, 2000), 0xFFFFFFFF])
        own_fields = [
            goal[0], goal[1],
            tick - rng.choice([0, 1199, 1200, 1201, 5000, rng.randint(0, 10000), -5]),
            tick - rng.choice([0, ping // 2, ping // 2 + 1, rng.randint(0, 3000), -5]),
            rng.choice([0, 1, 1, 5, 5, 2, 2, 3, 3, 4, 4, 6, 6, 7, -1]),
            rng.choice([0, 0, 1]),
            rng.choice([0, 0, 2, 1]),
            rng.choice([0, 0, 1]),
            rng.choice([0, 0, 1]),
            target,
            rng.choice([0, 0, 1, rng.randint(0, 9)]),
            tick - rng.choice([0, 449, 450, 451, 1199, 1200, 1201, rng.randint(0, 5000), -5]),
            rng.choice([0, 0, rng.randint(1, 400)]),
            rng.choice([1, 1, 2, 3, 5, 9, 14, 0, -1, rng.randint(0, 20)]),
            near(cx + rng.randint(-10, 10)), near(cy + rng.randint(-10, 10)),
            rng.choice([-1, near(cx), 0]), rng.choice([-1, near(cy), 0]),
            ping,
            rng.choice([1, 1, 2, 9, 14, 0, -1, rng.randint(0, 20), 0x7FFFFFFF]),
            rng.choice([target, rng.randint(1, 0x7FFFFFFF), 0]),
            rng.choice([7, 7, 7, 0, 1, 2, 4, 6, 5, 3]),
            f"0x{bits(tx):08x}", f"0x{bits(f32(rng.choice([0.0, 0.0, 3.5, -7.0]))):08x}", f"0x{bits(tz):08x}",
            rng.choice([0, 0, 1, 3, 2, 4]),
            rng.choice([tcx, tcx, near(tcx)]), rng.choice([tcy, tcy, near(tcy)]),
            rng.choice([0, 0, 1, 1, 2]),
            rng.choice([0x8D, 0x8F, 0x98, 0x8E, 0x7E, 7, 14, rng.randint(0, 2000)]),
            rng.choice([0, 0, 0x20]),
            rng.choice([0, 0, 1, 2]),
        ]
        lines.append("o " + " ".join(fields + [str(f & 0xFFFFFFFF) if isinstance(f, int) else f for f in own_fields]
                                     + times))
    return lines


def make_orders(name, width, height, attrs, count, updates=(8, 40, 200, 600)):
    """The script for a map: players as make_plays makes them, as the own player (m,
    docs/world/orders.md), sent a few of the messages CPlayer::SendMsg handles itself: moves
    from the player's cell, another or one off the map toward cells near and far, walkable or
    not; looks at the cells about the player with the facing anywhere and the head each way;
    attacks on targets about four thirds of the player's speed; the walk asked again about its
    second; skills on actors, cells (0x7d among them) and objects; in each state, hiding or not."""
    rng = random.Random(zlib.crc32(b"orders " + name.encode()))
    walkable = [(k % width, k // width) for k, a in enumerate(attrs) if a not in (1, 5)] or [(0, 0)]
    lines = []
    half_w, half_h = width // 2, height // 2
    for line in make_plays("orders " + name, width, height, attrs, count, updates):
        if line[0] != "P":
            continue
        tokens = line.split()[1:]
        fields, times = tokens[:44], tokens[44:]
        server, tick = int(times[0]), int(times[1])
        fields[2] = f"0x{bits(f32(rng.choice([rng.uniform(0, 360), rng.uniform(0, 360), rng.uniform(-720, 720), 0.0, 180.0, 359.9, -0.0, 3000.0, -3000.0]))):08x}"
        fields[8] = str(rng.choice([0, 0, 0, 2, 0x22, 0x20, rng.getrandbits(11)]))
        fields[17] = str(rng.choice([0, 0, 0, 1, 1, 2, 4, 5, 6, 6, 3, 7, 8]))
        fields[19] = str(int(rng.random() < 0.15))
        fields[40] = str(rng.choice([0, 0, 1, 1, 2, 2, 3]))
        x, z = (struct.unpack("<f", struct.pack("<I", int(fields[k], 0) & 0xFFFFFFFF))[0] for k in (0, 1))
        cx, cy = int(x // 5 + half_w), int(z // 5 + half_h)
        near = lambda c, r=3: c + rng.randint(-r, r)
        target = rng.choice([rng.randint(1, 0x7FFFFFFF), 110000 + rng.randint(0, 1000), 2000000 + rng.randint(0, 99)])
        skill = rng.choice([0, 0x7D, 0x7D, 0x89, rng.randint(1, 400)])
        own_fields = [
            rng.choice([cx, -1, near(cx)]), rng.choice([cy, -1, near(cy)]),
            tick - rng.randint(0, 5000), tick - rng.randint(0, 5000),
            rng.choice([0, 1, 1, 5, 5, 2, 3, 3, 4, 6, 7, -1]),
            rng.choice([0, 0, 1]), rng.choice([0, 0, 2, 1, 5]), rng.choice([0, 0, 1]), rng.choice([0, 0, 1]),
            target, rng.choice([0, 0, 1, rng.randint(0, 9)]), tick - rng.randint(0, 5000),
            skill, rng.choice([1, 1, 2, 9, 0, -1]),
            near(cx, 10), near(cy, 10), rng.choice([-1, near(cx)]), rng.choice([-1, near(cy)]),
            rng.choice([0, 100, rng.randint(0, 2000)]), rng.choice([1, 2, 9]),
            rng.choice([target, rng.randint(1, 0x7FFFFFFF)]),
            rng.choice([1, 1, 1, 0, 3, 7]),
            f"0x{bits(f32(x + rng.uniform(-30, 30))):08x}", "0", f"0x{bits(f32(z + rng.uniform(-30, 30))):08x}",
            rng.choice([0, 0, 1, 3]), near(cx), near(cy), 0, rng.choice([0, 7, 0x8D]),
            rng.choice([0, 0, 0x20]), 0,
        ]
        own_speed = rng.choice([150, 150, 100, 200, 0, 1, 2, 3, 0x7FFFFFFF, 0x40000000, 0x60000000, -1, -3,
                                rng.randint(0, 1000)])
        limit = int(((own_speed * 4) & 0xFFFFFFFF) - (1 << 32 if (own_speed * 4) & 0x80000000 else 0))
        limit = int(limit / 3)
        order_fields = [
            rng.choice([0, 0, 1]), tick - rng.choice([0, 999, 1000, 1001, rng.randint(0, 5000), -5]),
            rng.choice([0, 1, 5, 10]), own_speed, rng.choice([5, 1, 9, 0, -1]),
            rng.choice([limit, limit, limit + 1, limit - 1, 0, -1, 0x7FFFFFFF, rng.randint(0, 400)]),
            rng.choice([0, 0, 0x14, 0x13]),
        ]
        script = []
        for _ in range(rng.choice([1, 2, 4, 8, 16])):
            message = rng.choice([0, 0, 0, 8, 9, 0xA, 0xA, 0xD, 0xF, 0xF, 0x10, 0x10, 0x10, 0x10, 0x11, 0x11, 0x12,
                                  0x1D, 0x1E, 0x1F, 0x28, 0x28, 0x29, 0x2A, 0x3C, 0x41, 0x42, 0x42, 0x5A, 0x5B,
                                  0x5B, 0x5B, 0x67])
            a, b, c = rng.choice([target, rng.randint(1, 0x7FFFFFFF), 0]), 0, 0
            words = [0] * 7
            if message == 0:
                sx, sy = rng.choice([(cx, cy), (cx, cy), rng.choice(walkable), (-5, cy), (cx, height + 3),
                                     (near(cx), near(cy))])
                gx, gy = rng.choice([(near(cx, 6), near(cy, 6)), rng.choice(walkable), (near(sx), near(sy)),
                                     (-2, -2)])
                words = [(server + rng.choice([0, -rng.randint(0, 3000), rng.randint(0, 200)])) & 0xFFFFFFFF,
                         sx, sy, rng.choice([8, rng.randint(0, 15)]), rng.choice([8, rng.randint(0, 15)]), gx, gy]
            elif message in (9, 0x11, 0x5B, 0x10):
                a, b = (near(cx, 2), near(cy, 2)) if message == 0x10 else (near(cx, 8), near(cy, 8))
            elif message == 0xA:
                b = rng.choice([0, 1, 2])
            elif message in (0x1D, 0x28):
                b = rng.choice([0x8D, 0x8F, 0x98, 0x8E, 0, 7])
            elif message == 0x29:
                b, c = rng.choice([0x89, 0x89, skill, rng.randint(1, 400)]), rng.choice([1, 2, 9, 0])
            elif message == 0x41:
                a, b, c = rng.choice([skill, 0x7D, rng.randint(1, 400)]), near(cx, 9), near(cy, 9)
            elif message == 0x5A:
                a = rng.choice([1, 5, 10, 0, -1])
            elif message == 0x67:
                words = [rng.getrandbits(32), rng.choice([0, 0x7D, rng.randint(1, 400)]), rng.choice([1, 2, 9]),
                         rng.choice([1, 5, 10])] + [0] * 3
            step = rng.choice([0, 0, 1, 16, 450, 999, 1000, 1001, 1200, rng.randint(0, 5000)])
            server, tick = (server + step) & 0xFFFFFFFF, (tick + step) & 0xFFFFFFFF
            script += [message, a, b, c, *words, server, tick]
        lines.append("m " + " ".join(fields + [str(f & 0xFFFFFFFF) if isinstance(f, int) else f
                                               for f in own_fields + order_fields]
                                     + [str(len(script) // path_oracle.ORDER_RECORD)]
                                     + [str(v & 0xFFFFFFFF) for v in script]))
    return lines


LOOK_MESSAGES = [3, 3, 4, 4, 5, 6, 6, 0x79, 0x7A, 0x7B, 0x7C, 0x22, 0x22, 0x23, 0x4D, 0x64, 0x69, 0x6A, 0x72]
SPECIAL_IDS = [0x2C8C7D, 0x2C8C80, 0x2C8C82]


def make_looks(name, width, height, attrs, count, updates=(8, 40, 200, 600)):
    """The script for a map: players as make_plays makes them sent CPc::SendMsg's own messages
    (c, docs/world/look.md): arrivals standing and walking dressed by kinds and by item ids, each
    hair, headgear, shield and colour, weapons by kind, by their own sprites there or not, in
    both hands and the jobs that hold them otherwise; administrators on both sets; the status
    words as make_arrivals varies them; the gauge made, filled and closed; the frame, mounting
    (0x69, 0x6a) and 0x72; in each state, vf37's words below 0 or not."""
    rng = random.Random(zlib.crc32(b"looks " + name.encode()))
    walkable = [(k % width, k // width) for k, a in enumerate(attrs) if a not in (1, 5)] or [(0, 0)]
    lines = []
    jobs = list(range(0, 45)) + [7, 14, 0x13, 0x14, 6, 6, 0xC, 0x11, 8, 0xD, 0x15]

    def effect():
        return rng.choice([0, 0, 0, 0x20, 0x20, 0x40, 2, 4, 8, 0x10, 0x80, rng.getrandbits(11)])

    def health():
        return rng.choice([0, 0, 0, 1, 2, 4, 0x10, 0x20, 0x40, rng.getrandbits(7)])

    def body():
        return rng.choice([0, 0, 0, 0, 1, 2, 3, 4, 6])

    def hair():
        return rng.choice([rng.randint(0, 19), rng.randint(0, 19), 0, 19, 20, -1, 13, rng.getrandbits(16)])

    def headgear():
        return rng.choice([0, 0, rng.randint(1, 139), 0x12, 0x13, 139, 1])

    def shield():
        return rng.choice([0, 0, 1, 2, 3, 4, rng.randint(0x835, 0x83F), 2000, 2001, 5, rng.randint(0, 3000)])

    def colour():
        return rng.choice([0, 0, rng.randint(1, 8), -1, rng.getrandbits(16)])

    def kind():
        return rng.choice([0, rng.randint(1, 22), rng.randint(1, 22), 23, -1, 1101, rng.getrandbits(16)])

    def item():
        low = rng.choice([0, 0x63F, rng.randint(1100, 1999), rng.randint(1100, 1999), rng.randint(1, 1099),
                          rng.randint(2000, 0xFFFF), 1150, 1201, 1301, 1550])
        high = rng.choice([0, 0, 0, rng.randint(0x835, 0x83F), rng.randint(1100, 1349), rng.randint(1100, 1999),
                           rng.randint(1, 0xFFFF), 2000])
        return high << 16 | low

    for line in make_plays("looks " + name, width, height, attrs, count, updates):
        if line[0] != "P":
            continue
        tokens = line.split()[1:]
        fields, times = tokens[:44], tokens[44:]
        server, tick = int(times[0]), int(times[1])
        fields[7] = str(rng.choice(jobs))
        fields[8] = str(effect())
        fields[17] = str(rng.choice([0, 0, 0, 1, 2, 3, 6, 6, 8]))
        old_id = int(fields[34])
        x, z = (struct.unpack("<f", struct.pack("<I", int(fields[k], 0)))[0] for k in (0, 1))
        own = rng.choice([rng.getrandbits(31), old_id, 0x186B4])
        arrive = [0, 0, body(), health(), rng.choice([0b1011, 0b1011, 0b0011, rng.getrandbits(4)]),
                  rng.choice([0, rng.getrandbits(20)]), rng.choice([0, rng.randint(1, 9)]), own,
                  int(rng.random() < 0.5), 1,
                  f"0x{bits(f32(x + rng.uniform(-40, 40))):08x}", f"0x{bits(f32(z + rng.uniform(-40, 40))):08x}"]
        effect_fields = [rng.choice([0, 0, rng.getrandbits(11), 1 << 10]), rng.choice([0, 0, 1]), 0]
        ids = [old_id, rng.getrandbits(31), rng.choice(SPECIAL_IDS), 0x2100E]
        admin = rng.choice([0, 0, 0, rng.choice(ids)])
        look = [rng.choice([0, 1]), rng.randint(0, 19), colour(), rng.choice([0, rng.randint(1, 22)]),
                headgear(), headgear(), headgear(), rng.choice([0, 1, 2, 3, 4]), colour(),
                rng.choice([0, -1, 5]), rng.getrandbits(16), 0, 0, 0, int(rng.random() < 0.3),
                rng.randint(-50, 700), rng.randint(-50, 500), rng.choice([480, 480, 600, 768, 0, 1 << 28]),
                admin, rng.choice([0, 1]), rng.choice([0, 1])]
        script = []
        for _ in range(rng.choice([1, 2, 4, 8, 16])):
            message = rng.choice(LOOK_MESSAGES)
            a, b = rng.choice([0, rng.randint(1, 100), rng.getrandbits(32)]), rng.choice([0, 0, 1, 2])
            record = [0] * path_oracle.ARRIVE_RECORD
            junk = lambda: rng.choice([0, rng.getrandbits(32)])
            new_id = rng.choice(ids)
            by_item = message >= 0x79
            weapon = item() if by_item else kind()
            effect_word = rng.choice([effect(), effect() | 0x40 if b == 2 else effect()])
            if message in (3, 4, 0x79, 0x7C):
                cx, cy = rng.choice(walkable) if rng.random() < 0.9 else (rng.randint(-3, width + 3),
                                                                          rng.randint(-3, height + 3))
                record = [new_id, rng.choice(jobs), rng.choice([0, 1, 1, 2, -1]), cx, cy,
                          rng.choice([0, 1, 2, 3, 4, 5, 6, 7, -1, 9]), rng.choice(path_oracle.SPEEDS), junk(),
                          junk(), hair(), weapon, headgear(), rng.choice([0, 0, 1, 2, 3, -1]), body(), health(),
                          effect_word, junk(), shield(), headgear(), headgear(), colour(), colour(), junk(),
                          rng.choice([0, 1, rng.getrandbits(20), -1]), rng.choice([0, 0, 1, rng.randint(2, 50)]),
                          rng.choice([0, -1, -5, 7]), junk(), 0]
            elif message in (5, 6, 0x7A, 0x7B):
                sx, sy = rng.choice(walkable)
                r = rng.choice([0, 1, 3, 8])
                gx, gy = rng.choice([(sx + rng.randint(-r, r), sy + rng.randint(-r, r)), rng.choice(walkable)])
                time = (server + rng.choice([0, -rng.randint(0, 3000), rng.randint(0, 200)])) & 0xFFFFFFFF
                record = [new_id, rng.choice(jobs), rng.choice([0, 1, 1, 2]), time, sx, sy,
                          rng.choice([8, rng.randint(0, 15)]), rng.choice([8, rng.randint(0, 15)]), gx, gy,
                          rng.choice(path_oracle.SPEEDS), hair(), weapon, headgear(), body(), health(), effect_word,
                          junk(), shield(), headgear(), headgear(), colour(), colour(), junk(),
                          rng.choice([0, 1, rng.getrandbits(20)]), rng.choice([0, 0, 1, rng.randint(2, 50)]),
                          rng.choice([0, -1, 7]), junk()]
            elif message == 0x64:
                a = rng.choice([0, 1, 5, rng.randint(0, 100)])
            step = rng.choice([0, 1, 16, 100, rng.randint(0, 5000)])
            server, tick = (server + step) & 0xFFFFFFFF, (tick + step) & 0xFFFFFFFF
            script += [message, a, b, *record, server, tick]
        lines.append("c " + " ".join(fields + [str(f & 0xFFFFFFFF) if isinstance(f, int) else f
                                               for f in arrive + effect_fields + look]
                                     + [str(len(script) // path_oracle.LOOK_RECORD)]
                                     + [str(v & 0xFFFFFFFF) for v in script]))
    return lines


ITEM_FORMATS = [b"%s: %d ea", b"%s: %d ea", b"%s", b"", b"%s x%d", b"[%s] %+d", b"%-20s|%5d", b"%.4s %04d",
                b"%s \xb0\xb3 %d", b"%s %d%%"]
ITEM_NAMES = [b"Red_Potion", b"Apple", b"\xbb\xe7\xb0\xfa", b"Knife_[3]", b"", b"Jellopy", b"a b_c", b"%s%d",
              b"n" * 63, b"\xc4\xab\xc5\xb8\xb3\xaa"]
ITEM_RESOURCES = [b"\xbb\xe7\xb0\xfa", b"red_potion", b"", b"\xb3\xaa\xc0\xcc\xc7\xc1", b"r" * 100, b"a\\b"]


def item_tables(rng):
    """Four small tables (IdNum2 and Num2 display names, then resource names) of a few ids,
    each table "-" at times, and the ids they know."""
    ids = rng.sample([0, 1, 501, 502, 909, 1201, 4001, 65535, 2147483647, -5], rng.randint(1, 5))
    texts = []
    for _ in path_oracle.ITEM_TABLES[:2]:
        texts.append(b"".join(b"%d#%s#\r\n" % (i, rng.choice(ITEM_NAMES)) for i in ids if rng.random() < 0.8))
    for _ in path_oracle.ITEM_TABLES[2:]:
        texts.append(b"".join(b"%d#%s#\r\n" % (i, rng.choice(ITEM_RESOURCES)) for i in ids if rng.random() < 0.8))
    return [t.hex() if t and rng.random() < 0.9 else "-" for t in texts], ids


def make_items(name, width, height, attrs, count):
    """The script for a map: items made and sent CItem's messages (i, docs/world/items.md), 4 most
    often, on walkable cells and others, off the map, at assorted sixteenths, dropped or not, by
    ids the tables know (as text, with spaces, signs and junk) and do not, identified or not, with
    assorted counts and label formats; then updated through the fall's end and the blinks, at
    times before the message too; drawn shown or not at assorted scales."""
    rng = random.Random(zlib.crc32(b"items " + name.encode()))
    walkable = [(k % width, k // width) for k, a in enumerate(attrs) if a not in (1, 5)] or [(0, 0)]
    lines = []
    for _ in range(count):
        tables, ids = item_tables(rng)
        known = rng.choice(ids)
        text = rng.choice([b"%d" % known, b"%d" % known, b" %d" % known, b"+%d" % known, b"%dx" % known, b"abc",
                           b"", b"%d" % rng.randint(0, 99999), b"4294967797", b"0" * 40 + b"501"])
        cx, cy = rng.choice(walkable) if rng.random() < 0.9 else (rng.randint(-3, width + 3),
                                                                  rng.randint(-3, height + 3))
        sub = lambda: rng.choice([8, 8, rng.randint(0, 15), rng.randint(-20, 40), rng.getrandbits(32)])
        made = rng.getrandbits(32)
        at = (made + rng.choice([0, 0, 16, rng.randint(0, 100000)])) & 0xFFFFFFFF
        fields = [rng.choice([4, 4, 4, 4, 4, 0, 3, 5, rng.getrandbits(32)]), rng.getrandbits(32), cx, cy, sub(), sub(),
                  rng.choice([0, 1, 1, rng.getrandbits(32)]),
                  rng.choice([1, 1, 5, 99, 30000, 0, -1, rng.getrandbits(32)]),
                  rng.choice([0, 1, 1, rng.randint(2, 255), 256]), rng.choice([1, 1, 1, 0]),
                  bits(f32(rng.choice([1.0, 1.0, 0.5, 1.25, 0.0, rng.uniform(0, 3)]))), made, at]
        ticks, tick = [], at
        for _ in range(rng.choice([0, 1, 3, 8, 20, 40])):
            tick = (tick + rng.choice([0, 16, 24, 40, 100, 300, rng.randint(0, 5000)])) & 0xFFFFFFFF
            if rng.random() < 0.1:
                blink = rng.randint(0, 50) * 92 * 24 + rng.choice([2159, 2160, 2183, 2184, 2207, 2208])
                tick = (at + blink) & 0xFFFFFFFF
            elif rng.random() < 0.05:
                tick = (at - rng.randint(1, 5000)) & 0xFFFFFFFF
            ticks.append(tick)
        label = rng.choice(ITEM_FORMATS)
        lines.append("i " + " ".join([text.hex() or "-"] + [str(f & 0xFFFFFFFF) for f in fields] + tables
                                     + [label.hex() or "-"] + [str(t) for t in ticks]))
    return lines


def make_corpses(name, width, height, attrs, count):
    """The script for a map: corpses made by CGameMode::AddCorpse and faded (k,
    docs/world/items.md): assorted names, actions, frames, facings, yaws and frame counts, updated
    through the 255th tick, past it by a little (the alpha -1, kept) and at times before the
    corpse was made."""
    rng = random.Random(zlib.crc32(b"corpses " + name.encode()))
    walkable = [(k % width, k // width) for k, a in enumerate(attrs) if a not in (1, 5)] or [(0, 0)]
    lines = []
    for _ in range(count):
        cx, cy = rng.choice(walkable)
        made = rng.getrandbits(32)
        fields = [bits(f32(centre(cx, width))), bits(f32(rng.uniform(-20, 5))), bits(f32(centre(cy, height))),
                  rng.choice([0, 8, 16, 24, 32, 40, rng.randint(0, 248)]), rng.choice([0, 0, 1, 5, -1, 999]),
                  bits(f32(rng.choice([0.0, 45.0, 180.0, 359.9, -10.0, 720.0, rng.uniform(0, 360)]))),
                  bits(f32(rng.choice([0.0, 45.0, 135.0, 270.0, rng.uniform(0, 360)]))),
                  rng.choice([0, 1, 2, 5, 9]), made]
        ticks, tick = [], made
        for _ in range(rng.choice([1, 3, 8, 40, 400])):
            tick = (tick + rng.choice([0, 16, 24, 40, 100, rng.randint(0, 3000)])) & 0xFFFFFFFF
            if rng.random() < 0.05:
                tick = (made + rng.choice([6119, 6120, 6143, 6144, 6167, 6168])) & 0xFFFFFFFF
            elif rng.random() < 0.02:
                tick = (made - rng.randint(1, 5000)) & 0xFFFFFFFF
            ticks.append(tick)
        names = [rng.choice([b"poring.act", b"\xc6\xf7\xb8\xb5.act", b"", b"a" * 80]) for _ in range(2)]
        lines.append("k " + " ".join([n.hex() or "-" for n in names] + [str(f & 0xFFFFFFFF) for f in fields + ticks]))
    return lines


def main():
    args = path_oracle.arguments("walks per map", 40)
    ours, originals = path_oracle.run_maps(
        args, lambda *a: make_walks(*a) + make_goes(*a) + make_plays(*a) + make_tos(*a)
        + make_stands(*a) + make_enters(*a) + make_arrivals(*a) + make_effects(*a)
        + make_deaths(*a) + make_attacks(*a) + make_landings(*a) + make_casts(*a) + make_updates(*a)
        + make_own_updates(*a) + make_orders(*a) + make_looks(*a) + make_items(*a) + make_corpses(*a),
        "walks")
    totals = {}
    for (_, lines, _), want in zip(ours, originals):
        for line, result in zip(lines, want):
            key = result.split()[0] if line[0] == "F" else {"W": "walk ", "G": "go ", "P": "player ", "T": "to ", "U": "player to ", "N": "stand ",
                   "Y": "player stand ", "E": "enter ",
                   "Q": "player enter ", "A": "arrive ", "B": "arrive with effects ", "X": "effects ",
                   "K": "player effects ", "D": "death ", "H": "player death ", "V": "attack ",
                   "J": "player attack ", "L": "land ", "O": "player land ", "C": "cast ",
                   "R": "player cast ", "u": "update ", "p": "player update ",
                   "o": "own update ", "m": "orders ", "c": "looks ", "i": "item ",
                   "k": "corpse "}[line[0]] + result.split()[0]
            totals[key] = totals.get(key, 0) + 1
            if line[0] in "WGPTUNYEQABDHVJLOCRup" and result != "-":
                totals["updates"] = totals.get("updates", 0) + int(result.split()[1])
            if line[0] == "o":
                totals["own updates"] = totals.get("own updates", 0) + int(result.split()[1])
            if line[0] == "m":
                totals["own messages"] = totals.get("own messages", 0) + int(result.split()[1])
            if line[0] == "c":
                totals["player messages"] = totals.get("player messages", 0) + int(result.split()[1])
            if line[0] in "ik":
                totals[line[0] + " updates"] = totals.get(line[0] + " updates", 0) + int(result.split()[1])
            if line[0] in "XK":
                totals["effect messages"] = totals.get("effect messages", 0) + int(result.split()[1])
    return path_oracle.report(ours, originals, totals, "queries")


if __name__ == "__main__":
    sys.exit(main())
