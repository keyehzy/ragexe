"""Compares the port's item and card tables with the original (docs/formats/item_tables.md).

usage: .venv/bin/python -I tools/item_tables_oracle.py [--jobs J]

Runs LoadItemTables (004d48e0) in the emulation oracle on sets of table files and compares
what it leaves in g_itemInfoById, g_cardInfoById, g_metalProcessItems and the two
fallbacks with `ragres decode-item-tables`:
  - the tables of data.grf, and with each .gpf patch's tables over them, for the American
    service and for the Korean one, whose descriptions have their alphabet reversed;
  - synthetic sets, each a few small tables that exercise one rule.
Some synthetic sets make the client run past its buffers or go round a table without end;
there the port must fail, and the original's result is only counted.

The code page stays 0: the emulator's CharNextExA is single-byte only.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import oracle_pool  # noqa: E402
from emu import EmulationError, Emulator  # noqa: E402

LOAD_ITEM_TABLES = 0x4D48E0
MAP_INITS = (0x4D3440, 0x4D35A0, 0x4D3700)  # static_init_122/123/124_body
ITEM_MAP, CARD_MAP, METAL_MAP = 0x67CC50, 0x67CC38, 0x67CC20
ITEM_NIL, CARD_NIL, METAL_NIL = 0x67CC64, 0x67CC6C, 0x67CC74
ITEM_FALLBACK, ITEM_GUARD, ITEM_SIZE = 0x689140, 0x689130, 0x78
CARD_FALLBACK, CARD_GUARD, CARD_SIZE = 0x6891C0, 0x6891B8, 0x30
SERVICE_TYPE, CODE_PAGE = 0x67C9D4, 0x67C998
BUDGET = 400_000_000
TABLES = ["Num2ItemDisplayNameTable.txt", "Num2ItemResNameTable.txt", "Num2itemDescTable.txt",
          "IdNum2ItemDisplayNameTable.txt", "IdNum2ItemResNameTable.txt", "IdNum2itemDescTable.txt",
          "itemSlotTable.txt", "itemParamTable.txt", "itemSlotCountTable.txt",
          "CardItemNameTable.txt", "Num2CardIllustNameTable.txt", "CardPrefixNameTable.txt",
          "CardPostfixNameTable.txt", "MetalProcessItemList.txt"]
EXPECT_FAIL = "port fails: "


def fnv1a64(data, h=0xCBF29CE484222325):
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def lines_hash(lines):
    return f"{len(lines)}:{fnv1a64(b''.join(line + b'\0' for line in lines)):016x}"


def synthetic():
    """(name, {table: bytes}, korean, expectation)."""
    yield "keys: spaces, signs, junk, wrapping", {
        "Num2ItemDisplayNameTable.txt": b"  12#Twelve#\r\n-3#Minus_three#\r\n+7#Plus#\r\n"
                                        b"9a#Nine#\r\n4294967297#Wraps#\r\n#Empty_key#\r\n",
    }, False, ""
    yield "duplicates: the last name wins, params pile up", {
        "Num2ItemDisplayNameTable.txt": b"5#First#\r\n5#Second#\r\n",
        "itemParamTable.txt": b"5#1#2#\r\n5#3#4#\r\n6#-1#99999999999#\r\n",
        "itemSlotTable.txt": b"5#8#\r\n7#2#\r\n",
        "itemSlotCountTable.txt": b"5#3#\r\n",
    }, False, ""
    yield "descriptions: lines before the first id, empty lines, digits", {
        "Num2itemDescTable.txt": b"stray line\r\n501#\r\nA potion.\r\n\r\n12 34\r\n0\r\nZero_item\r\n502\r\n"
                                 b"Second.\r\n#\r\n",
        "IdNum2itemDescTable.txt": b"501#\r\nIdentified ^ff0000red^000000\r\n#\r\n",
    }, False, ""
    yield "descriptions for the Korean service", {
        "Num2itemDescTable.txt": b"501#\r\nAbc xyz ABC XYZ \xb0\xbf\xc0 09\r\n#\r\n",
    }, True, ""
    yield "line endings: LF only, CR only, and a field left open", {
        "Num2ItemDisplayNameTable.txt": b"1#One#\n2#Two#\n3#Three#\nFour",
        "Num2itemDescTable.txt": b"1#\nlost to LF\n2#\rkept by CR\r",
        "Num2ItemResNameTable.txt": b"1#a#2#b\r\n#3#c#",
    }, False, ""
    yield "a NUL inside a field", {
        "Num2ItemResNameTable.txt": b"1#ab\0cd#2#\0#",
    }, False, ""
    yield "cards: names, illustrations, prefixes moved to postfixes, card 0", {
        "CardItemNameTable.txt": b"4001#4002#4003#",
        "Num2CardIllustNameTable.txt": b"4001#poring#0#zero#",
        "CardPrefixNameTable.txt": b"4001#Lucky#4002#Hard#4004#Odd#",
        "CardPostfixNameTable.txt": b"4002#4005#",
    }, False, ""
    yield "item 0 in the tables", {
        "IdNum2ItemDisplayNameTable.txt": b"0#Nothing#\r\n",
        "Num2itemDescTable.txt": b"0\r\nline\r\n",
    }, False, ""
    yield "metal process list", {
        "MetalProcessItemList.txt": b"skip me\r\n1000\r\nOre\r\n\r\n1001\r\nSteel\r\nIron\r\n12x\r\n",
    }, False, ""
    yield "a name of 255 bytes, a line of 1023", {
        "Num2ItemDisplayNameTable.txt": b"1#" + b"n" * 255 + b"#",
        "Num2itemDescTable.txt": b"1\r\n" + b"d" * 1023 + b"\r\n",
    }, False, ""
    yield "a name of 256 bytes", {"Num2ItemDisplayNameTable.txt": b"1#" + b"n" * 256 + b"#"}, False, \
        EXPECT_FAIL + "the name overruns the 256-byte buffer"
    yield "a description line of 1024 bytes", {"Num2itemDescTable.txt": b"1\r\n" + b"d" * 1024 + b"\r\n"}, \
        False, EXPECT_FAIL + "the line overruns the 1024-byte buffer"
    yield "a pair table with an odd field count", {"Num2ItemResNameTable.txt": b"1#a#2#"}, False, \
        EXPECT_FAIL + "the last record reads the list's head node"
    yield "a triple table cut short", {"itemParamTable.txt": b"1#2#3#4#5#"}, False, \
        EXPECT_FAIL + "the last record reads the list's head node"
    yield "a pair table cut short by a field left open", {"Num2ItemDisplayNameTable.txt": b"1#One#\n2#Two"},\
        False, EXPECT_FAIL + "the last record reads the list's head node"
    yield "no tables at all", {}, False, ""


def init():
    emu = Emulator(max_instructions=BUDGET).init_crt()
    emu.serve_data_files({})
    return emu


def read_string(emu, p):
    data, length = emu.read_u32(p + 4), emu.read_u32(p + 8)
    return emu.mem_read(data, length) if data and length else b""


def read_strings(emu, p):
    first, last = emu.read_u32(p + 4), emu.read_u32(p + 8)
    return [read_string(emu, q) for q in range(first, last, 16)] if first else []


def read_ints(emu, p):
    first, last = emu.read_u32(p + 4), emu.read_u32(p + 8)
    return list(struct_ints(emu.mem_read(first, last - first))) if first else []


def struct_ints(data):
    for k in range(0, len(data), 4):
        yield int.from_bytes(data[k:k + 4], "little", signed=True)


def nodes(emu, tree, nil_slot):
    """The nodes of a VC6 map in key order: the head's parent is the root."""
    nil = emu.read_u32(nil_slot)
    out, stack, node = [], [], emu.read_u32(emu.read_u32(tree + 4) + 4)
    while stack or node != nil:
        while node != nil:
            stack.append(node)
            node = emu.read_u32(node)
        node = stack.pop()
        out.append(node)
        node = emu.read_u32(node + 8)
    return out


def item_line(emu, key, p):
    params = read_ints(emu, p + 0x68)
    blob = b"".join((v & 0xFFFFFFFF).to_bytes(4, "little") for v in params)
    h = lambda b: f"{fnv1a64(b):016x}"  # noqa: E731
    slot = int.from_bytes(emu.mem_read(p + 0x60, 4), "little", signed=True)
    count = int.from_bytes(emu.mem_read(p + 0x64, 4), "little", signed=True)
    return (f"I {key} {h(read_string(emu, p))} {h(read_string(emu, p + 0x10))} "
            f"{lines_hash(read_strings(emu, p + 0x20))} {h(read_string(emu, p + 0x30))} "
            f"{h(read_string(emu, p + 0x40))} {lines_hash(read_strings(emu, p + 0x50))} {slot} {count} "
            f"{len(params)}:{fnv1a64(blob):016x}")


def card_line(emu, key, p):
    h = lambda b: f"{fnv1a64(b):016x}"  # noqa: E731
    return f"C {key} {h(read_string(emu, p))} {h(read_string(emu, p + 0x10))} {h(read_string(emu, p + 0x20))}"


def work(emu, item):
    directory, korean = item
    files = {}
    for name in os.listdir(os.path.join(directory, "data")):
        with open(os.path.join(directory, "data", name), "rb") as f:
            files["data\\" + name.lower()] = f.read()
    mark = emu.heap_mark()
    emu.serve_data_files(files)
    for f in MAP_INITS:
        emu.call(f)
    emu.mem_write(ITEM_FALLBACK, bytes(ITEM_SIZE))
    emu.mem_write(CARD_FALLBACK, bytes(CARD_SIZE))
    emu.write_u32(ITEM_GUARD, 0)
    emu.write_u32(CARD_GUARD, 0)
    emu.write_u32(SERVICE_TYPE, 0 if korean else 1)
    emu.write_u32(CODE_PAGE, 0)
    try:
        emu.call(LOAD_ITEM_TABLES)
    except EmulationError as e:
        emu.heap_release(mark)
        return "does not return" if "did not return" in str(e) else "faults"
    lines = []
    for node in nodes(emu, ITEM_MAP, ITEM_NIL):
        lines.append(item_line(emu, int.from_bytes(emu.mem_read(node + 0xC, 4), "little", signed=True), node + 0x10))
    lines.append(item_line(emu, "fallback", ITEM_FALLBACK))
    for node in nodes(emu, CARD_MAP, CARD_NIL):
        lines.append(card_line(emu, int.from_bytes(emu.mem_read(node + 0xC, 4), "little", signed=True), node + 0x10))
    lines.append(card_line(emu, "fallback", CARD_FALLBACK))
    for node in nodes(emu, METAL_MAP, METAL_NIL):
        key = int.from_bytes(emu.mem_read(node + 0xC, 4), "little", signed=True)
        lines.append(f"M {key} {lines_hash(read_strings(emu, node + 0x10))}")
    emu.heap_release(mark)
    return "\n".join(lines)


def extract(ragres, archive, out):
    # extract stops at the first name the archive does not have, so one at a time
    for t in TABLES:
        subprocess.run([ragres, "extract", archive, out, f"data\\{t}"], capture_output=True)
    return os.path.isdir(os.path.join(out, "data"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--install", default=os.path.join(ROOT, "original", "install"))
    ap.add_argument("--ragres", default=os.path.join(ROOT, "build", "dev", "src", "ragres"))
    ap.add_argument("--jobs", type=int, default=0, help="worker processes (0: one per core but one)")
    args = ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        sets = []  # (name, directory, korean, expectation)
        base = os.path.join(tmp, "data.grf")
        extract(args.ragres, os.path.join(args.install, "data.grf"), base)
        for korean in (False, True):
            sets.append((f"data.grf{' (Korean)' if korean else ''}", base, korean, ""))
        for patch in sorted(n for n in os.listdir(args.install) if n.endswith(".gpf")):
            over = os.path.join(tmp, "over-" + patch)
            if not extract(args.ragres, os.path.join(args.install, patch), over):
                continue
            merged = os.path.join(tmp, "merged-" + patch)
            shutil.copytree(base, merged)
            for name in os.listdir(os.path.join(over, "data")):
                shutil.copy(os.path.join(over, "data", name), os.path.join(merged, "data", name))
            sets.append((f"data.grf with {patch}", merged, False, ""))
        for k, (name, tables, korean, expectation) in enumerate(synthetic()):
            directory = os.path.join(tmp, f"synthetic{k}")
            os.makedirs(os.path.join(directory, "data"))
            for table, data in tables.items():
                with open(os.path.join(directory, "data", table.lower()), "wb") as f:
                    f.write(data)
            sets.append((name, directory, korean, expectation))
        originals = oracle_pool.run([(d, k) for _, d, k, _ in sets], init, work, args.jobs or None,
                                    label="item tables")
        mismatches, refused = [], 0
        for (name, directory, korean, expectation), want in zip(sets, originals):
            command = [args.ragres, "decode-item-tables", directory] + (["--korea"] if korean else [])
            got = subprocess.run(command, capture_output=True, text=True).stdout.strip()
            if expectation:
                refused += 1
                if not got.startswith("failed"):
                    mismatches.append(f"{name}: expected the port to fail "
                                      f"({expectation[len(EXPECT_FAIL):]}); the original {want}")
                continue
            if got != want:
                ours, theirs = got.split("\n"), want.split("\n")
                diff = [f"    port     {a}\n    original {b}" for a, b in zip(ours, theirs) if a != b]
                if len(ours) != len(theirs):
                    diff.append(f"    {len(ours)} lines against {len(theirs)}")
                mismatches.append(f"{name}:\n" + "\n".join(diff[:4]))
    real = sum(1 for name, *_ in sets if name.startswith("data.grf"))
    print(f"{real} sets of real tables and {len(sets) - real} synthetic ones compared "
          f"({refused} that the port must refuse), {len(mismatches)} differ")
    for line in mismatches[:20]:
        print("MISMATCH", line)
    print("FAILED" if mismatches else "OK")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
