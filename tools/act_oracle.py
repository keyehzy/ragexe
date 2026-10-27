"""Compares the port's action loader with the original (docs/formats/act.md).

usage: .venv/bin/python -I tools/act_oracle.py [--sample N] [--scales 00,11] [--jobs J]

Runs CActRes::Load (00420f60) in the emulation oracle on every .act of data.grf, with
the original resource manager and the sprite type registered, so the matching .spr
really loads, at each detail level given (halve x, halve y). Compares every field with
`ragres decode-acts`. An action whose original load faults (a missing sprite) must fail
in the port.
"""
import argparse
import hashlib
import os
import random
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import oracle_pool  # noqa: E402
from emu import EmulationError, Emulator  # noqa: E402
from sprite_oracle import replace_textures  # noqa: E402

# analysis/labels: CSprRes and CActRes constructors and slots, ResourceManager
CSPRRES_REGISTER, G_CSPRRES = 0x433F00, 0x6414A0
CACTRES_CTOR, CACTRES_LOAD, CACTRES_RESET = 0x420A70, 0x420F60, 0x420D60
G_RESOURCE_MANAGER, RESOURCE_MANAGER_CLEAR = 0x67CD88, 0x4E0310
HALVE_X, HALVE_Y = 0x688A7C, 0x688A80


def port_dump(ragres, files, scale):
    """{n: [lines]} from ragres decode-acts, in batches."""
    flags = (["--halve-x"] if scale[0] else []) + (["--halve-y"] if scale[1] else [])
    results = {}
    for start in range(0, len(files), 200):
        out = subprocess.run([ragres, "decode-acts", *flags, *files[start:start + 200]],
                             capture_output=True, text=True).stdout
        current = None
        for line in out.splitlines():
            if line.startswith("A "):
                fields = line.split(" ")
                current = start + int(fields[1])
                results[current] = ["A " + ("failed" if fields[2] == "failed" else " ".join(fields[2:]))]
            else:
                results[current].append(line)
    return results


def init():
    emu = Emulator(max_instructions=2_000_000_000).init_crt()
    replace_textures(emu)
    # The sprite type's registration, as static initializer 00433ee0 does.
    emu.call(CSPRRES_REGISTER, 0, emu.cstr("spr"), emu.cstr("sprite\\"), this=G_CSPRRES)
    obj = emu.scratch(size=0x144)
    return emu, obj


def u32(emu, a):
    return emu.read_u32(a)


def i32(emu, a):
    return struct.unpack("<i", emu.mem_read(a, 4))[0]


def dump(emu, obj):
    actions_begin, actions_end = u32(emu, obj + 0x114), u32(emu, obj + 0x118)
    events_begin, events_end = u32(emu, obj + 0x128), u32(emu, obj + 0x12C)
    delays_begin, delays_end = u32(emu, obj + 0x138), u32(emu, obj + 0x13C)
    version = None
    lines = []
    actions = range(actions_begin, actions_end, 16) if actions_begin else []
    for a in actions:
        fb, fe = u32(emu, a + 4), u32(emu, a + 8)
        frames = range(fb, fe, 0x4C) if fb else []
        lines.append(f"a {len(frames)}")
        for f in frames:
            header = struct.unpack("<8i", emu.mem_read(f, 32))
            ib, ie = u32(emu, f + 0x24), u32(emu, f + 0x28)
            nb, ne = u32(emu, f + 0x3C), u32(emu, f + 0x40)
            items = range(ib, ie, 0x24) if ib else []
            anchors = range(nb, ne, 12) if nb else []
            lines.append("f " + " ".join(map(str, header)) + f" {i32(emu, f + 0x34)} {len(items)} {len(anchors)}")
            for it in items:
                x, y, image, flags = struct.unpack("<iiiI", emu.mem_read(it, 16))
                r, g, b, al = emu.mem_read(it + 16, 4)
                sx, sy, angle, kind = struct.unpack("<IIii", emu.mem_read(it + 20, 16))
                lines.append(f"i {x} {y} {image} {flags} {r} {g} {b} {al} {sx:08x} {sy:08x} {angle} {kind}")
            for an in anchors:
                lines.append("n " + " ".join(map(str, struct.unpack("<iii", emu.mem_read(an, 12)))))
    events = range(events_begin, events_end, 16) if events_begin else []
    for e in events:
        buf, length = u32(emu, e + 4), u32(emu, e + 8)
        lines.append("e " + (emu.mem_read(buf, length).hex() if buf and length else ""))
    delays = range(delays_begin, delays_end, 4) if delays_begin else []
    for d in delays:
        lines.append(f"d {u32(emu, d):08x}")
    return lines, len(actions), len(events), i32(emu, obj + 0x120)


def work(state, job):
    emu, obj = state
    act_path, scale = job
    with open(act_path, "rb") as f:
        act = f.read()
    files = {"data\\sprite\\t.act": act}
    spr_path = act_path[:-4] + ".spr"
    if os.path.exists(spr_path):
        with open(spr_path, "rb") as f:
            files["data\\sprite\\t.spr"] = f.read()
    mark = emu.heap_mark()
    emu.write_u32(HALVE_X, scale[0])
    emu.write_u32(HALVE_Y, scale[1])
    emu.serve_data_files(files)
    emu.mem_write(obj, bytes(0x144))
    emu.call(CACTRES_CTOR, this=obj)
    try:
        ok = emu.call(CACTRES_LOAD, emu.cstr("sprite\\t.act"), this=obj).eax & 0xFF
    except EmulationError:
        ok = None
    if ok:
        lines, n_actions, n_events, max_items = dump(emu, obj)
        version = struct.unpack_from("<H", act, 2)[0]
        lines.insert(0, f"A {version:x} {n_actions} {n_events} {max_items}")
    else:
        lines = ["A failed" if ok == 0 else "A fault"]
    if ok is not None:
        emu.call(CACTRES_RESET, this=obj)
        emu.call(RESOURCE_MANAGER_CLEAR, this=G_RESOURCE_MANAGER)
    emu.heap_release(mark)
    return hashlib.sha1("\n".join(lines).encode()).hexdigest(), lines if ok is None or not ok else None, lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--archive", default=os.path.join(ROOT, "original", "install", "data.grf"))
    ap.add_argument("--ragres", default=os.path.join(ROOT, "build", "dev", "src", "ragres"))
    ap.add_argument("--sample", type=int, default=0, help="check N actions (0: all)")
    ap.add_argument("--scales", default="00,11", help="detail levels: halve x and halve y digits")
    ap.add_argument("--jobs", type=int, default=0, help="worker processes (0: one per core but one)")
    args = ap.parse_args()
    scales = [(int(s[0]), int(s[1])) for s in args.scales.split(",")]

    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([args.ragres, "extract", args.archive, tmp, "*.act", "*.spr"], check=True,
                       capture_output=True)
        files = sorted(os.path.join(d, n) for d, _, ns in os.walk(tmp) for n in ns if n.lower().endswith(".act"))
        if args.sample:
            files = sorted(random.Random(1).sample(files, min(args.sample, len(files))))
        mismatches, loads, faults, items = [], 0, 0, 0
        for scale in scales:
            ours = port_dump(args.ragres, files, scale)
            results = oracle_pool.run([(f, scale) for f in files], init, work, args.jobs or None,
                                      label=f"act {scale[0]}{scale[1]}")
            for n, (path, (_, _, want)) in enumerate(zip(files, results)):
                got = ours.get(n, ["A failed"])
                loads += 1
                if want == ["A fault"]:
                    faults += 1
                    if got[0] != "A failed":
                        mismatches.append(f"{os.path.relpath(path, tmp)} at {scale}: the original faults, the port loads it")
                    continue
                items += sum(1 for l in want if l.startswith("i "))
                if got != want:
                    k = next((k for k, (a, b) in enumerate(zip(got, want)) if a != b), min(len(got), len(want)))
                    mismatches.append(f"{os.path.relpath(path, tmp)} at {scale}: line {k}: port "
                                      f"{got[k:k + 1]}, original {want[k:k + 1]}")
    print(f"{len(files)} actions at {len(scales)} detail levels: {loads} loads, {items} items compared, "
          f"{faults} loads where the original faults (the port must fail), {len(mismatches)} differ")
    for line in mismatches[:20]:
        print("MISMATCH", line)
    print("FAILED" if mismatches else "OK")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
