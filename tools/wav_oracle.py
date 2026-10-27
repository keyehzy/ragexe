"""Compares the port's wave loader with the original (docs/formats/wav.md).

usage: .venv/bin/python -I tools/wav_oracle.py [--jobs J]

Runs CWave::Load (004354a0) in the emulation oracle, with the original Mss32.dll mapped
in, on every .wav of data.grf and on synthetic files, and compares with
`ragres decode-wavs`:
  - Miles' file type, read from AIL_file_type's answer (004356a8);
  - the sample the client keeps at +0x110: the file, or the PCM wave the IMA ADPCM
    decoder makes, byte for byte.
For MPEG Layer III only the type is compared: the client decodes it through Mp3dec.asi,
which the emulator does not load, and the port leaves to the sound layer.

Some synthetic files make Mss32.dll read or write past its buffers, or walk its chunks
forever; there the port must fail, and the original's result is only counted.
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
import oracle_pool  # noqa: E402
from emu import EmulationError, Emulator  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX  # noqa: E402

CWAVE_CTOR, CWAVE_LOAD = 0x435440, 0x4354A0  # analysis/labels
CWAVE_SIZE = 0x118
AFTER_FILE_TYPE = 0x4356A8
TYPES = {1: "pcm", 2: "ima-adpcm", 13: "mpeg-layer3"}
STEPS = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60,
         66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371,
         408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878,
         2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845,
         8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086,
         29794, 32767]
EXPECT_FAIL = "port fails: "
# The largest load, a 367 KB decoded wave, takes a few million instructions.
BUDGET = 50_000_000


def fnv1a64(data, h=0xCBF29CE484222325):
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def chunk(tag, body):
    return tag + struct.pack("<I", len(body)) + body + (b"\0" if len(body) & 1 else b"")


def riff(chunks, form=b"WAVE", head=b"RIFF", size=None):
    body = form + b"".join(chunks)
    return head + struct.pack("<I", len(body) if size is None else size) + body


def fmt(tag, channels, rate, bits, align, extra=b""):
    return chunk(b"fmt ", struct.pack("<HHIIHH", tag, channels, rate, rate * align, align, bits) + extra)


def ima_data(blocks, align, channels, seed, index=None):
    """blocks blocks of align bytes: per channel a header (a sample, an index), then nibbles."""
    rng = random.Random(seed)
    out = b""
    for _ in range(blocks):
        for _ in range(channels):
            out += struct.pack("<hBB", rng.randrange(-32768, 32768),
                               rng.randrange(89) if index is None else index, 0)
        out += bytes(rng.randrange(256) for _ in range(align - 4 * channels))
    return out


def ima(channels=1, align=256, blocks=4, seed=1, fact=None, data=None, bits=4, index=None):
    body = ima_data(blocks, align, channels, seed, index) if data is None else data
    per_block = (align - 4 * channels) * 8 // (4 * channels) + 1
    chunks = [fmt(0x11, channels, 22050, bits, align, struct.pack("<HH", 2, per_block))]
    if fact is not False:
        chunks.append(chunk(b"fact", struct.pack("<I", per_block * blocks if fact is None else fact)))
    chunks.append(chunk(b"data", body))
    return riff(chunks)


def pcm(samples=b"\x01\x02" * 50, tag=1):
    return riff([fmt(tag, 1, 22050, 16, 2), chunk(b"data", samples)])


def synthetic():
    """(name, data, expectation): expectation is "" to compare, or why the port fails."""
    yield "ima mono", ima(), ""
    yield "ima stereo", ima(channels=2, align=512, seed=2), ""
    yield "ima mono, no fact", ima(fact=False, seed=3), ""
    yield "ima stereo, no fact", ima(channels=2, align=512, fact=False, seed=4), ""
    yield "ima, 3 channels through the mono decoder", ima(channels=3, align=256, seed=5, fact=300), ""
    yield "ima, a block not a multiple of 4", ima(align=258, blocks=3, seed=6), ""
    yield "ima, fact past the data: silence after", ima(fact=5000, seed=7), ""
    yield "ima, fact inside a block", ima(fact=700, seed=8), ""
    yield "ima, fact 0: no sample", ima(fact=0, seed=9), ""
    yield "ima, data ends inside a block", ima(data=ima_data(2, 256, 1, 10)[:300]), ""
    yield "ima, 3 bits", ima(bits=3, seed=11), ""
    yield "ima, index 88", ima(index=88, seed=12), ""
    yield "ima, fact ends at a block with more data", ima(fact=505, seed=13), ""
    yield "ima, no channels", riff([fmt(0x11, 0, 22050, 4, 256, struct.pack("<HH", 2, 505)),
                                    chunk(b"fact", struct.pack("<I", 505)),
                                    chunk(b"data", ima_data(1, 256, 1, 15))]), \
        EXPECT_FAIL + "the first sample is written past an empty output"
    yield "ima, index 89", ima(index=89, seed=14), EXPECT_FAIL + "the step tables are read past"
    yield "pcm", pcm(), ""
    yield "pcm, lower case tags", pcm().replace(b"WAVEfmt ", b"waveFMT ").replace(b"data", b"DaTa"), ""
    yield "pcm without RIFF", b"XXXX" + pcm()[4:], ""
    yield "ms adpcm", pcm(tag=2), ""
    mp3 = b"\xff\xfb\x90\x00" + bytes(413)
    yield "mpeg layer 3", b"ID3" + bytes(7) + mp3 * 4, ""
    past = EXPECT_FAIL + "the scan skips 3 bytes after each sync and reads past the file"
    yield "mpeg layer 2", (b"\xff\xfd\x90\x00" + bytes(100)) * 4, past
    yield "mpeg layer 2, past 4 KB", (b"\xff\xfd\x90\x00" + bytes(100)) * 50, ""
    yield "mpeg layer 3 after layer 2", (b"\xff\xfd\x90\x00" + bytes(100)) * 2 + mp3, ""
    yield "mpeg sync with a third byte 0xfc", (b"\xff\xfb\xfc\x00" + bytes(100)) * 4, past
    yield "mpeg sync with a third byte 0xfc, past 4 KB", (b"\xff\xfb\xfc\x00" + bytes(100)) * 50, ""
    yield "four 0xff, past 4 KB", (b"\xff" * 4 + bytes(100)) * 50, ""
    yield "mpeg sync past 4 KB", bytes(0x1100) + mp3, ""
    yield "mpeg in a wave, tag 0x55", riff([fmt(0x55, 1, 22050, 0, 1), chunk(b"data", mp3 * 2)]), ""
    yield "tag 0x55 without a sync", riff([fmt(0x55, 1, 22050, 0, 1), chunk(b"data", bytes(400))]), ""
    yield "pcm cut off by its RIFF size, sync in the data", \
        riff([fmt(1, 1, 22050, 16, 2), chunk(b"data", bytes(8) + mp3)], size=20), ""
    yield "voc", b"Creative Voice File\x1a" + mp3, ""
    yield "xmidi", b"FORM" + bytes(4) + b"XDIR" + bytes(20), ""
    yield "dls", b"RIFF" + bytes(4) + b"DLS " + mp3, ""
    yield "midi", bytes(10) + b"MThd" + bytes(20), ""
    yield "empty", b"", ""
    yield "7 bytes", b"RIFFabc", ""
    yield "8 bytes", b"RIFF\x10\0\0\0", EXPECT_FAIL + "AIL_WAV_info reads past it"
    yield "no WAVE", b"RIFF\x10\0\0\0AVI " + bytes(20), ""
    yield "a chunk size that keeps the walk in place", \
        b"RIFF" + struct.pack("<I", 100) + b"WAVELIST" + struct.pack("<I", 0xFFFFFFF8) + bytes(16), \
        EXPECT_FAIL + "the chunk walk never ends"


def init():
    emu = Emulator(max_instructions=BUDGET).init_crt().load_mss()
    seen = {}

    def after_file_type(uc, address, size, _):
        seen["type"] = uc.reg_read(UC_X86_REG_EAX)

    emu.hook_code(AFTER_FILE_TYPE, AFTER_FILE_TYPE, after_file_type)
    return emu, emu.scratch(size=CWAVE_SIZE), seen


def work(state, path):
    emu, obj, seen = state
    with open(path, "rb") as f:
        data = f.read()
    mark = emu.heap_mark()
    emu.serve_data_files({"data\\wav\\t.wav": data})
    emu.mem_write(obj, bytes(CWAVE_SIZE))
    emu.call(CWAVE_CTOR, this=obj)
    seen.clear()
    try:
        ok = emu.call(CWAVE_LOAD, emu.cstr("wav\\t.wav"), this=obj).eax & 0xFF
    except EmulationError as e:
        emu.heap_release(mark)
        return "does not return" if "did not return" in str(e) else "faults"
    if not ok:
        emu.heap_release(mark)
        return "failed"
    kind = TYPES.get(seen["type"], "other")
    sample, _ = struct.unpack("<II", emu.mem_read(obj + 0x110, 8))
    size = emu.blocks.get(sample, 0) if sample else 0
    body = emu.mem_read(sample, size) if size else b""
    emu.heap_release(mark)
    if kind == "mpeg-layer3":
        return kind
    return f"ok {kind} {size}:{fnv1a64(body):016x}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--archive", default=os.path.join(ROOT, "original", "install", "data.grf"))
    ap.add_argument("--ragres", default=os.path.join(ROOT, "build", "dev", "src", "ragres"))
    ap.add_argument("--jobs", type=int, default=0, help="worker processes (0: one per core but one)")
    args = ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        files_dir = os.path.join(tmp, "files")
        subprocess.run([args.ragres, "extract", args.archive, files_dir, "*.wav"], check=True,
                       capture_output=True)
        files = sorted(os.path.join(d, n) for d, _, ns in os.walk(files_dir) for n in ns)
        real = len(files)
        expect = [""] * real
        names = [os.path.relpath(p, files_dir) for p in files]
        for k, (name, data, expectation) in enumerate(synthetic()):
            path = os.path.join(tmp, f"synthetic{k}.wav")
            with open(path, "wb") as f:
                f.write(data)
            files.append(path)
            expect.append(expectation)
            names.append(name)
        out = subprocess.run([args.ragres, "decode-wavs", *files], capture_output=True, text=True).stdout
        ours = {}
        for line in out.splitlines():
            n, rest = line.split(" ", 1)
            ours[int(n)] = "failed" if rest.startswith("failed") else rest
        originals = oracle_pool.run(files, init, work, args.jobs or None, label="wav")
        mismatches, kinds, fails, outcomes = [], {}, 0, {}
        for n, want in enumerate(originals):
            got = ours.get(n, "")
            if expect[n]:
                fails += 1
                outcome = "returns" if want.startswith("ok ") else want
                outcomes[outcome] = outcomes.get(outcome, 0) + 1
                if got != "failed":
                    mismatches.append(f"{names[n]}: expected the port to fail ({expect[n][len(EXPECT_FAIL):]}), "
                                      f"got {got}")
                continue
            if want == "mpeg-layer3":
                got = got.split(" ")[1] if got.startswith("ok ") else got
            if got != want:
                mismatches.append(f"{names[n]}:\n    port     {got}\n    original {want}")
            if n < real:
                kind = want.split(" ")[1] if want.startswith("ok ") else want
                kinds[kind] = kinds.get(kind, 0) + 1
    print(f"{real} waves ({', '.join(f'{v} {k}' for k, v in sorted(kinds.items()))}) and "
          f"{len(files) - real} synthetic ones compared ({fails} that the port must refuse), "
          f"{len(mismatches)} differ")
    print("the original on those:", ", ".join(f"{v} {k}" for k, v in sorted(outcomes.items())))
    for line in mismatches[:20]:
        print("MISMATCH", line)
    print("FAILED" if mismatches else "OK")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
