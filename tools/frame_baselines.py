"""Checks ragview's reference frames against their reviewed baselines (docs/render/gpu.md#baselines).

usage: .venv/bin/python -I tools/frame_baselines.py [--update] [--ragview PATH] [--install DIR]
           [--images DIR] [NAME...]

tests/baselines/frames.tsv names each scene and its ragview arguments, and keeps what its reviewed
frame gave the GPU: the number of draws and the FNV-1a digests `ragview --digest` prints of the
draws (their states and vertices) and of the textures' texels. These do not depend on the GPU, and
must stay equal. It also keeps the SHA-256 of the image as reviewed and the GPU driver it was drawn
on.

The reviewed images themselves show the game's data, so they stay out of the repository, under
--images (original/baselines/frames by default, beside the install). When one is there, each new
image must match it: at most 0.1% of the pixels may differ, by more than 16 in a channel, which
allows another driver's rounding but not a missing or misplaced draw.

--update renders the scenes named (every scene by default), writes their digests and image hashes
to the table and their images under --images. Run it only after looking at the images.

Exit status: 0 when every scene matches, 1 otherwise, 77 when ragview has no GPU device.
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageChops

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TABLE = os.path.join(ROOT, "tests", "baselines", "frames.tsv")
HEADER = """# Reviewed frames of ragview (tools/frame_baselines.py; docs/render/gpu.md#baselines). Each scene:
# its name, its ragview arguments, the draws its frame gave the GPU, the digests of the draws and of
# the textures (ragview --digest), the SHA-256 of the image as reviewed, and the GPU driver.
"""
SKIP = 77
CHANNEL_TOLERANCE = 16
PIXEL_SHARE = 0.001


def read_table():
    rows = []
    with open(TABLE, encoding="utf-8") as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            name, args, draws, draw_digest, texture_digest, sha, gpu = line.rstrip("\n").split("\t")
            rows.append({"name": name, "args": args, "draws": draws, "draw_digest": draw_digest,
                         "texture_digest": texture_digest, "sha": sha, "gpu": gpu})
    return rows


def write_table(rows):
    with open(TABLE, "w", encoding="utf-8") as f:
        f.write(HEADER)
        for r in rows:
            f.write("\t".join(r[k] for k in ("name", "args", "draws", "draw_digest", "texture_digest", "sha",
                                             "gpu")) + "\n")


def render(ragview, install, row, out):
    """The scene's frame into out: (draws, draw digest, texture digest, driver), or None without a
    GPU device."""
    result = subprocess.run([ragview, "--install", install, "--screenshot", out, "--digest", *row["args"].split()],
                            capture_output=True, text=True)
    if result.returncode != 0:
        if "SDL_CreateGPUDevice" in result.stderr:
            return None
        raise RuntimeError(f"{row['name']}: ragview failed: {result.stderr.strip()}")
    _, draws, draw_digest, _, texture_digest = result.stdout.split()
    driver = next((line.rsplit(" on ", 1)[1] for line in result.stderr.splitlines() if " cells, " in line), "?")
    return draws, draw_digest, texture_digest, driver


def sha256(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def compare_images(got, want):
    """The pixels that differ by more than the tolerance in some channel, and the count of all."""
    a, b = Image.open(got).convert("RGB"), Image.open(want).convert("RGB")
    if a.size != b.size:
        return a.size[0] * a.size[1], a.size[0] * a.size[1]
    diff = ImageChops.difference(a, b)
    bands = [band.point(lambda v: 255 if v > CHANNEL_TOLERANCE else 0) for band in diff.split()]
    mask = ImageChops.lighter(ImageChops.lighter(bands[0], bands[1]), bands[2])
    return mask.histogram()[255], a.size[0] * a.size[1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("names", nargs="*")
    ap.add_argument("--ragview", default=os.path.join(ROOT, "build", "dev", "src", "ragview"))
    ap.add_argument("--install", default=os.path.join(ROOT, "original", "install"))
    ap.add_argument("--images", default=os.path.join(ROOT, "original", "baselines", "frames"))
    ap.add_argument("--update", action="store_true", help="take the frames as the new baselines")
    args = ap.parse_args()
    rows = read_table()
    unknown = set(args.names) - {r["name"] for r in rows}
    if unknown:
        sys.exit(f"no such scenes: {', '.join(sorted(unknown))}")
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        for row in rows:
            if args.names and row["name"] not in args.names:
                continue
            out = os.path.join(tmp, row["name"] + ".png")
            got = render(args.ragview, args.install, row, out)
            if got is None:
                print("ragview has no GPU device: skipped")
                sys.exit(SKIP)
            draws, draw_digest, texture_digest, driver = got
            reviewed = os.path.join(args.images, row["name"] + ".png")
            if args.update:
                row.update(draws=draws, draw_digest=draw_digest, texture_digest=texture_digest, sha=sha256(out),
                           gpu=driver)
                os.makedirs(args.images, exist_ok=True)
                shutil.copyfile(out, reviewed)
                print(f"{row['name']}: updated ({draws} draws)")
                continue
            problems = []
            if (draws, draw_digest, texture_digest) != (row["draws"], row["draw_digest"], row["texture_digest"]):
                problems.append(f"the GPU was given other draws or textures: {draws} draws {draw_digest} "
                                f"textures {texture_digest}, baseline {row['draws']} draws {row['draw_digest']} "
                                f"textures {row['texture_digest']}")
            if sha256(out) == row["sha"]:
                image = "image identical"
            elif os.path.exists(reviewed):
                differing, total = compare_images(out, reviewed)
                image = f"{differing} of {total} pixels differ"
                if differing > PIXEL_SHARE * total:
                    problems.append(f"the image differs from the reviewed one in {differing} of {total} pixels")
            else:
                image = f"image not compared (drawn on {driver}, reviewed on {row['gpu']}; no reviewed copy)"
            if problems:
                failures += 1
                print(f"{row['name']}: FAILED")
                for p in problems:
                    print(f"    {p}")
            else:
                print(f"{row['name']}: equal ({draws} draws, {image})")
    if args.update:
        write_table(rows)
        return
    print("FAILED" if failures else "OK")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
