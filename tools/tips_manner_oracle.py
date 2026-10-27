"""Compares the port's tip-of-the-day and manner filter with the original
(docs/formats/tips.md, docs/formats/manner.md).

usage: .venv/bin/python -I tools/tips_manner_oracle.py [--jobs J]

In the emulation oracle:
  - TipOfTheDayList::Load (004db290) on the install folder's tip files and on synthetic
    ones, with the service type that picks each file name. The client opens them with
    fopen in text mode, so the C library's own CR LF and Ctrl-Z handling runs too.
  - MannerFilter::Load (004d25e0) on data.grf's manner.txt and synthetic word lists, then
    IsClean (004d2220) on a set of probe strings for each.
Every tip and line, every word, and every probe's answer is compared with
`ragres decode-tips` and `ragres decode-manner`.
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

TIPS_CTOR, TIPS_LOAD = 0x4DB1D0, 0x4DB290
MANNER_CTOR, MANNER_LOAD, MANNER_IS_CLEAN = 0x4D2A00, 0x4D25E0, 0x4D2220
SERVICE_TYPE = 0x67C9D4
TIP_FILES = {1: "tipOfTheday_e.txt", 2: "tipOfTheday_j.txt", 0: "tipOfTheday.txt"}


def quoted(text):
    out = '"'
    for b in text:
        c = chr(b)
        if c in '"\\':
            out += "\\" + c
        elif b < 0x20 or b > 0x7E:
            out += f"\\x{b:02x}"
        else:
            out += c
    return out + '"'


PROBES = [b"hello", b"you fuck", b"FUCK YOU", b"f u c k", b"FuCk", b"bitch please", b"clean words only",
          b"  ass  ", b"\tword", b"word ", b"WORD", b"\xb0\xa1fuck", b"\xb0FUCK", b"\xb0\xa1\xb0\xa1",
          b"x\xffmatch", b"MATCH", b"a b c", b"", b"embedded\x00fuck", b"\xc7\xd1\xb1\xdb"]


def synthetic_tips():
    """(service type, file bytes)."""
    yield 1, b"first tip\r\n#\r\nsecond\r\n  indented\r\n#\r\nnot closed\r\n"
    yield 2, b"tip one#tip two\n#\n"
    yield 0, b"lone\rCR\r\r\n#CR CR LF\r\r\n#"
    yield 1, b"before\x1a after Ctrl-Z#\r\n"                         # Ctrl-Z ends the file
    yield 1, b"line\x00cut\r\n#nul\x00#\r\n"
    yield 1, b"#\r\n#\r\n##"
    yield 1, b""
    rng = random.Random(1)
    body = bytearray(rng.choice(b"ab \r\n#") for _ in range(9000))
    body[4095:4097] = b"\r\n"                                           # a CR LF across 4 KB reads
    body[8191] = 0x0D
    yield 1, bytes(body)
    yield 1, bytes(rng.choice(b"xy\r\n#\x1a\x00") for _ in range(3000))


def synthetic_manner():
    yield b"fuck\r\nBitch\r\n  ass\r\n\t \r\nword \r\n\r\nMATCH\r\n"
    yield b"one\rtwo\r\nthree\nfour"                                     # CR cuts, the last line is dropped
    yield b"\xb0\xa1\r\n\xc7\xd1\xb1\xdb\r\nA\xb0B\r\n"
    yield b"nul\x00inside\r\n\r\n   \r\n"
    yield b""
    rng = random.Random(2)
    yield b"\r\n".join(bytes(rng.choice(b"abAB \t\xb0") for _ in range(rng.randrange(0, 5))) for _ in range(60))


def init():
    emu = Emulator(max_instructions=500_000_000).init_crt()
    emu.serve_data_files({})
    return emu, emu.scratch(size=0x40)


def read_string(emu, p):
    data, length = emu.read_u32(p + 4), emu.read_u32(p + 8)
    return emu.mem_read(data, length) if data and length else b""


def work(state, item):
    emu, obj = state
    mark = emu.heap_mark()
    emu.mem_write(obj, bytes(0x40))
    if item[0] == "tips":
        _, directory, service = item
        emu.api.files_root = directory
        emu.write_u32(SERVICE_TYPE, service)
        emu.call(TIPS_CTOR, this=obj)
        loaded = emu.call(TIPS_LOAD, this=obj).eax & 0xFF
        first, last = emu.read_u32(obj + 4), emu.read_u32(obj + 8)
        tips = range(first, last, 16) if first else range(0)
        lines = [f"ok {len(tips)}" if loaded else "not loaded"]
        for t in tips:
            begin, end = emu.read_u32(t + 4), emu.read_u32(t + 8)
            strings = range(begin, end, 16) if begin else range(0)
            lines.append(f"tip {len(strings)}")
            lines += [f"line {quoted(read_string(emu, s))}" for s in strings]
        emu.heap_release(mark)
        return "\n".join(lines)
    _, path = item
    with open(path, "rb") as f:
        data = f.read()
    emu.serve_data_files({"data\\manner.txt": data})
    emu.call(MANNER_CTOR, this=obj)
    emu.call(MANNER_LOAD, emu.cstr("manner.txt"), this=obj)
    words = []
    last, cur, node = emu.read_u32(obj + 8), emu.read_u32(obj + 0xC), emu.read_u32(obj + 0x10)
    finish = emu.read_u32(obj + 0x1C)
    while cur != finish:
        words.append(read_string(emu, cur))
        cur += 16
        if cur == last:
            node += 4
            cur = emu.read_u32(node)
            last = cur + 0x1000
    lines = [f"words {len(words)}"] + [f"word {quoted(w)}" for w in words]
    for probe in PROBES:
        clean = emu.call(MANNER_IS_CLEAN, emu.cstr(probe), this=obj).eax & 0xFF
        shown = probe.split(b"\x00")[0]
        lines.append(f"probe {quoted(shown)} {'clean' if clean else 'banned'}")
    emu.heap_release(mark)
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--install", default=os.path.join(ROOT, "original", "install"))
    ap.add_argument("--ragres", default=os.path.join(ROOT, "build", "dev", "src", "ragres"))
    ap.add_argument("--jobs", type=int, default=0, help="worker processes (0: one per core but one)")
    args = ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        cases = []  # (name, item, port command)
        probes = os.path.join(tmp, "probes.txt")
        with open(probes, "wb") as f:
            f.write(b"\n".join(p.split(b"\0")[0] for p in PROBES) + b"\n")
        for service, name in TIP_FILES.items():
            path = os.path.join(args.install, name)
            found = [n for n in os.listdir(args.install) if n.lower() == name.lower()]
            if found:
                cases.append((f"install: {found[0]}", ("tips", args.install, service),
                              ["decode-tips", os.path.join(args.install, found[0])]))
        out = os.path.join(tmp, "data.grf")
        subprocess.run([args.ragres, "extract", os.path.join(args.install, "data.grf"), out,
                        "data\\manner.txt"], capture_output=True)
        manner = os.path.join(out, "data", "manner.txt")
        if os.path.exists(manner):
            cases.append(("data.grf: manner.txt", ("manner", manner), ["decode-manner", manner, probes]))
        real = len(cases)
        for k, (service, data) in enumerate(synthetic_tips()):
            directory = os.path.join(tmp, f"tips{k}")
            os.makedirs(directory)
            path = os.path.join(directory, TIP_FILES[service])
            with open(path, "wb") as f:
                f.write(data)
            cases.append((f"synthetic tips {k}", ("tips", directory, service), ["decode-tips", path]))
        for k, data in enumerate(synthetic_manner()):
            path = os.path.join(tmp, f"manner{k}.txt")
            with open(path, "wb") as f:
                f.write(data)
            cases.append((f"synthetic manner {k}", ("manner", path), ["decode-manner", path, probes]))
        originals = oracle_pool.run([item for _, item, _ in cases], init, work, args.jobs or None,
                                    label="tips and manner")
        mismatches = []
        for (name, item, command), want in zip(cases, originals):
            got = subprocess.run([args.ragres, *command], capture_output=True, text=True,
                                 encoding="latin-1").stdout.strip()
            if item[0] == "tips":
                got = got.replace("0 ok", "ok", 1)
            if got != want:
                diff = [f"    port     {a}\n    original {b}" for a, b in zip(got.split("\n"), want.split("\n"))
                        if a != b]
                if got.count("\n") != want.count("\n"):
                    diff.append(f"    {got.count(chr(10)) + 1} lines against {want.count(chr(10)) + 1}")
                mismatches.append(f"{name}:\n" + "\n".join(diff[:6]))
    print(f"{real} real files and {len(cases) - real} synthetic ones compared, {len(mismatches)} differ")
    for line in mismatches[:20]:
        print("MISMATCH", line)
    print("FAILED" if mismatches else "OK")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
