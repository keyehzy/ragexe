"""Function ledger: one row per function in Ragexe.exe, tracking porting progress.

The ledger (analysis/ledger.tsv) is the source of truth for status. Ghidra is the
source of truth for names, sizes and namespaces, so `sync` refreshes those from
the ExportInventory output while keeping everything set by hand.

usage:
  ledger.py sync   [--export TSV] [--ledger TSV]
  ledger.py report [--ledger TSV]
  ledger.py set    ADDR|LO-HI ... [--status S] [--subsystem S|-] [--library 0|1] [--note TEXT]
  ledger.py classify [--ledger TSV]
      re-applies the library rules: code at or above LIBRARY_START is the static C
      runtime (crt); code in a std namespace anywhere is the standard library (stl).
"""
import argparse
import os
import sys
from collections import Counter, defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_EXPORT = os.path.join(ROOT, "analysis", "exports", "Ragexe.exe.functions.tsv")
DEFAULT_LEDGER = os.path.join(ROOT, "analysis", "ledger.tsv")

COLUMNS = ["entry", "size", "kind", "library", "subsystem", "status", "namespace", "name", "notes"]
STATUSES = ["raw", "named", "typed", "specced", "ported", "verified"]
SUBSYSTEMS = ["core", "res", "render", "world", "net", "ui", "audio", "game", "crt", "stl"]
# The static CRT and Dinkumware STL follow the game's own code and the import thunks
# (docs/PLAN.md, M1 progress).
LIBRARY_START = 0x5B325C

# Statuses that sync may assign or revoke on its own; anything later was set by hand.
AUTO_STATUSES = {"raw", "named", "-"}

# Kinds that are not ported as functions in their own right: exception-handling
# funclets belong to their parent function and thunks are import stubs.
KIND_CODE, KIND_FUNCLET, KIND_THUNK = "code", "funclet", "thunk"


def read_tsv(path):
    """Read a header-first TSV whose fields never contain tabs or newlines."""
    with open(path, encoding="utf-8") as f:
        lines = f.read().splitlines()
    header = lines[0].split("\t")
    rows = []
    for n, line in enumerate(lines[1:], start=2):
        fields = line.split("\t")
        if len(fields) != len(header):
            raise SystemExit(f"{path}:{n}: expected {len(header)} fields, got {len(fields)}")
        rows.append(dict(zip(header, fields)))
    return rows


def write_ledger(path, rows):
    rows = sorted(rows, key=lambda r: int(r["entry"], 16))
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write("\t".join(COLUMNS) + "\n")
        for r in rows:
            f.write("\t".join(r[c] for c in COLUMNS) + "\n")
    os.replace(tmp, path)


def classify(fn):
    if fn["thunk"] == "1":
        return KIND_THUNK
    if fn["name"].startswith(("Unwind@", "Catch@")):
        return KIND_FUNCLET
    return KIND_CODE


def auto_status(kind, source):
    if kind != KIND_CODE:
        return "-"
    return "raw" if source == "DEFAULT" else "named"


def cmd_sync(args):
    exported = {fn["entry"]: fn for fn in read_tsv(args.export)}
    ledger = {r["entry"]: r for r in read_tsv(args.ledger)} if os.path.exists(args.ledger) else {}

    added = updated = dropped = 0
    stale = []
    for entry, fn in exported.items():
        kind = classify(fn)
        row = ledger.get(entry)
        if row is None:
            ledger[entry] = {
                "entry": entry, "size": fn["size"], "kind": kind, "library": fn["fid"],
                "subsystem": "", "status": auto_status(kind, fn["source"]),
                "namespace": fn["namespace"], "name": fn["name"], "notes": "",
            }
            added += 1
            continue
        before = dict(row)
        row.update(size=fn["size"], kind=kind, namespace=fn["namespace"], name=fn["name"])
        if fn["fid"] == "1":
            row["library"] = "1"
        if row["status"] in AUTO_STATUSES:
            row["status"] = auto_status(kind, fn["source"])
        updated += row != before

    for entry in [e for e in ledger if e not in exported]:
        r = ledger[entry]
        if r["status"] in AUTO_STATUSES and not r["notes"] and not r["subsystem"]:
            del ledger[entry]
            dropped += 1
        else:
            stale.append(entry)

    write_ledger(args.ledger, ledger.values())
    print(f"ledger {args.ledger}: {len(ledger)} rows, {added} added, {updated} updated, {dropped} dropped")
    for entry in stale:
        r = ledger[entry]
        print(f"  warning: {entry} {r['name']} ({r['status']}) is no longer a function in Ghidra", file=sys.stderr)


def pct(part, whole):
    return 100.0 * part / whole if whole else 0.0


def cmd_report(args):
    rows = read_tsv(args.ledger)
    kinds = defaultdict(lambda: [0, 0])
    lib = [0, 0]
    by_status = defaultdict(lambda: [0, 0])
    by_subsystem = defaultdict(lambda: defaultdict(int))
    for r in rows:
        size = int(r["size"])
        kinds[r["kind"]][0] += 1
        kinds[r["kind"]][1] += size
        if r["kind"] != KIND_CODE:
            continue
        if r["library"] == "1":
            lib[0] += 1
            lib[1] += size
            continue
        by_status[r["status"]][0] += 1
        by_status[r["status"]][1] += size
        by_subsystem[r["subsystem"] or "(unassigned)"][r["status"]] += size

    print(f"{len(rows)} ledger rows")
    for kind in (KIND_CODE, KIND_FUNCLET, KIND_THUNK):
        n, b = kinds[kind]
        print(f"  {kind:8} {n:6} functions {b:9} bytes")
    total_n = sum(v[0] for v in by_status.values())
    total_b = sum(v[1] for v in by_status.values())
    print(f"\nlibrary code (excluded from progress): {lib[0]} functions, {lib[1]} bytes")
    print(f"code to port: {total_n} functions, {total_b} bytes\n")
    print(f"  {'status':9} {'functions':>9} {'bytes':>9} {'% bytes':>8}")
    for s in STATUSES:
        n, b = by_status.get(s, (0, 0))
        print(f"  {s:9} {n:9} {b:9} {pct(b, total_b):7.1f}%")
    print(f"\nprogress: {pct(by_status.get('verified', (0, 0))[1], total_b):.1f}% of code bytes verified")
    if len(by_subsystem) > 1 or "(unassigned)" not in by_subsystem:
        print(f"\n  {'subsystem':14} {'bytes':>9} {'% verified':>10}")
        for sub, counts in sorted(by_subsystem.items()):
            b = sum(counts.values())
            print(f"  {sub:14} {b:9} {pct(counts.get('verified', 0), b):9.1f}%")


def cmd_classify(args):
    rows = read_tsv(args.ledger)
    changed = Counter()
    for r in rows:
        if r["kind"] != KIND_CODE:
            continue
        ns = r["namespace"]
        if ns == "std" or ns.startswith("std::"):
            want = ("1", "stl")
        elif int(r["entry"], 16) >= LIBRARY_START:
            want = ("1", "crt")
        else:
            continue
        if (r["library"], r["subsystem"]) != want:
            r["library"], r["subsystem"] = want
            changed[want[1]] += 1
    write_ledger(args.ledger, rows)
    print(f"classified: {dict(changed) or 'no changes'}")


def parse_targets(specs, rows):
    entries = {int(r["entry"], 16): r for r in rows}
    hits = []
    for spec in specs:
        if "-" in spec:
            lo, hi = (int(x, 16) for x in spec.split("-", 1))
            matched = [r for a, r in entries.items() if lo <= a < hi]
            if not matched:
                raise SystemExit(f"no functions in range {spec}")
            hits.extend(matched)
        else:
            addr = int(spec, 16)
            if addr not in entries:
                raise SystemExit(f"no function starts at {spec}")
            hits.append(entries[addr])
    return hits


def cmd_set(args):
    if args.status and args.status not in STATUSES:
        raise SystemExit(f"status must be one of {', '.join(STATUSES)}")
    if args.subsystem and args.subsystem != "-" and args.subsystem not in SUBSYSTEMS:
        raise SystemExit(f"subsystem must be one of {', '.join(SUBSYSTEMS)}, or - to clear it")
    rows = read_tsv(args.ledger)
    targets = parse_targets(args.targets, rows)
    for r in targets:
        if args.status:
            if r["kind"] != KIND_CODE:
                raise SystemExit(f"{r['entry']} is a {r['kind']}, which has no status of its own")
            r["status"] = args.status
        if args.subsystem:
            r["subsystem"] = "" if args.subsystem == "-" else args.subsystem
        if args.library is not None:
            r["library"] = args.library
        if args.note is not None:
            r["notes"] = args.note.replace("\t", " ")
    write_ledger(args.ledger, rows)
    print(f"updated {len(targets)} rows")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("sync")
    s.add_argument("--export", default=DEFAULT_EXPORT)
    s.add_argument("--ledger", default=DEFAULT_LEDGER)
    s.set_defaults(func=cmd_sync)
    r = sub.add_parser("report")
    r.add_argument("--ledger", default=DEFAULT_LEDGER)
    r.set_defaults(func=cmd_report)
    c = sub.add_parser("classify")
    c.add_argument("--ledger", default=DEFAULT_LEDGER)
    c.set_defaults(func=cmd_classify)
    t = sub.add_parser("set")
    t.add_argument("targets", nargs="+", help="hex entry address, or LO-HI half-open range")
    t.add_argument("--status")
    t.add_argument("--subsystem")
    t.add_argument("--library", choices=["0", "1"])
    t.add_argument("--note")
    t.add_argument("--ledger", default=DEFAULT_LEDGER)
    t.set_defaults(func=cmd_set)
    args = p.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
