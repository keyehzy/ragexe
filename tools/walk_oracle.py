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


def main():
    args = path_oracle.arguments("walks per map", 40)
    ours, originals = path_oracle.run_maps(
        args, lambda *a: make_walks(*a) + make_goes(*a) + make_plays(*a) + make_tos(*a)
        + make_stands(*a) + make_enters(*a) + make_arrivals(*a) + make_effects(*a)
        + make_deaths(*a) + make_attacks(*a), "walks")
    totals = {}
    for (_, lines, _), want in zip(ours, originals):
        for line, result in zip(lines, want):
            key = result.split()[0] if line[0] == "F" else {"W": "walk ", "G": "go ", "P": "player ", "T": "to ", "U": "player to ", "N": "stand ",
                   "Y": "player stand ", "E": "enter ",
                   "Q": "player enter ", "A": "arrive ", "B": "arrive with effects ", "X": "effects ",
                   "K": "player effects ", "D": "death ", "H": "player death ", "V": "attack ",
                   "J": "player attack "}[line[0]] + result.split()[0]
            totals[key] = totals.get(key, 0) + 1
            if line[0] in "WGPTUNYEQABDHVJ" and result != "-":
                totals["updates"] = totals.get("updates", 0) + int(result.split()[1])
            if line[0] in "XK":
                totals["effect messages"] = totals.get("effect messages", 0) + int(result.split()[1])
    return path_oracle.report(ours, originals, totals, "queries")


if __name__ == "__main__":
    sys.exit(main())
