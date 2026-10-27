"""Compares the port's emblem loader with the original (docs/formats/ebm.md).

usage: .venv/bin/python -I tools/ebm_oracle.py [--jobs J] [--mutations N]

data.grf holds no .ebm files: the client writes them from what the server sends. So
this runs CEmblemRes::Load (00428500) in the emulation oracle, with the original cps.dll
inflating, on synthetic ones:
  - emblems of the shapes the client meets and the edges of its 8 KB buffers;
  - hand-built deflate streams for what zlib 1.0.4 accepts and current zlib does not;
  - seeded mutations of valid streams, whose Adler-32 is redone where current zlib still
    inflates them, so that a difference in what inflates shows.
It compares the bitmap (size and every pixel) and, separately, the bytes inflated and the
length the client passes on, with `ragres decode-ebms`.
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
from emu import EmulationError, Emulator  # noqa: E402
from emu.emulator import CPS_BASE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EBP, UC_X86_REG_EIP  # noqa: E402

CBITMAPRES_CTOR, CEMBLEMRES_LOAD = 0x4277D0, 0x428500  # analysis/labels
CEMBLEMRES_SIZE, BITMAP = 0x230, 0x110
AFTER_UNCOMPRESS = 0x4285B1  # [ebp-0x10] the length, ebp-0x40a0 the 8 KB buffer
BUFFER = 0x2000
CPS_SIZE = 0x15000
# A load takes at most a quarter of a million instructions; one still in cps.dll after
# this many is a copy that does not end.
BUDGET = 10_000_000


def fnv1a64(data, h=0xCBF29CE484222325):
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def bmp(width, height, bits=24, seed=0, colors_used=0):
    rng = random.Random(seed)
    if bits == 24:
        stride = (width * 3 + 3) & ~3
        palette = b""
    else:
        stride = (width * bits + 31) // 32 * 4
        palette = bytes(rng.randrange(256) for _ in range(4 * (colors_used or 1 << bits)))
    top = colors_used or 256 if bits == 8 else 256
    pixels = bytes(rng.randrange(top) for _ in range(stride * height))
    offset = 54 + len(palette)
    return (b"BM" + struct.pack("<IHHI", offset + len(pixels), 0, 0, offset)
            + struct.pack("<IiiHHIIiiII", 40, width, height, 1, bits, 0, len(pixels), 0, 0, colors_used, 0)
            + palette + pixels)


class Bits:
    """A deflate bit stream: values go in least significant bit first, Huffman codes most."""
    def __init__(self):
        self.value, self.count = 0, 0

    def put(self, value, n):
        self.value |= value << self.count
        self.count += n

    def code(self, code, n):
        self.put(int(f"{code:0{n}b}"[::-1], 2), n)

    def literal(self, byte):  # the fixed code
        if byte < 144:
            self.code(0x30 + byte, 8)
        else:
            self.code(0x190 + byte - 144, 9)

    def bytes(self):
        return self.value.to_bytes((self.count + 7) // 8, "little")


def zlib_stream(body, inflated, header=b"\x78\x9c"):
    return header + body + struct.pack(">I", zlib.adler32(inflated))


def far_reference(image):
    """A fixed-code stream of image whose reserved header bytes (6 to 9, zero) come from a
    copy of 4 at distance 10: before the start of the output."""
    assert image[6:10] == bytes(4)
    bits = Bits()
    bits.put(1, 1)
    bits.put(1, 2)
    for b in image[:6]:
        bits.literal(b)
    bits.code(2, 7)      # length 4
    bits.code(6, 5)      # distances 9 to 12, 2 extra bits
    bits.put(1, 2)       # 10
    for b in image[10:]:
        bits.literal(b)
    bits.code(0, 7)      # end of block
    return zlib_stream(bits.bytes(), image)


def runaway(image):
    """A fixed-code stream of image with a length symbol 286 in the middle, well before the
    end: zlib 1.0.4's fast loop takes it as a copy of length 0."""
    bits = Bits()
    bits.put(1, 1)
    bits.put(1, 2)
    for b in image[:100]:
        bits.literal(b)
    bits.code(0xC6, 8)   # 286
    bits.code(0, 5)
    for b in image[100:]:
        bits.literal(b)
    bits.code(0, 7)
    return zlib_stream(bits.bytes(), image)


def stored(data):
    """data in one stored block."""
    return zlib_stream(b"\x01" + struct.pack("<HH", len(data), len(data) ^ 0xFFFF) + data, data)


def synthetic():
    plain = bmp(24, 24, seed=1)
    yield zlib.compress(plain)
    yield zlib.compress(bmp(24, 24, bits=8, seed=2))
    yield zlib.compress(bmp(24, 24, bits=8, seed=3, colors_used=16))  # indices below 16
    yield zlib.compress(bmp(30, 20, seed=4))             # wider: width clamped, pixels kept
    yield zlib.compress(bmp(20, 40, bits=8, seed=5))     # taller
    yield zlib.compress(bmp(16, 9, seed=6))
    yield zlib.compress(bmp(0, 24, seed=7))
    yield zlib.compress(bmp(24, 24, bits=4, seed=8))     # refused by the bitmap: blank
    yield zlib.compress(b"not a bitmap" * 20)
    yield b""
    yield b"\x78"
    yield b"junk that is not zlib"
    good = zlib.compress(plain)
    yield good[:-1]                                       # no complete Adler-32
    yield good[:-6]                                       # broken off
    yield good[:-4] + struct.pack(">I", zlib.adler32(plain) ^ 1)
    yield good + b"trailing bytes"
    yield b"\x78\x01" + good[2:]                          # another valid check
    yield b"\x88\x1a" + good[2:]                          # a 64 KB window
    yield b"\x77\x22" + good[2:]                          # method 7
    yield b"\x78\xbb" + good[2:]                          # a preset dictionary
    yield b"\x78\x9d" + good[2:]                          # bad check bits
    yield zlib.compress(plain, 0)
    yield zlib.compress(plain, 1)
    fixed = zlib.compressobj(9, zlib.DEFLATED, 15, 9, zlib.Z_FIXED)
    yield fixed.compress(plain) + fixed.flush()
    yield far_reference(plain)
    yield far_reference(bmp(24, 24, bits=8, seed=9))
    yield runaway(plain)
    padded = plain + bytes(random.Random(10).randrange(256) for _ in range(BUFFER - 1 - len(plain)))
    yield zlib.compress(padded)                           # 8191 bytes: decoded
    yield zlib.compress(padded + b"\0")                   # 8192: the buffer's size, blank
    yield zlib.compress(padded + b"\0\0")                 # more than fits: blank
    rest = BUFFER - 11 - len(plain)
    yield stored(plain + bytes(random.Random(11).randrange(256) for _ in range(rest)))  # an 8 KB file


def mutations(count, seed):
    """Valid streams with a few body bytes changed; where current zlib still inflates one
    over a zeroed window, its Adler-32 is redone so that only the inflating can differ."""
    rng = random.Random(seed)
    images = [bmp(24, 24, seed=20), bmp(24, 24, bits=8, seed=21), bmp(13, 7, seed=22)]
    for k in range(count):
        image = rng.choice(images)
        level = rng.choice([1, 6, 9])
        strategy = rng.choice([zlib.Z_DEFAULT_STRATEGY, zlib.Z_FIXED, zlib.Z_HUFFMAN_ONLY])
        c = zlib.compressobj(level, zlib.DEFLATED, -15, 9, strategy)
        body = bytearray(c.compress(image) + c.flush())
        for _ in range(rng.randrange(1, 4)):
            body[rng.randrange(len(body))] ^= 1 << rng.randrange(8)
        d = zlib.decompressobj(-15, zdict=bytes(0x8000))
        try:
            inflated = d.decompress(bytes(body), BUFFER)
        except zlib.error:
            inflated = b""
        yield zlib_stream(bytes(body), inflated if d.eof else image)


def reads_unset(data):
    """Whether LoadBmp (00427b90) would build its image from bytes nobody set: past what
    was inflated, or palette entries past the count (docs/formats/bitmap.md). The port
    gives the blank emblem or zero entries there, so only what was inflated is compared.
    Sizes the port refuses count too."""
    if len(data) < 2 or data[:2] != b"BM":
        return False
    if len(data) < 54:
        return True
    offset, = struct.unpack_from("<I", data, 10)
    width, height = struct.unpack_from("<ii", data, 18)
    bits, = struct.unpack_from("<H", data, 28)
    used, = struct.unpack_from("<I", data, 46)
    if bits not in (8, 24):
        return False
    if width < 0 or height < 0 or width * height > 1 << 26:
        return True
    if width == 0 or height == 0:
        return False
    if bits == 24:
        return offset + (width * 3 + (4 - width * 3 % 4) % 4) * (height - 1) + width * 3 > len(data)
    count, stride = used or 256, (width + 3) & ~3
    if count > 256 or 54 + 4 * count > len(data) or offset + stride * height > len(data):
        return True
    rows = (data[offset + row * stride:offset + row * stride + width] for row in range(height))
    return any(b >= count for row in rows for b in row)


def init():
    emu = Emulator(max_instructions=BUDGET).init_crt().load_cps()
    obj = emu.scratch(size=CEMBLEMRES_SIZE)
    seen = {}

    def after_uncompress(uc, address, size, _):
        ebp = uc.reg_read(UC_X86_REG_EBP)
        length = emu.read_u32(ebp - 0x10)
        seen["inflated"] = length, emu.mem_read(ebp - 0x40A0, min(length, BUFFER))

    emu.hook_code(AFTER_UNCOMPRESS, AFTER_UNCOMPRESS, after_uncompress)
    state = emu, obj, seen
    # The first inflate may set up tables that later ones reuse; keep them out of the
    # heap that each run gives back.
    fixed = zlib.compressobj(9, zlib.DEFLATED, 15, 9, zlib.Z_FIXED)
    emu.serve_data_files({"data\\t.ebm": fixed.compress(bmp(4, 4)) + fixed.flush()})
    emu.call(CBITMAPRES_CTOR, this=obj + BITMAP)
    emu.call(CEMBLEMRES_LOAD, emu.cstr("t.ebm"), this=obj)
    return state


def work(state, path):
    emu, obj, seen = state
    with open(path, "rb") as f:
        data = f.read()
    mark = emu.heap_mark()
    emu.serve_data_files({"data\\t.ebm": data})
    emu.mem_write(obj, bytes(CEMBLEMRES_SIZE))
    emu.call(CBITMAPRES_CTOR, this=obj + BITMAP)
    seen.clear()
    try:
        ok = emu.call(CEMBLEMRES_LOAD, emu.cstr("t.ebm"), this=obj).eax & 0xFF
    except EmulationError:
        if "inflated" not in seen and CPS_BASE <= emu.uc.reg_read(UC_X86_REG_EIP) < CPS_BASE + CPS_SIZE:
            emu.heap_release(mark)
            return "faults"
        ok = None
    length, inflated = seen.get("inflated", (BUFFER, b""))
    passed = "I -" if length == BUFFER else f"I {length}:{fnv1a64(inflated):016x}"
    if length != BUFFER and reads_unset(inflated):
        emu.heap_release(mark)
        return "unset\n" + passed
    if ok is None:
        raise EmulationError(f"{path}: the original faulted on an image it should decode")
    if not ok:
        emu.heap_release(mark)
        return "failed"
    width, height, pixels = struct.unpack("<iiI", emu.mem_read(obj + BITMAP + 0x114, 12))
    size = emu.blocks.get(pixels, 0) if pixels else 0
    image = emu.mem_read(pixels, size) if size else b""
    emu.heap_release(mark)
    return f"ok {width} {height} {size // 4}:{fnv1a64(image):016x}\n{passed}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ragres", default=os.path.join(ROOT, "build", "dev", "src", "ragres"))
    ap.add_argument("--jobs", type=int, default=0, help="worker processes (0: one per core but one)")
    ap.add_argument("--mutations", type=int, default=400, help="mutated streams")
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        cases = list(synthetic())
        made = len(cases)
        cases += list(mutations(args.mutations, args.seed))
        files = []
        for k, data in enumerate(cases):
            path = os.path.join(tmp, f"{'synthetic' if k < made else 'mutation'}{k}.ebm")
            with open(path, "wb") as f:
                f.write(data)
            files.append(path)
        too_large = BUFFER + 1
        out = subprocess.run([args.ragres, "decode-ebms", *files], capture_output=True, text=True).stdout
        ours, current = {}, None
        for line in out.splitlines():
            if line.startswith("I "):
                ours[current].append(line)
                continue
            n, rest = line.split(" ", 1)
            current = int(n)
            ours[current] = ["failed"] if rest.startswith("failed") else [rest]
        ours = {n: "\n".join(lines) for n, lines in ours.items()}
        originals = oracle_pool.run(files, init, work, args.jobs or None, label="ebm")
        mismatches, unset, faults_failed, faults_blank = [], 0, 0, 0
        for n, (p, want) in enumerate(zip(files, originals)):
            got = ours.get(n)
            if want == "faults":
                # The port must not decode an image where zlib 1.0.4 faults or runs away.
                if got == "failed":
                    faults_failed += 1
                    continue
                if (got or "").endswith("\nI -"):
                    faults_blank += 1
                    continue
            if want.startswith("unset\n"):
                unset += 1
                got, want = (got or "").split("\n")[-1], want.split("\n")[-1]
            if got != want:
                diff = [f"    port     {a}\n    original {b}" for a, b in
                        zip((got or "").split("\n"), want.split("\n")) if a != b]
                mismatches.append(f"{os.path.basename(p)}:\n" + "\n".join(diff[:4]))
        decoded = sum(1 for n in range(len(files)) if (ours.get(n) or "").split("\n")[-1].startswith("I ")
                      and not ours[n].endswith("I -"))
        # Larger files overrun the original's stack buffer; the port refuses them.
        big = os.path.join(tmp, "big.ebm")
        with open(big, "wb") as f:
            f.write(zlib.compress(bmp(24, 24)) + bytes(too_large))
        refused = subprocess.run([args.ragres, "decode-ebms", big], capture_output=True, text=True).stdout
        if "failed" not in refused:
            mismatches.append(f"a file over 8 KB was not refused: {refused.strip()}")
    print(f"{made} synthetic emblems and {len(files) - made} mutated streams compared "
          f"({decoded} inflate, {unset} only as far as that), {len(mismatches)} differ")
    print(f"zlib 1.0.4 faults or runs away on {faults_failed + faults_blank}: the port fails "
          f"{faults_failed} and gives the blank emblem for {faults_blank}")
    for line in mismatches[:20]:
        print("MISMATCH", line)
    print("FAILED" if mismatches else "OK")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
