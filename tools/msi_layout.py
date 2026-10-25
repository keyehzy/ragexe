"""Rebuild the installed directory tree from files extracted out of an MSI cabinet.

The cabinet stores each file under its MSI File-table key. This joins the File,
Component and Directory tables (exported as .idt by `msiinfo export`) to recover
each file's long name and directory relative to INSTALLDIR, then hard-links the
extracted file into place and writes a SHA-256 manifest.

usage: msi_layout.py <idt dir> <extracted cab dir> <output dir> <manifest path>
"""
import hashlib
import os
import sys


def read_idt(path):
    with open(path, encoding="cp1252") as f:
        lines = f.read().splitlines()
    header = lines[0].split("\t")
    return [dict(zip(header, line.split("\t"))) for line in lines[3:] if line]


def long_name(field):
    """Pick the long form of an MSI 'short|long' name."""
    return field.split("|")[-1]


def resolve_dirs(rows, root="INSTALLDIR"):
    parent = {r["Directory"]: r["Directory_Parent"] for r in rows}
    default = {r["Directory"]: r["DefaultDir"] for r in rows}
    cache = {root: ""}

    def resolve(d):
        if d in cache:
            return cache[d]
        if not parent.get(d):
            raise KeyError(f"directory {d} is not under {root}")
        # DefaultDir is "target[:source]"; the target part names the installed folder.
        name = long_name(default[d].split(":")[0])
        base = resolve(parent[d])
        path = base if name == "." else os.path.join(base, name)
        cache[d] = path
        return path

    return resolve


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main(idt_dir, cab_dir, out_dir, manifest_path):
    files = read_idt(os.path.join(idt_dir, "File.idt"))
    components = {r["Component"]: r["Directory_"] for r in read_idt(os.path.join(idt_dir, "Component.idt"))}
    resolve = resolve_dirs(read_idt(os.path.join(idt_dir, "Directory.idt")))

    manifest = []
    seen = set()
    for row in sorted(files, key=lambda r: int(r["Sequence"])):
        rel = os.path.join(resolve(components[row["Component_"]]), long_name(row["FileName"]))
        if rel.lower() in seen:
            raise SystemExit(f"duplicate install path {rel}")
        seen.add(rel.lower())
        src = os.path.join(cab_dir, row["File"])
        if os.path.getsize(src) != int(row["FileSize"]):
            raise SystemExit(f"size mismatch for {row['File']}")
        dst = os.path.join(out_dir, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if os.path.exists(dst):
            os.remove(dst)
        os.link(src, dst)
        manifest.append((sha256(dst), int(row["FileSize"]), rel.replace(os.sep, "/")))

    with open(manifest_path, "w") as f:
        for digest, size, rel in sorted(manifest, key=lambda m: m[2].lower()):
            f.write(f"{digest}  {size:>10}  {rel}\n")
    print(f"{len(manifest)} files laid out under {out_dir}")


if __name__ == "__main__":
    main(*sys.argv[1:5])
