"""Compares the port's path finder with the original on every map (docs/world/path.md).

usage: .venv/bin/python -I tools/path_oracle.py [--queries N] [--jobs J] [--install DIR] [NAME...]

For each .gat of data.grf and the .gpf patches (the last archive's copy), the original's
C3dAttr::Load (00426ec0) loads it in the emulation oracle and g_PathFinder (00684e30) takes
it, as CGameMode does (004ccc50). A script of queries, made from the map's cells with a
seed of its name, then runs on that finder and one actor's MovePath (CGameActor+0x170):
  - PathFinder::FindPath (004cc5c0) between walkable cells near each other, far apart,
    onto cells that cannot be walked, from the map's edges and from outside it, from a
    cell to itself, at assorted sub-cell points, speeds and times;
  - CGameActor::FindMovePath (00506b20), the server's time (ClientState 00593200) given,
    both before and after the path would have ended;
  - CGameMode::IsPathFound (00511ec0), timeGetTime given;
  - MovePath::FindSegment (004cd1f0) at times before, along and after the path.
Oracle.run also takes "Z value", which sets the path's +0x10, for tools/path_vectors.py, and
"W ...", a walk of the path with CGameActor::ProcessState, for tools/walk_oracle.py.
Every result, the message logged (no path, or the nodes run out), and the whole path after
each search are compared with `ragres find-paths` on the same script.
"""
import argparse
import os
import random
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import oracle_pool  # noqa: E402
import world_oracle  # noqa: E402
from emu import Emulator  # noqa: E402
from emu.winapi import Stub  # noqa: E402
from unicorn.x86_const import UC_X86_REG_FPCW  # noqa: E402

C3DATTR_CTOR, C3DATTR_LOAD = 0x426BA0, 0x426EC0
PATH_FINDER_CTOR, SET_ATTR_MAP, FIND_PATH = 0x4CC1B0, 0x4CCC50, 0x4CC5C0
G_PATH_FINDER, PATH_FINDER_SIZE = 0x684E30, 0x1098
MOVE_PATH_CTOR, FIND_SEGMENT = 0x4CCF10, 0x4CD1F0
FIND_MOVE_PATH, SERVER_TIME, IS_PATH_FOUND = 0x506B20, 0x593200, 0x511EC0
DEBUG_LOG = 0x4060F0
NO_PATH, OVERFLOW = 0x60C1D0, 0x60C1F4
ACTOR_PATH, ACTOR_MOVE_TIME, ACTOR_SIZE = 0x170, 0x18C, 0x300
GAME_ACTOR_VTABLE, PROCESS, SET_STATE = 0x5E95C0, 0x5038C0, 0x5034E0
GET_GAME_MODE, GET_FRAME_COUNT = 0x4CBF30, 0x421A80
# W: the CGameActor fields a walk starts from, in order: +4 and +0xc (float bits), +0x38,
# the camera's yaw, +0x44 and +0x198 (float bits), +0x20, +0x1b8, +0x21c, +0x190, the frames
# of every action, +0x1d0, +0x234, +0x7c, +0x82, +0x1a0 and +0x19c; then the updates, each
# the server's time and timeGetTime.
WALK_FIELDS = ["x", "z", "facing", "yaw", "delay", "travelled", "base", "job", "effect", "pushed",
               "frames", "hold", "fade", "color", "held", "server_at", "processed_at"]
SINGLE = 0x007F
SPEEDS = [150, 150, 150, 100, 200, 75, 300, 120, 1, 7, 999]


def fnv1a64(values, h=0xCBF29CE484222325):
    for v in values:
        for b in struct.pack("<I", v & 0xFFFFFFFF):
            h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def cells(data):
    """The width, height and attributes of a .gat as C3dAttr::Load reads it, or None."""
    if data[:4] != b"GRAT" or data[4] != 1 or data[5] >= 3:
        return None
    width, height = struct.unpack_from("<ii", data, 6)
    count = width * height
    raw = data[14:14 + 20 * count]
    if len(raw) < 20 * count:
        return width, height, [0] * count
    return width, height, [struct.unpack_from("<i", raw, 20 * k + 16)[0] for k in range(count)]


def make_queries(name, width, height, attrs, count):
    """The query script for a map: count searches, with FindSegment queries after some."""
    rng = random.Random(zlib.crc32(name.encode()))
    walkable = [(k % width, k // width) for k, a in enumerate(attrs) if a not in (1, 5)]
    blocked = [(k % width, k // width) for k, a in enumerate(attrs) if a in (1, 5)]
    if not walkable:
        walkable = [(0, 0)]

    def near(x, y, r):
        return x + rng.randint(-r, r), y + rng.randint(-r, r)

    def time():
        return rng.choice([rng.getrandbits(32), rng.randint(0, 100000), 0xFFFFFFFF - rng.randint(0, 3000)])

    def sub():
        return rng.choice([rng.randint(0, 15), rng.randint(0, 15), 0, 8, rng.randint(-20, 40)])

    lines = []
    for _ in range(count):
        sx, sy = rng.choice(walkable)
        kind = rng.random()
        if kind < 0.45:
            gx, gy = near(sx, sy, rng.choice([1, 2, 4, 8, 12, 16]))
        elif kind < 0.6:
            gx, gy = rng.choice(walkable)
        elif kind < 0.7 and blocked:
            gx, gy = rng.choice(blocked)
            sx, sy = near(gx, gy, 6)
        elif kind < 0.75:
            gx, gy = sx, sy
        elif kind < 0.85:
            sx, sy = rng.choice([(0, sy), (sx, 0), (0, 0), (width - 1, sy), (sx, height - 1)])
            gx, gy = near(sx, sy, 6)
        else:
            sx, sy = rng.choice([(-1, sy), (sx, -1), (width, sy), (sx, height), (-3, -2)])
            gx, gy = near(sx, sy, 5)
        speed = rng.choice(SPEEDS)
        what = rng.random()
        if what < 0.2:
            move_time = time()
            now = (move_time + rng.choice([0, rng.randint(0, 3000), rng.randint(0, 20000)])) & 0xFFFFFFFF
            lines.append(f"M {move_time} {now} {sx} {sy} {sub()} {sub()} {gx} {gy} {speed}")
        elif what < 0.3:
            lines.append(f"I {time()} {sx} {sy} {sub()} {sub()} {gx} {gy}")
            continue
        else:
            start = time()
            lines.append(f"F {start} {sx} {sy} {sub()} {sub()} {gx} {gy} {speed}")
        for _ in range(rng.choice([0, 0, 1, 3])):
            lines.append(f"S {time()}")
    return lines


class Oracle:
    def __init__(self):
        emu = Emulator(max_instructions=200_000_000).init_crt()
        emu.uc.reg_write(UC_X86_REG_FPCW, SINGLE)
        self.emu = emu
        self.attr = emu.scratch(size=0x12C)
        self.actor = emu.scratch(size=ACTOR_SIZE)
        self.out = emu.scratch(size=16)
        self.now = 0
        self.logged = []
        emu.mem_write(G_PATH_FINDER, bytes(PATH_FINDER_SIZE))
        emu.call(PATH_FINDER_CTOR, this=G_PATH_FINDER)
        self.server = 0
        emu.replace_thiscall(SERVER_TIME, 0, lambda this: self.server)
        emu.api.stubs["WINMM.DLL!timeGetTime"] = Stub(0, lambda: self.now)

        def debug_log(uc, address, size, _):
            esp = emu.esp()
            self.logged.append(emu.read_u32(esp + 4))
            emu.complete(0, emu.read_u32(esp), esp + 4)
        emu.hook_code(DEBUG_LOG, DEBUG_LOG, debug_log)
        # CGameActor::ProcessState reads the world through the game mode (+0xb4, its attribute map
        # at +0x40) and the camera's yaw through its view (+0xb8, +0x20).
        self.mode, self.world, self.view = (emu.scratch(size=0x100) for _ in range(3))
        emu.write_u32(self.mode + 0xB4, self.world)
        emu.write_u32(self.mode + 0xB8, self.view)
        emu.write_u32(self.world + 0x40, self.attr)
        emu.replace_thiscall(GET_GAME_MODE, 0, lambda this: self.mode)
        self.frames = 1
        emu.replace_thiscall(GET_FRAME_COUNT, 1, lambda this, action: self.frames if self.frames > 1 else 1)
        self.states = []
        emu.replace_thiscall(SET_STATE, 1, lambda this, state: self.states.append(state))

    def result(self, found):
        if found:
            return "found"
        if not self.logged:
            return "same"
        return {NO_PATH: "none", OVERFLOW: "overflow"}.get(self.logged[-1], "?")

    def path(self):
        emu, path = self.emu, self.actor + ACTOR_PATH
        begin, end = emu.read_u32(path + 4), emu.read_u32(path + 8)
        steps = struct.unpack(f"<{(end - begin) // 4}I", emu.mem_read(begin, end - begin)) if begin else ()
        field, sx, sy = struct.unpack("<iII", emu.mem_read(path + 0x10, 12))
        return field, sx, sy, len(steps) // 4, fnv1a64(steps), steps

    def run(self, data, lines):
        emu = self.emu
        mark = emu.heap_mark()
        emu.serve_data_files({"data\\t.gat": data})
        emu.mem_write(self.attr, bytes(0x12C))
        emu.call(C3DATTR_CTOR, this=self.attr)
        emu.call(C3DATTR_LOAD, emu.cstr("t.gat"), this=self.attr)
        emu.mem_write(G_PATH_FINDER, bytes(PATH_FINDER_SIZE))
        emu.call(PATH_FINDER_CTOR, this=G_PATH_FINDER)
        emu.call(SET_ATTR_MAP, self.attr, this=G_PATH_FINDER)
        emu.mem_write(self.actor, bytes(ACTOR_SIZE))
        emu.call(MOVE_PATH_CTOR, this=self.actor + ACTOR_PATH)
        out = []
        self.paths = []
        for line in lines:
            kind, *values = line.split()
            v = [int(x) for x in values] if kind != "W" else []
            self.logged = []
            if kind == "F":
                found = emu.call(FIND_PATH, *v, self.actor + ACTOR_PATH, this=G_PATH_FINDER).eax & 0xFF
                field, sx, sy, count, digest, steps = self.path()
                self.paths.append(steps)
                out.append(f"{self.result(found)} - {field} {sx:08x} {sy:08x} {count} {digest:016x}")
            elif kind == "M":
                emu.write_u32(self.actor + ACTOR_MOVE_TIME, v[0])
                self.server = v[1]
                found = emu.call(FIND_MOVE_PATH, *v[2:], this=self.actor).eax & 0xFF
                field, sx, sy, count, digest, steps = self.path()
                self.paths.append(steps)
                move_time = emu.read_u32(self.actor + ACTOR_MOVE_TIME)
                out.append(f"{self.result(found)} {move_time} {field} {sx:08x} {sy:08x} {count} {digest:016x}")
            elif kind == "I":
                self.now = v[0]
                found = emu.call(IS_PATH_FOUND, *v[1:], this=0).eax & 0xFF
                out.append(f"{1 if found else 0}")
            elif kind == "S":
                if self.path()[3] < 2:
                    out.append("-")
                    continue
                index = emu.call(FIND_SEGMENT, v[0], self.out, self.out + 4, self.out + 8,
                                 this=self.actor + ACTOR_PATH).eax
                index -= (index & 0x80000000) << 1
                time, x, y = struct.unpack("<III", emu.mem_read(self.out, 12))
                out.append(f"{index} {time} {x:08x} {y:08x}")
            elif kind == "Z":
                emu.write_u32(self.actor + ACTOR_PATH + 0x10, v[0] & 0xFFFFFFFF)
                out.append(f"{v[0]}")
            elif kind == "W":
                out.append(self.walk(values))
        emu.heap_release(mark)
        return out

    def walk(self, values):
        """CGameActor::ProcessState on the actor walking its path, update by update until it arrives
        or has faded."""
        emu, actor = self.emu, self.actor
        if self.path()[3] < 2:
            return "-"
        v = [int(x, 0) for x in values]
        w = dict(zip(WALK_FIELDS, v))
        updates = v[len(WALK_FIELDS):]
        emu.write_u32(actor, GAME_ACTOR_VTABLE)
        for offset, name in [(0x4, "x"), (0xC, "z"), (0x38, "facing"), (0x44, "delay"), (0x198, "travelled"),
                             (0x20, "base"), (0x1B8, "job"), (0x21C, "effect"), (0x1D0, "hold"),
                             (0x234, "fade"), (0x7C, "color"), (0x1A0, "server_at"), (0x19C, "processed_at")]:
            emu.write_u32(actor + offset, w[name] & 0xFFFFFFFF)
        for offset in (0x8, 0x24, 0x28, 0x194):
            emu.write_u32(actor + offset, 0)
        emu.write_u32(actor + 0x58, 1)
        emu.write_u32(actor + 0x9C, actor)
        emu.mem_write(actor + 0x190, bytes([w["pushed"]]))
        emu.mem_write(actor + 0x82, bytes([w["held"]]))
        emu.write_u32(self.view + 0x20, w["yaw"])
        self.frames = w["frames"]
        h, calls, last = 0xCBF29CE484222325, 0, "walking"
        for server, tick in zip(updates[::2], updates[1::2]):
            self.states = []
            self.now, self.server = tick, server
            ret = emu.call(PROCESS, this=actor).eax & 0xFF
            calls += 1
            state = self.states[0] if self.states else 0xFFFFFFFF
            fields = [ret, *(emu.read_u32(actor + o) for o in (0x4, 0x8, 0xC, 0x38, 0x24, 0x28, 0x194, 0x198,
                                                                0x19C, 0x1A0, 0x7C)),
                      emu.mem_read(actor + 0x82, 1)[0], emu.mem_read(actor + 0x190, 1)[0], state]
            h = fnv1a64(fields, h)
            if ret == 0:
                last = "faded"
                break
            if self.states:
                last = "arrived"
                break
        r = [emu.read_u32(actor + o) for o in (0x4, 0x8, 0xC, 0x38, 0x24, 0x28, 0x198)]
        return (f"{last} {calls} {r[0]:08x} {r[1]:08x} {r[2]:08x} {r[3]:08x} {r[4]} {r[5]} {r[6]:08x} "
                f"{h:016x}")


def init():
    return Oracle()


def work(oracle, item):
    path, lines = item
    with open(path, "rb") as f:
        return oracle.run(f.read(), lines)


def arguments(queries_help, queries=200):
    ap = argparse.ArgumentParser()
    ap.add_argument("names", nargs="*", help="maps to check (all by default)")
    ap.add_argument("--install", default=os.path.join(ROOT, "original", "install"))
    ap.add_argument("--ragres", default=os.path.join(ROOT, "build", "dev", "src", "ragres"))
    ap.add_argument("--queries", type=int, default=queries, help=queries_help)
    ap.add_argument("--jobs", type=int, default=0, help="worker processes (0: one per core but one)")
    return ap.parse_args()


def run_maps(args, make_script, label):
    """Each map's script, from make_script(name, width, height, attrs, count), run by ragres
    and by the original: [(name, lines, port's results)] and the original's results."""
    with tempfile.TemporaryDirectory() as tmp:
        for archive in world_oracle.archives(args.install):
            subprocess.run([args.ragres, "extract", archive, tmp, "*.gat"], check=True, capture_output=True)
        gats = {os.path.splitext(n)[0].lower(): os.path.join(d, n)
                for d, _, ns in os.walk(tmp) for n in ns if n.lower().endswith(".gat")}
        names = sorted(args.names or gats)
        missing = [n for n in names if n not in gats]
        if missing:
            sys.exit(f"no such maps: {', '.join(missing)}")
        items, ours = [], []
        for name in names:
            with open(gats[name], "rb") as f:
                decoded = cells(f.read())
            if decoded is None:
                continue
            lines = make_script(name, *decoded, args.queries)
            script = os.path.join(tmp, name + ".queries")
            with open(script, "w") as f:
                f.write("\n".join(lines) + "\n")
            result = subprocess.run([args.ragres, "find-paths", gats[name], script], capture_output=True, text=True)
            items.append((gats[name], lines))
            ours.append((name, lines, result.stdout.splitlines()))
        originals = oracle_pool.run(items, init, work, args.jobs or None, label=label)
    return ours, originals


def report(ours, originals, totals, what):
    """Prints the maps whose results differ; 0 when none do."""
    differing = []
    for (name, lines, got), want in zip(ours, originals):
        bad = [(line, g, w) for line, g, w in zip(lines, got + [None] * len(lines), want) if g != w]
        if bad or len(got) != len(want):
            differing.append((name, bad))
    queries = sum(len(w) for w in originals)
    print(f"{len(ours)} maps, {queries} {what}: " + ", ".join(f"{k} {v}" for k, v in sorted(totals.items())))
    for name, bad in differing[:20]:
        print(f"MISMATCH {name}: {len(bad)} queries differ")
        for line, g, w in bad[:3]:
            print(f"    {line[:300]}\n        port     {g}\n        original {w}")
    print(f"{len(differing)} maps differ")
    print("FAILED" if differing else "OK")
    return 1 if differing else 0


def main():
    args = arguments("searches per map")
    ours, originals = run_maps(args, make_queries, "paths")
    totals = {}
    for (name, lines, got), want in zip(ours, originals):
        for line, result in zip(lines, want):
            kind = line[0]
            key = result.split()[0] if kind in "FM" else {"I": "IsPathFound", "S": "FindSegment"}[kind]
            totals[key] = totals.get(key, 0) + 1
    return report(ours, originals, totals, "queries")


if __name__ == "__main__":
    sys.exit(main())
