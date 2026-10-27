"""Compares the port's sprite loader with the original (docs/formats/sprite.md).

usage: .venv/bin/python -I tools/sprite_oracle.py [--sample N] [--scales 00,11,10,01]

Runs CSprRes::Load (004341d0) in the emulation oracle on every .spr of data.grf, behind
a stand-in archive and with CTexture replaced to capture the 32-bit images, at each
detail level given (halve x, halve y), and compares with `ragres decode-sprites`: the
palette, every 8-bit image's record and pixels (decoded by the original DecodeRle) and
every 32-bit image's record and pixels.
"""
import argparse
import os
import random
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from emu import Emulator  # noqa: E402

# analysis/labels/inferred.functions.tsv and objects.functions.tsv
CSPRRES_CTOR, CSPRRES_LOAD, CSPRRES_RESET, DECODE_RLE = 0x4340A0, 0x4341D0, 0x434110, 0x434E50
CTEXTURE_CTOR, CTEXTURE_UPDATE, CTEXTURE_DELETE, CTEXTURE_VFTABLE = 0x4165E0, 0x416990, 0x416680, 0x5E270C
HALVE_X, HALVE_Y = 0x688A7C, 0x688A80


def fnv1a64(data, h=0xCBF29CE484222325):
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def port_results(ragres, files, scale):
    """{n: [lines]} from ragres decode-sprites, in batches."""
    flags = (["--halve-x"] if scale[0] else []) + (["--halve-y"] if scale[1] else [])
    results = {}
    for start in range(0, len(files), 300):
        out = subprocess.run([ragres, "decode-sprites", *flags, *files[start:start + 300]],
                             capture_output=True, text=True).stdout
        current = None
        for line in out.splitlines():
            if line.startswith("F "):
                current = start + int(line.split()[1])
                results[current] = [line.split(" ", 2)[2]]
            else:
                results[current].append(line)
    return results


class Original:
    def __init__(self):
        # EncodeRle inserts into a vector a byte at a time, so a large sprite at a low detail
        # level takes far more than the default limit of instructions.
        self.emu = Emulator(max_instructions=2_000_000_000).init_crt()
        self.textures = []
        emu = self.emu

        def ctor(this, w, h, flag):
            emu.write_u32(this, CTEXTURE_VFTABLE)
            return this

        def update(this, x, y, w, h, pixels, a, b):
            self.textures.append((w, h, emu.mem_read(pixels, w * h * 4)))
            return 1

        emu.replace_thiscall(CTEXTURE_CTOR, 3, ctor)
        emu.replace_thiscall(CTEXTURE_UPDATE, 7, update)
        emu.replace_thiscall(CTEXTURE_DELETE, 1, lambda this, flags: this)
        self.obj = emu.scratch(size=0x534)

    def load(self, data, scale):
        emu = self.emu
        mark = emu.heap_mark()
        emu.write_u32(HALVE_X, scale[0])
        emu.write_u32(HALVE_Y, scale[1])
        emu.serve_data_files({"data\\sprite\\t.spr": data})
        self.textures = []
        emu.mem_write(self.obj, bytes(0x534))
        emu.call(CSPRRES_CTOR, this=self.obj)
        ok = emu.call(CSPRRES_LOAD, emu.cstr("sprite\\t.spr"), this=self.obj).eax & 0xFF
        lines = []
        if ok:
            version = struct.unpack_from("<H", data, 2)[0]
            palette = emu.mem_read(self.obj + 0x110, 1024) if version > 0x100 else bytes(1024)
            images = self.records(self.obj + 0x510)
            images32 = self.records(self.obj + 0x520)
            lines.append(f"ok {version:x} {len(images)} {len(images32)} {fnv1a64(palette):016x}")
            for w, h, hx, hy, rle in images:
                decoded = b""
                if w > 0 and h > 0:
                    out = emu.call(DECODE_RLE, rle, w, h, this=self.obj).eax
                    decoded = emu.mem_read(out, w * h)
                lines.append(f"I {w} {h} {hx} {hy} {fnv1a64(decoded):016x}")
            for (w, h, hx, hy, _), (pw, ph, pixels) in zip(images32, self.textures):
                lines.append(f"T {w} {h} {hx} {hy} {pw} {ph} {fnv1a64(pixels):016x}")
            emu.call(CSPRRES_RESET, this=self.obj)
        else:
            lines.append("failed")
        emu.heap_release(mark)
        return lines

    def records(self, vector):
        begin, end = self.emu.read_u32(vector + 4), self.emu.read_u32(vector + 8)
        out = []
        for slot in range(begin, end, 4):
            r = self.emu.read_u32(slot)
            w, h, hx, hy = struct.unpack("<hhHH", self.emu.mem_read(r, 8))
            out.append((w, h, hx, hy, self.emu.read_u32(r + 12)))
        return out


def comparable(port_lines):
    """The port's lines without what the original does not record (the RLE size)."""
    head = port_lines[0].split()
    first = "failed" if head[0] == "failed" else " ".join(head)
    rest = [" ".join(l.split()[:6]) if l.startswith("I ") else l for l in port_lines[1:]]
    return [first] + rest if head[0] != "failed" else ["failed"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--archive", default=os.path.join(ROOT, "original", "install", "data.grf"))
    ap.add_argument("--ragres", default=os.path.join(ROOT, "build", "dev", "src", "ragres"))
    ap.add_argument("--sample", type=int, default=0, help="check N sprites (0: all)")
    ap.add_argument("--scales", default="00,11", help="detail levels: halve x and halve y digits")
    args = ap.parse_args()
    scales = [(int(s[0]), int(s[1])) for s in args.scales.split(",")]

    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([args.ragres, "extract", args.archive, tmp, "*.spr"], check=True,
                       capture_output=True)
        files = sorted(os.path.join(d, n) for d, _, ns in os.walk(tmp) for n in ns)
        if args.sample:
            files = sorted(random.Random(1).sample(files, min(args.sample, len(files))))
        original = Original()
        mismatches = []
        counts = [0, 0, 0]
        for scale in scales:
            ours = port_results(args.ragres, files, scale)
            for n, path in enumerate(files):
                with open(path, "rb") as f:
                    data = f.read()
                want = original.load(data, scale)
                got = comparable(ours.get(n, ["failed"]))
                if got != want:
                    first = next((k for k, (a, b) in enumerate(zip(got, want)) if a != b), min(len(got), len(want)))
                    mismatches.append(f"{os.path.relpath(path, tmp)} at {scale}: line {first}: "
                                      f"port {got[first:first + 1]}, original {want[first:first + 1]}")
                counts[0] += 1
                counts[1] += sum(1 for l in want if l.startswith("I "))
                counts[2] += sum(1 for l in want if l.startswith("T "))
    print(f"{len(files)} sprites at {len(scales)} detail levels: {counts[0]} loads, "
          f"{counts[1]} 8-bit and {counts[2]} 32-bit images compared, {len(mismatches)} differ")
    for line in mismatches[:20]:
        print("MISMATCH", line)
    print("FAILED" if mismatches else "OK")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
