"""The class graph: every class of Ragexe.exe with its bases, size, constructors,
destructors and members, joined from the exports and label files.

Classes come from RTTI (analysis/exports/Ragexe.exe.rtti.tsv) and from the curated
classes without RTTI (analysis/labels/inferred.classes.tsv). For each one:
  bases              direct bases with their offsets (RTTI lists every ancestor; a base
                     that is an ancestor of another listed base is not direct)
  size, size_source  from Ragexe.exe.layouts.tsv: `new` (operator new before a
                     constructor), `curated` or `estimated` (end of the last field used)
  constructors       out-of-line functions named Class::Class
  inlined_in         for a class without one, the functions that store its vtable and are
                     no destructor: the places the compiler expanded its constructor
  destructors        Class::~Class, and the deleting destructors
  functions          code functions in the class's namespace, and how many are reachable
  subsystem          the most common subsystem of its reachable functions
  members            fields typed class:X (an object of X at that offset) and ptr:X
Writes analysis/exports/Ragexe.exe.class_graph.tsv and Ragexe.exe.class_graph.dot
(inheritance as solid edges, contained members as dashed ones) and prints the counts.
"""
import csv
import os
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from binindex import LEDGER, ROOT  # noqa: E402

EXPORTS = os.path.join(ROOT, "analysis", "exports")
LABELS = os.path.join(ROOT, "analysis", "labels")
OUT_TSV = os.path.join(EXPORTS, "Ragexe.exe.class_graph.tsv")
OUT_DOT = os.path.join(EXPORTS, "Ragexe.exe.class_graph.dot")
DELETING = ("`scalar_deleting_destructor'", "`vector_deleting_destructor'")
COLUMNS = ["class", "origin", "bases", "size", "size_source", "vtables", "constructors", "inlined_in",
           "destructors", "deleting_destructors", "functions", "reachable", "subsystem", "members", "pointers"]


def read_tsv(path):
    with open(path, encoding="utf-8") as f:
        return list(csv.DictReader((l for l in f if not l.startswith("#")), delimiter="\t"))


def short(cls):
    return cls.rsplit("::", 1)[-1].split("<")[0]


def main():
    rtti = {r["class"]: r for r in read_tsv(os.path.join(EXPORTS, "Ragexe.exe.rtti.tsv"))}
    curated = {r["class"]: r for r in read_tsv(os.path.join(LABELS, "inferred.classes.tsv"))}
    layouts = {r["class"]: r for r in read_tsv(os.path.join(EXPORTS, "Ragexe.exe.layouts.tsv"))}
    ledger = [r for r in read_tsv(LEDGER) if r["kind"] == "code"]
    by_ns = defaultdict(list)
    for r in ledger:
        by_ns[r["namespace"]].append(r)
    stores = defaultdict(set)
    for r in read_tsv(os.path.join(EXPORTS, "Ragexe.exe.vtable_refs.tsv")):
        stores[r["class"]].add(r["function"])
    members, pointers = defaultdict(list), defaultdict(list)
    for r in read_tsv(os.path.join(LABELS, "layouts.fields.tsv")):
        t = r.get("type", "")
        if t.startswith("class:"):
            members[r["class"]].append(f"{t[6:]}@{r['offset']}")
        elif t.startswith("ptr:"):
            pointers[r["class"]].append(f"{t[4:]}@{r['offset']}")

    ancestors = {c: [b.rsplit("@", 1)[0] for b in r["bases"].split("|") if b] for c, r in rtti.items()}
    rows = []
    for cls in list(rtti) + [c for c in curated if c not in rtti]:
        r = rtti.get(cls)
        bases = []
        if r:
            listed = [b for b in r["bases"].split("|") if b]
            names = [b.rsplit("@", 1)[0] for b in listed]
            for b, name in zip(listed, names):
                if not any(name in ancestors.get(other, ()) for other in names if other != name):
                    bases.append(b)
        fns = by_ns.get(cls, [])
        own = short(cls)
        ctors = sorted(f["entry"] for f in fns if f["name"] == own)
        dtors = sorted(f["entry"] for f in fns if f["name"] == "~" + own)
        deleting = sorted(f["entry"] for f in fns if f["name"] in DELETING)
        inlined = []
        if not ctors:
            inlined = sorted(f for f in stores.get(cls, ())
                             if not f.rsplit("::", 1)[-1].startswith(("~", "`")))
        reach = [f for f in fns if not f["reach"]]
        sub = Counter(f["subsystem"] for f in reach if f["subsystem"]).most_common(1)
        lay = layouts.get(cls, {})
        size = lay.get("size", "") or (curated.get(cls, {}).get("size", ""))
        rows.append({
            "class": cls,
            "origin": "rtti" if r else "curated",
            "bases": " ".join(bases),
            "size": size,
            "size_source": lay.get("size_source", "curated" if size else ""),
            "vtables": str(len([v for v in (r["vftables"].split("|") if r else []) if v])),
            "constructors": " ".join(ctors),
            "inlined_in": "; ".join(inlined),
            "destructors": " ".join(dtors),
            "deleting_destructors": " ".join(deleting),
            "functions": str(len(fns)),
            "reachable": str(len(reach)),
            "subsystem": sub[0][0] if sub else "",
            "members": " ".join(members.get(cls, [])),
            "pointers": " ".join(pointers.get(cls, [])),
        })

    with open(OUT_TSV, "w", encoding="utf-8") as f:
        f.write("\t".join(COLUMNS) + "\n")
        for row in rows:
            f.write("\t".join(row[c] for c in COLUMNS) + "\n")
    with open(OUT_DOT, "w", encoding="utf-8") as f:
        f.write("digraph classes {\n  rankdir=BT;\n  node [shape=box, fontsize=10];\n")
        for row in rows:
            style = "" if row["origin"] == "rtti" else ", style=dashed"
            f.write(f'  "{row["class"]}" [label="{row["class"]}\\n{row["size"] or "?"} bytes"{style}];\n')
        for row in rows:
            for b in row["bases"].split():
                f.write(f'  "{row["class"]}" -> "{b.rsplit("@", 1)[0]}";\n')
            for m in row["members"].split():
                f.write(f'  "{row["class"]}" -> "{m.rsplit("@", 1)[0]}" [style=dashed, arrowhead=diamond];\n')
        f.write("}\n")

    game = [r for r in rows if not r["class"].startswith("std::")]
    print(f"{len(rows)} classes ({sum(r['origin'] == 'rtti' for r in rows)} with RTTI, "
          f"{sum(r['origin'] == 'curated' for r in rows)} curated); {len(game)} outside std")
    for label, part in (("all", rows), ("outside std", game)):
        print(f"  {label}: size {sum(bool(r['size']) for r in part)}, "
              f"constructor {sum(bool(r['constructors']) for r in part)} "
              f"(+{sum(bool(r['inlined_in']) for r in part)} only inlined), "
              f"destructor {sum(bool(r['destructors'] or r['deleting_destructors']) for r in part)}")


if __name__ == "__main__":
    main()
