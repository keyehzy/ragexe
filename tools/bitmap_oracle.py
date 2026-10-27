"""Compares the port's bitmap decoders with the original (docs/formats/bitmap.md).

usage: .venv/bin/python -I tools/bitmap_oracle.py [--sample N] [--tolerance T]

Extracts every .bmp, .tga and .jpg of data.grf with ragres, decodes them with
`ragres decode-images`, and checks them:
  - BMP and TGA against the original LoadBmp (00427b90) and LoadTga (00427e30) run in the
    emulation oracle: same success, size, alpha flag and every pixel;
  - JPEG, whose original decoder is ijl15.dll, against Pillow within T levels per channel,
    with the original's magenta key applied to Pillow's pixels.
Exits non-zero on any difference beyond that.
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

# analysis/labels/inferred.functions.tsv
CBITMAPRES_CTOR, CBITMAPRES_RESET = 0x4277D0, 0x427940
LOAD_BMP, LOAD_TGA = 0x427B90, 0x427E30


def ragres_decode(ragres, files, outdir):
    """{index: (ok, width, height, alpha, pixels bytes)} for the files, in batches."""
    results = {}
    for start in range(0, len(files), 400):
        batch = files[start:start + 400]
        sub = os.path.join(outdir, str(start))
        out = subprocess.run([ragres, "decode-images", sub, *batch], capture_output=True, text=True)
        for line in out.stdout.splitlines():
            n, status, w, h, alpha, error = (line.split("\t") + [""])[:6]
            i = start + int(n)
            pixels = b""
            if status == "ok":
                with open(os.path.join(sub, n + ".argb"), "rb") as f:
                    pixels = f.read()[12:]
            results[i] = (status == "ok", int(w), int(h), int(alpha), pixels, error)
    return results


def original_decode(emu, obj, fn, data):
    """(ok, width, height, alpha, pixels bytes, message box text) from the original."""
    mark = emu.heap_mark()
    shown = len(emu.api.messages)
    buf = emu.scratch(data)
    ok = emu.call(fn, buf, len(data), this=obj).eax & 0xFF
    w, h = emu.read_u32(obj + 0x114), emu.read_u32(obj + 0x118)
    alpha, pix = emu.read_u32(obj + 0x110) & 0xFF, emu.read_u32(obj + 0x11C)
    pixels = emu.mem_read(pix, w * h * 4) if ok and pix else b""
    emu.call(CBITMAPRES_RESET, this=obj)
    emu.write_u32(obj + 0x110, 0)
    emu.heap_release(mark)
    message = emu.api.messages[-1][1] if len(emu.api.messages) > shown else ""
    return bool(ok), w, h, alpha, pixels, message


def jpeg_reference(path):
    from PIL import Image
    with Image.open(path) as im:
        rgb = im.convert("RGB")
        w, h = rgb.size
        raw = rgb.tobytes()
    out = bytearray()
    for i in range(0, len(raw), 3):
        r, g, b = raw[i], raw[i + 1], raw[i + 2]
        v = 0x000A0A0A if (r >= 0xFE and g <= 3 and b >= 0xFE) else (0xFF << 24 | r << 16 | g << 8 | b)
        out += struct.pack("<I", v)
    return w, h, bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--archive", default=os.path.join(ROOT, "original", "install", "data.grf"))
    ap.add_argument("--ragres", default=os.path.join(ROOT, "build", "dev", "src", "ragres"))
    ap.add_argument("--sample", type=int, default=0, help="check N files of each type (0: all)")
    ap.add_argument("--tolerance", type=int, default=4, help="JPEG levels per channel")
    args = ap.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        files_dir = os.path.join(tmp, "files")
        subprocess.run([args.ragres, "extract", args.archive, files_dir, "*.bmp", "*.tga", "*.jpg"],
                       check=True, capture_output=True)
        by_type = {"bmp": [], "tga": [], "jpg": []}
        for d, _, names in os.walk(files_dir):
            for name in names:
                by_type[name.lower().rsplit(".", 1)[-1]].append(os.path.join(d, name))
        rng = random.Random(1)
        files = []
        for ext in ("bmp", "tga", "jpg"):
            group = sorted(by_type[ext])
            if args.sample:
                group = sorted(rng.sample(group, min(args.sample, len(group))))
            files += group
        ours = ragres_decode(args.ragres, files, os.path.join(tmp, "decoded"))

        emu = Emulator().init_crt()
        obj = emu.scratch(size=0x120)
        emu.call(CBITMAPRES_CTOR, this=obj)
        failures = []
        stats = {"bmp": [0, 0, 0], "tga": [0, 0, 0], "jpg": [0, 0, 0]}  # files, both fail, pixels
        worst, key_flips = 0, 0
        for i, path in enumerate(files):
            ext = path.lower().rsplit(".", 1)[-1]
            rel = os.path.relpath(path, files_dir)
            mine = ours.get(i)
            stats[ext][0] += 1
            if ext == "jpg":
                w, h, ref = jpeg_reference(path)
                if not mine or not mine[0] or (mine[1], mine[2], mine[3]) != (w, h, 0):
                    failures.append(f"{rel}: port {mine and mine[:4]}, reference {w}x{h}")
                    continue
                for a, b in zip(struct.iter_unpack("<I", mine[4]), struct.iter_unpack("<I", ref)):
                    if (a[0] == 0x000A0A0A) != (b[0] == 0x000A0A0A):
                        key_flips += 1
                        continue
                    worst = max(worst, *(abs((a[0] >> s & 0xFF) - (b[0] >> s & 0xFF)) for s in (0, 8, 16)))
                stats[ext][2] += w * h
                continue
            with open(path, "rb") as f:
                data = f.read()
            orig = original_decode(emu, obj, LOAD_BMP if ext == "bmp" else LOAD_TGA, data)
            if not orig[0]:
                if mine and not mine[0] and mine[5] == orig[5]:
                    stats[ext][1] += 1
                else:
                    failures.append(f"{rel}: the original fails ({orig[5]!r}), the port gives {mine and (mine[0], mine[5])}")
                continue
            if mine is None or mine[:5] != orig[:5]:
                differing = sum(1 for a, b in zip(struct.iter_unpack("<I", mine[4] if mine else b""),
                                                   struct.iter_unpack("<I", orig[4])) if a != b)
                failures.append(f"{rel}: port {mine and mine[:4]}, original {orig[:4]}, "
                                f"{differing} pixels differ")
                continue
            stats[ext][2] += orig[1] * orig[2]

    for ext, (n, both_fail, pixels) in stats.items():
        print(f"{ext}: {n} files, {pixels} pixels compared, {both_fail} rejected by both with the same message")
    print(f"jpg: largest channel difference {worst} (tolerance {args.tolerance}), "
          f"{key_flips} pixels keyed differently")
    for line in failures[:40]:
        print("MISMATCH", line)
    bad = failures or worst > args.tolerance
    print("FAILED" if bad else "OK", f"({len(failures)} mismatches)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
