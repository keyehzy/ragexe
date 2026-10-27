"""Compares the port's ClientState tables with the original (docs/formats/client_tables.md).

usage: .venv/bin/python -I tools/client_tables_oracle.py [--jobs J]

Runs each of ClientState's table loaders in the emulation oracle on its table from
data.grf and from each .gpf patch that carries it, and on synthetic tables through one
loader of each kind:
  - name pairs: mapNameTable.txt (005979d0), mapObjLightTable.txt (00597e10),
    mp3NameTable.txt (00598290), skillNameTable.txt (00593c10);
  - number records: LevelUseskillSpAmount.txt (0059ca60), skillAfterDelay.txt (0059cdd0);
  - line records: skillDescTable.txt (005a54b0);
  - fields: msgStringTable.txt (00598c20), and indoorRswTable.txt (005988c0), sorted;
  - fog records: fogParameterTable.txt (00598e10).
Every record of the vector the loader leaves is compared in order with the matching
`ragres decode-…` mode.
"""
import argparse
import os
import random
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import oracle_pool  # noqa: E402
from emu import Emulator  # noqa: E402

# Each table: its loader, where in ClientState it keeps the records, and their kind.
LOADERS = {
    "mapNameTable.txt": (0x5979D0, 0x1670, "pairs"),
    "mapObjLightTable.txt": (0x597E10, 0x1718, "pairs"),
    "mp3NameTable.txt": (0x598290, 0x1680, "pairs"),
    "skillNameTable.txt": (0x593C10, 0x400, "pairs"),
    "LevelUseskillSpAmount.txt": (0x59CA60, 0x430, "numbers"),
    "skillAfterDelay.txt": (0x59CDD0, 0x440, "numbers"),
    "skillDescTable.txt": (0x5A54B0, 0x420, "lines"),
    "msgStringTable.txt": (0x598C20, 0x16C0, "fields"),
    "indoorRswTable.txt": (0x5988C0, 0x16A0, "sorted"),
    "fogParameterTable.txt": (0x598E10, 0x1690, "fog"),
}
COMMANDS = {"pairs": ["decode-name-pairs"], "numbers": ["decode-number-records"],
            "lines": ["decode-line-records"], "fields": ["decode-fields"],
            "sorted": ["decode-fields", "--sorted"], "fog": ["decode-fog-parameters"]}
# The LevelUseskillSpAmount loader then indexes the records by skill id (0059d1f0): 158
# pointers in a vector at +0x460, found through the skill id names, which a start-up
# routine sets up. The index is skipped here; the records are compared.
INDEX_SP_RECORDS = 0x59D1F0
CLIENT_STATE_SIZE = 0x1800


def fnv1a64(data, h=0xCBF29CE484222325):
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def synthetic_pairs():
    yield b"prontera.rsw#Prontera#\r\ngeffen.rsw#Geffen#\r\n"
    yield b"b#2#\r\na#1#\r\nc#3#\r\na#0#\r\n"                     # sorted, with a duplicate
    yield b"key#value\r\n#lost#\r\nnext#kept#"                   # a line break empties the value
    yield b"k##v#x#y#\r\n#empty first#\r\n##\r\n"                # empty names, a NUL in the second
    yield b"\xc7\xc1\xb7\xd0#\xb0\xa1#\r\nA#B#\r\na#b#"           # bytes above 0x7f sort after ASCII
    yield b"no hash at all\r\n" + b"only#one\n"
    rng = random.Random(1)
    names = [bytes(rng.choice(b"abcAB_.\xb0\xc0") for _ in range(rng.randrange(0, 4))) for _ in range(300)]
    yield b"".join(n + b"#" + bytes([48 + k % 10]) + b"#\r\n" for k, n in enumerate(names))  # many ties
    yield bytes(rng.choice(b"ab#\r\n\0") for _ in range(4000))


def synthetic_numbers():
    yield b"SM_BASH#\r\n8#\r\n15#\r\n@\r\nMG_SIGHT#\r\n10#\r\n@\r\n"
    yield b"B#1#2#@A#3#@A#4#@"                                   # no line breaks, a duplicate
    yield b"  -12x#  7#+3#99999999999#@\r\n#5#@"                   # atoi, and an empty name
    yield b"N\0X#1#@N#2#@"                                        # a NUL in a name sorts as its end
    yield b"open#1#2#"                                            # no '@': no record
    yield b"A#1\r\n2#@\r\nB\n#3#@"                               # line breaks move the field start
    rng = random.Random(2)
    yield b"".join(bytes(rng.choice(b"XYZ") for _ in range(rng.randrange(1, 3))) + b"#" +
                   b"".join(b"%d#" % rng.randrange(-5, 50) for _ in range(rng.randrange(0, 4))) + b"@\r\n"
                   for _ in range(200))
    yield bytes(rng.choice(b"a1#@\r\n\0 -") for _ in range(4000))


def synthetic_lines():
    yield b"NV_BASIC#\r\nBasic Skill\r\nLevel 1\r\n#\r\nSM_SWORD#\r\nSword\r\n#\r\n"
    yield b"B#one\ntwo#A#x#"                                       # bare LF, no breaks
    yield b"#\r\nlate#\r\nline\r\n#"                               # an empty name waits for the next
    yield b"N#a\0b\r\n#"                                           # a NUL in a line ends it
    yield b"open#line\r\n"                                         # no closing '#': no record
    rng = random.Random(3)
    yield b"".join(bytes(rng.choice(b"PQ") for _ in range(rng.randrange(1, 3))) + b"#\r\n" +
                   b"".join(b"l%d\r\n" % rng.randrange(9) for _ in range(rng.randrange(0, 3))) + b"#\r\n"
                   for _ in range(120))
    yield bytes(rng.choice(b"ab#\r\n\0") for _ in range(4000))


def synthetic_fields():
    yield b"Hello#\r\nWorld#\r\n#\r\n"
    yield b"a\r\nb#c\nd#e#"                                     # breaks move the start
    yield b"x\0y#\xb0#B#a#\r\n#\r\n"                               # NULs, high bytes, empties
    yield b"open\r\nfield"                                       # no '#': no field
    rng = random.Random(4)
    yield b"".join(bytes(rng.choice(b"pqPQ_.\xb0") for _ in range(rng.randrange(0, 3))) + b"#\r\n"
                   for _ in range(300))                                 # many ties when sorted
    yield bytes(rng.choice(b"ab#\r\n\0") for _ in range(4000))


def synthetic_fog():
    yield b"gef_fild12.rsw#\r\n0.2#\r\n0.45#\r\n0x2275A9FF#\r\n0.5#\r\nbeach_dun.rsw#1#2#3#4#\r\n"
    yield b"a#1#2#3#\r\n"                                          # three values: no record
    yield b"#\r\nlate#1\n#2#3#4#5#"                                 # an empty name waits
    yield b"N\0#1#2#3#4#M#5#6#7#8#"                                  # a NUL in a name
    rng = random.Random(5)
    yield b"".join(bytes(rng.choice(b"XY") for _ in range(rng.randrange(1, 3))) + b"#" +
                   b"".join(b"%d#" % rng.randrange(9) for _ in range(4)) + b"\r\n" for _ in range(150))
    yield bytes(rng.choice(b"ab1#\r\n\0") for _ in range(4000))


SYNTHETIC = {"mapNameTable.txt": synthetic_pairs, "LevelUseskillSpAmount.txt": synthetic_numbers,
             "skillDescTable.txt": synthetic_lines, "msgStringTable.txt": synthetic_fields,
             "indoorRswTable.txt": synthetic_fields, "fogParameterTable.txt": synthetic_fog}


def init():
    emu = Emulator(max_instructions=500_000_000).init_crt()
    emu.serve_data_files({})
    emu.replace_thiscall(INDEX_SP_RECORDS, 0, lambda this: 0)
    return emu, emu.scratch(size=CLIENT_STATE_SIZE)


def read_string(emu, p):
    data, length = emu.read_u32(p + 4), emu.read_u32(p + 8)
    return emu.mem_read(data, length) if data and length else b""


def read_vector(emu, p, size):
    first, last = emu.read_u32(p + 4), emu.read_u32(p + 8)
    return range(first, last, size) if first else range(0)


def record_line(emu, kind, p):
    name = f"{fnv1a64(read_string(emu, p)):016x}"
    if kind == "pairs":
        return f"P {name} {fnv1a64(read_string(emu, p + 16)):016x}"
    if kind in ("fields", "sorted"):
        return f"F {name}"
    if kind == "numbers":
        values = b"".join(emu.mem_read(q, 4) for q in read_vector(emu, p + 16, 4))
        return f"R {name} {len(values) // 4}:{fnv1a64(values):016x}"
    lines = [read_string(emu, q) for q in read_vector(emu, p + 16, 16)]
    return f"R {name} {len(lines)}:{fnv1a64(b''.join(line + b'\0' for line in lines)):016x}"


def work(state, item):
    emu, obj = state
    table, path = item
    loader, offset, kind = LOADERS[table]
    with open(path, "rb") as f:
        data = f.read()
    mark = emu.heap_mark()
    emu.serve_data_files({"data\\" + table.lower(): data})
    emu.mem_write(obj, bytes(CLIENT_STATE_SIZE))
    emu.call(loader, this=obj)
    records = list(read_vector(emu, obj + offset, 16 if kind in ("fields", "sorted") else 32))
    lines = [f"ok {len(records)}"] + [record_line(emu, kind, p) for p in records]
    emu.heap_release(mark)
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--install", default=os.path.join(ROOT, "original", "install"))
    ap.add_argument("--ragres", default=os.path.join(ROOT, "build", "dev", "src", "ragres"))
    ap.add_argument("--jobs", type=int, default=0, help="worker processes (0: one per core but one)")
    args = ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        items, names = [], []
        archives = ["data.grf"] + sorted(n for n in os.listdir(args.install) if n.endswith(".gpf"))
        for archive in archives:
            out = os.path.join(tmp, archive)
            for table in LOADERS:
                subprocess.run([args.ragres, "extract", os.path.join(args.install, archive), out,
                                f"data\\{table}"], capture_output=True)
                path = os.path.join(out, "data", table.lower())
                if os.path.exists(path):
                    items.append((table, path))
                    names.append(f"{archive}: {table}")
        real = len(items)
        for table, make in SYNTHETIC.items():
            for k, data in enumerate(make()):
                path = os.path.join(tmp, f"synthetic-{table}-{k}")
                with open(path, "wb") as f:
                    f.write(data)
                items.append((table, path))
                names.append(f"synthetic {table} {k}")
        ours = {}
        for kind, command in COMMANDS.items():
            chosen = [n for n, (table, _) in enumerate(items) if LOADERS[table][2] == kind]
            out = subprocess.run([args.ragres, *command, *[items[n][1] for n in chosen]],
                                 capture_output=True, text=True).stdout
            current = None
            for line in out.splitlines():
                if line[0] in "PRF" and line[1] == " ":
                    ours[current].append(line)
                else:
                    k, rest = line.split(" ", 1)
                    current = chosen[int(k)]
                    ours[current] = [rest]
        originals = oracle_pool.run(items, init, work, args.jobs or None, label="client tables")
        mismatches = []
        for n, want in enumerate(originals):
            got = "\n".join(ours.get(n, []))
            if got != want:
                diff = [f"    port     {a}\n    original {b}" for a, b in zip(got.split("\n"), want.split("\n"))
                        if a != b]
                mismatches.append(f"{names[n]}:\n" + "\n".join(diff[:4]))
    print(f"{real} real tables and {len(items) - real} synthetic ones compared, {len(mismatches)} differ")
    for line in mismatches[:20]:
        print("MISMATCH", line)
    print("FAILED" if mismatches else "OK")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
