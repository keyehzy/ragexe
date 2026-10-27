"""Compares the port's palette conversion with the original (docs/formats/palette.md).

usage: .venv/bin/python -I tools/palette_oracle.py

Runs C3dDevice::ConvertColorsToArgb1555 (00405440) in the emulation oracle on every .pal
of data.grf and on synthetic palettes that cover every colour near the magenta key, and
compares the 256 values of each with `ragres decode-palettes`.
"""
import argparse
import itertools
import os
import random
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from emu import Emulator  # noqa: E402

CONVERT_COLORS = 0x405440  # C3dDevice::ConvertColorsToArgb1555
G_C3DDEVICE = 0x6239D0


def synthetic():
    """Palettes covering r, b in 0xf0..0xff and g in 0..0x0f in every combination, then
    random colours."""
    colours = [bytes((r, g, b, 0)) for r, g, b in
               itertools.product(range(0xF0, 0x100), range(0x10), range(0xF0, 0x100))]
    rng = random.Random(7)
    colours += [bytes(rng.randrange(256) for _ in range(4)) for _ in range(256 * 8)]
    return [b"".join(colours[i:i + 256]) for i in range(0, len(colours) - 255, 256)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--archive", default=os.path.join(ROOT, "original", "install", "data.grf"))
    ap.add_argument("--ragres", default=os.path.join(ROOT, "build", "dev", "src", "ragres"))
    args = ap.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        files_dir = os.path.join(tmp, "files")
        subprocess.run([args.ragres, "extract", args.archive, files_dir, "*.pal"], check=True,
                       capture_output=True)
        files = sorted(os.path.join(d, n) for d, _, ns in os.walk(files_dir) for n in ns)
        for k, data in enumerate(synthetic()):
            path = os.path.join(tmp, f"synthetic{k}.pal")
            with open(path, "wb") as f:
                f.write(data)
            files.append(path)
        out = subprocess.run([args.ragres, "decode-palettes", *files], capture_output=True, text=True)
        ours = {}
        for line in out.stdout.splitlines():
            fields = line.split("\t")
            ours[int(fields[0])] = [int(v, 16) for v in fields[2:]] if fields[1] == "ok" else None

        emu = Emulator().init_crt()
        src = emu.scratch(size=1024)
        dst = emu.scratch(size=1024)
        mismatches, values = [], 0
        for n, path in enumerate(files):
            with open(path, "rb") as f:
                data = f.read()[:1024]
            emu.mem_write(src, data)
            emu.call(CONVERT_COLORS, dst, src, 256, this=G_C3DDEVICE)
            want = list(struct.unpack("<256I", emu.mem_read(dst, 1024)))
            if ours.get(n) != want:
                mismatches.append(os.path.basename(path))
            values += 256

    real = sum(1 for p in files if not os.path.basename(p).startswith("synthetic"))
    print(f"{len(files)} palettes ({real} from data.grf), {values} values compared, "
          f"{len(mismatches)} differ")
    for name in mismatches[:20]:
        print("MISMATCH", name)
    print("FAILED" if mismatches else "OK")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
