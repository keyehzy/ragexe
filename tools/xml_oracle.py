"""Compares the port's XML parser with the original (docs/formats/xml.md).

usage: .venv/bin/python -I tools/xml_oracle.py [--jobs J]

Runs XmlDocument::Parse (004e6590) in the emulation oracle on PetTalkTable.xml,
clientinfo.xml and monsterskillinfo.xml from data.grf and the .gpf patches, and on
synthetic documents; and
ClientState's PetTalkTable loader (005a8aa0) on data.grf's. Each document is placed so
that it ends at the end of mapped memory: a read past it faults. Every element is
compared, in document order (depth, name, text, number of children), with
`ragres decode-xml`; where the original faults, the port must fail.
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
from emu import EmulationError, Emulator  # noqa: E402

XML_DOCUMENT_CTOR, XML_DOCUMENT_PARSE = 0x4C95B0, 0x4E6590
LOAD_PET_TALK_TABLE, PET_TALK_DOCUMENT = 0x5A8AA0, 0xC60  # the document is in ClientState
CLIENT_STATE_SIZE = 0xD00
ELEMENT_SIZE = 0x28
DATA_REGION = 0x70000000


def fnv1a64(data, h=0xCBF29CE484222325):
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def synthetic():
    """(name, bytes, whether the original reads past them)."""
    yield "nested, with text and tabs", b"<a>\r\n\t<b>x</b>\r\n\t<c>y\r\nz</c>tail</a>", False
    yield "self-closing, attributes, declarations", \
        b'<?xml version="1.0"?><!DOCTYPE x><r k="v"><e/><f k=1 /><g\t/></r>', False
    yield "entities", b"<t>&lt;&gt;&amp;&quot;&am;&ampx;&;</t>", False
    yield "a closing tag of another element is skipped", b"<a><b>1</c>2</b>3</a>4", False
    yield "an unclosed element ends with the data", b"<a><b>text", False
    yield "the document's own closing tag", b"<a/></XMLDocument><b/>", False
    yield "text outside any element", b"loose\r\ntext<a/>more", False
    yield "names: empty, NUL, high bytes", b"< >\xb0\x00</ ><\x00/><\xc7\xd1>v</\xc7\xd1>", False
    yield "a comment with '>' inside", b"<!-- a > b -->c<x/>", False
    yield "nothing", b"", False
    yield "a tag left open", b"<a", True
    yield "a '<' at the end", b"<a>x<", True
    yield "an entity left open", b"<a>&lt", True
    yield "an '&' at the end", b"<a>&", True
    yield "'&a' at the end", b"<a>&a", True
    yield "'&am' at the end", b"<a>&am", True
    yield "a closing tag left open", b"<a></a", True
    yield "a comment left open", b"<!-- x", True
    rng = random.Random(1)
    names = [b"a", b"b", b"cc", b"d e"]
    parts = []
    for _ in range(400):
        r = rng.random()
        n = rng.choice(names)
        parts.append(b"<" + n + b">" if r < 0.3 else b"</" + n + b">" if r < 0.55 else
                     b"<" + n + b"/>" if r < 0.6 else b"&amp;" if r < 0.65 else
                     bytes(rng.choice(b"xy \t\r\n") for _ in range(rng.randrange(1, 5))))
    yield "random well-ended tags", b"".join(parts) + b"<z/>", False
    yield "random bytes", bytes(rng.choice(b"<>/&;ab \r\n!?") for _ in range(3000)) + b">;", None


def init():
    emu = Emulator(max_instructions=200_000_000).init_crt()
    emu.serve_data_files({})
    return emu, emu.scratch(size=ELEMENT_SIZE), emu.scratch(size=CLIENT_STATE_SIZE)


def read_string(emu, p):
    data, length = emu.read_u32(p + 4), emu.read_u32(p + 8)
    return emu.mem_read(data, length) if data and length else b""


def tree(emu, root):
    lines, stack = [], [(root, 0)]
    while stack:
        element, depth = stack.pop()
        children = []
        child = emu.read_u32(element + 0x24)
        while child:
            children.append(child)
            child = emu.read_u32(child + 0x20)
        lines.append(f"E {depth} {fnv1a64(read_string(emu, element)):016x} "
                     f"{fnv1a64(read_string(emu, element + 0x10)):016x} {len(children)}")
        stack.extend((c, depth + 1) for c in reversed(children))
    return lines


def work(state, item):
    emu, document, client_state = state
    how, path = item
    with open(path, "rb") as f:
        data = f.read()
    mark = emu.heap_mark()
    if how == "loader":
        emu.mem_write(client_state, bytes(CLIENT_STATE_SIZE))
        document = client_state + PET_TALK_DOCUMENT
    emu.mem_write(document, bytes(ELEMENT_SIZE))
    emu.call(XML_DOCUMENT_CTOR, this=document)
    try:
        if how == "loader":
            emu.serve_data_files({"data\\pettalktable.xml": data})
            emu.call(LOAD_PET_TALK_TABLE, this=client_state)
        else:
            size = (len(data) + 0xFFF) & ~0xFFF or 0x1000
            emu.uc.mem_map(DATA_REGION, size)
            end = DATA_REGION + size
            emu.mem_write(end - len(data), data)
            try:
                emu.call(XML_DOCUMENT_PARSE, end - len(data), end, this=document)
            finally:
                emu.uc.mem_unmap(DATA_REGION, size)
    except EmulationError as e:
        emu.heap_release(mark)
        return "does not return" if "did not return" in str(e) else "faults"
    lines = ["ok"] + tree(emu, document)
    emu.heap_release(mark)
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--install", default=os.path.join(ROOT, "original", "install"))
    ap.add_argument("--ragres", default=os.path.join(ROOT, "build", "dev", "src", "ragres"))
    ap.add_argument("--jobs", type=int, default=0, help="worker processes (0: one per core but one)")
    args = ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        items, names, reads_past = [], [], []
        archives = ["data.grf"] + sorted(n for n in os.listdir(args.install) if n.endswith(".gpf"))
        for archive in archives:
            # monsterskillinfo.xml is in the patches but nothing loads it: more XML to parse.
            for table in ("PetTalkTable.xml", "clientinfo.xml", "monsterskillinfo.xml"):
                out = os.path.join(tmp, archive)
                subprocess.run([args.ragres, "extract", os.path.join(args.install, archive), out,
                                f"data\\{table}"], capture_output=True)
                path = os.path.join(out, "data", table.lower())
                if os.path.exists(path):
                    items.append(("parse", path))
                    names.append(f"{archive}: {table}")
                    reads_past.append(False)
                    if archive == "data.grf" and table == "PetTalkTable.xml":
                        items.append(("loader", path))
                        names.append(f"{archive}: {table} through ClientState's loader")
                        reads_past.append(False)
        real = len(items)
        for k, (name, data, past) in enumerate(synthetic()):
            path = os.path.join(tmp, f"synthetic{k}.xml")
            with open(path, "wb") as f:
                f.write(data)
            items.append(("parse", path))
            names.append(name)
            reads_past.append(past)
        out = subprocess.run([args.ragres, "decode-xml", *[p for _, p in items]],
                             capture_output=True, text=True).stdout
        ours, current = {}, None
        for line in out.splitlines():
            if line.startswith("E "):
                ours[current].append(line)
            else:
                n, rest = line.split(" ", 1)
                current = int(n)
                ours[current] = ["failed" if rest.startswith("failed") else rest]
        originals = oracle_pool.run(items, init, work, args.jobs or None, label="xml")
        mismatches = []
        for n, want in enumerate(originals):
            got = "\n".join(ours.get(n, []))
            if want == "faults":
                if reads_past[n] is False:
                    mismatches.append(f"{names[n]}: the original faults where it should not")
                elif got != "failed":
                    mismatches.append(f"{names[n]}: the original reads past the data; the port does not fail")
                continue
            if reads_past[n]:
                mismatches.append(f"{names[n]}: expected the original to read past the data; it {want[:20]}")
                continue
            if got != want:
                diff = [f"    port     {a}\n    original {b}" for a, b in zip(got.split("\n"), want.split("\n"))
                        if a != b]
                mismatches.append(f"{names[n]}:\n" + "\n".join(diff[:4]))
    refused = sum(1 for n, want in enumerate(originals) if want == "faults")
    print(f"{real} real documents and {len(items) - real} synthetic ones compared "
          f"({refused} that the original reads past), {len(mismatches)} differ")
    for line in mismatches[:20]:
        print("MISMATCH", line)
    print("FAILED" if mismatches else "OK")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
