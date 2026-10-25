"""Assign every function of the game's own code to a subsystem (the ledger's subsystem column).

Seeds come from the curated rules in analysis/subsystems.tsv: a function rule, the
function's class (or, failing that, the nearest RTTI base with a rule), its namespace,
or, for functions in the global namespace, an address range or the imports it uses.
A function whose imports point at more than one subsystem gets no import seed.

The rest is propagated over the address order, which follows the order of the
translation units the linker was given, and the reference graph (direct calls, tail
jumps and code addresses taken as immediates). In order of preference, repeated until
no rule assigns anything:
  neighbours  the nearest assigned functions before and after it are in one subsystem
  initializer it is a dynamic initializer (listed in the C++ initializer table): the
              subsystem of the next function that is not one, once that is assigned,
              since MSVC 6 puts a file's initializers ahead of its functions
              (static_init_050 directly precedes C3dModelRes::C3dModelRes,
              static_init_074 UIFrameWnd::UIFrameWnd)
  callers     every assigned function that references it is in one subsystem
  shared      it is referenced from two or more subsystems: core (a shared helper)
  callees     every assigned function it references is in one subsystem, leaving out
              shared helpers (functions referenced from more than one subsystem): every
              initializer registers the same empty destructor with atexit
  previous    nothing references it: the subsystem of the function before it
Import thunks and game-code tail-jump stubs take their target's subsystem; exception
funclets take the subsystem of the function whose EH tables point at them.

The library rows (crt, stl; see ledger.py classify) are left alone. Before assigning,
the neighbours, callers and callees rules are checked against the class-seeded
functions, each predicted from the other seeds alone, and their agreement is reported:
the neighbours rule agrees far more often, because small helpers are called from
whichever subsystem needs them but live in the translation unit of their own.

Writes the subsystem column of analysis/ledger.tsv and
analysis/exports/Ragexe.exe.subsystems.tsv (entry, subsystem, rule, evidence).
"""
import os
import sys
from bisect import bisect_left
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from binindex import BinIndex, LEDGER, ROOT  # noqa: E402
from capstone import x86  # noqa: E402
import ledger  # noqa: E402

RULES = os.path.join(ROOT, "analysis", "subsystems.tsv")
OUT = os.path.join(ROOT, "analysis", "exports", "Ragexe.exe.subsystems.tsv")
CXX_INIT_TABLE = (0x605000, 0x605340)


def read_rules(path):
    with open(path, encoding="utf-8") as f:
        lines = [line for line in f.read().splitlines() if line and not line.startswith("#")]
    rules = defaultdict(dict)
    for n, line in enumerate(lines[1:], start=2):
        kind, key, sub, evidence = line.split("\t")
        if sub not in ledger.SUBSYSTEMS:
            raise SystemExit(f"{path}: {key}: unknown subsystem {sub}")
        if kind == "function":
            key = int(key, 16)
        elif kind == "range":
            key = tuple(int(x, 16) for x in key.split("-"))
        rules[kind][key] = (sub, evidence)
    return rules


class Assigner:
    def __init__(self):
        self.bx = BinIndex()
        self.rules = read_rules(RULES)
        self.rows = ledger.read_tsv(LEDGER)
        self.game = sorted(fn.entry for fn in self.bx.functions.values() if fn.kind == "code" and not fn.library)
        self.bases = self.read_bases()
        self.sub, self.why = {}, {}
        self.refs_in, self.refs_out = defaultdict(set), defaultdict(set)

    def read_bases(self):
        bases = {}
        for r in ledger.read_tsv(os.path.join(ROOT, "analysis", "exports", "Ragexe.exe.rtti.tsv")):
            bases.setdefault(r["class"], [b.split("@")[0] for b in r["bases"].split(",") if b])
        return bases

    # -- seeds ------------------------------------------------------------------------

    def class_rule(self, ns):
        if ns in self.rules["class"]:
            return self.rules["class"][ns], f"class {ns}"
        for prefix, rule in self.rules["namespace"].items():
            if ns == prefix or ns.startswith(prefix + "::"):
                return rule, f"namespace {prefix}"
        todo, seen = list(self.bases.get(ns, [])), set()
        while todo:
            b = todo.pop(0)
            if b in seen:
                continue
            seen.add(b)
            if b in self.rules["class"]:
                return self.rules["class"][b], f"class {b}, base of {ns}"
            todo.extend(self.bases.get(b, []))
        return None, None

    def imports_used(self, fn):
        """Imports read from the IAT or called through an import thunk."""
        out = set()
        for i in self.bx.insns(fn):
            for op in i.operands:
                if op.type == x86.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
                    name = self.bx.iat.get(op.mem.disp & 0xFFFFFFFF)
                    if name:
                        out.add(name)
                elif op.type == x86.X86_OP_IMM and (op.imm & 0xFFFFFFFF) in self.import_thunks:
                    out.add(self.import_thunks[op.imm & 0xFFFFFFFF])
        return out

    def import_rule(self, name):
        return self.rules["import"].get(name) or self.rules["import"].get(name.split("!")[0])

    def seed(self):
        used = set()
        unknown_classes = Counter()
        for e in self.game:
            fn = self.bx.functions[e]
            if e in self.rules["function"]:
                self.set(e, self.rules["function"][e][0], "function", self.rules["function"][e][1])
                continue
            if fn.namespace != "Global":
                (rule, why) = self.class_rule(fn.namespace)
                if rule:
                    self.set(e, rule[0], "class", why)
                else:
                    unknown_classes[fn.namespace] += 1
                continue
            ranged = [(k, r) for k, r in self.rules["range"].items() if k[0] <= e < k[1]]
            if ranged:
                (lo, hi), (sub, _) = ranged[0]
                self.set(e, sub, "range", f"{lo:08x}-{hi:08x}")
                continue
            hits = {}
            for name in self.imports_used(fn):
                rule = self.import_rule(name)
                if rule:
                    hits.setdefault(rule[0], []).append(name)
                    used.add(name if name in self.rules["import"] else name.split("!")[0])
            if len(hits) == 1:
                ((sub, names),) = hits.items()
                self.set(e, sub, "import", ", ".join(sorted(names)))
        for key in self.rules["import"]:
            if key not in used:
                print(f"  note: import rule {key} matched no function in the global namespace")
        for ns, n in unknown_classes.most_common():
            print(f"  warning: no rule reaches class {ns} ({n} functions)", file=sys.stderr)

    def set(self, e, sub, rule, evidence):
        self.sub[e] = sub
        self.why[e] = (rule, evidence)

    # -- graph ------------------------------------------------------------------------

    def build_graph(self):
        game = set(self.game)
        thunks = {fn.entry for fn in self.bx.functions.values() if fn.kind == "thunk"}
        self.stub_target, self.import_thunks = {}, {}
        for t in thunks:
            target = self.bx.resolve_thunk(t)
            if isinstance(target, int) and target in game:
                self.stub_target[t] = target
            elif isinstance(target, str):
                self.import_thunks[t] = target
        lo, hi = CXX_INIT_TABLE
        table = (self.bx.u32(a) for a in range(lo, hi, 4))
        self.initializers = {self.stub_target.get(v, v) for v in table} & game
        for e in self.game:
            fn = self.bx.functions[e]
            for i in self.bx.insns(fn):
                targets = []
                for op in i.operands:
                    if op.type == x86.X86_OP_IMM:
                        targets.append(op.imm & 0xFFFFFFFF)
                for t in targets:
                    t = self.stub_target.get(t, t)
                    if t in game and t != e and not fn.entry <= t < fn.entry + fn.size:
                        self.refs_out[e].add(t)
                        self.refs_in[t].add(e)

    # -- propagation -------------------------------------------------------------------

    def neighbours(self, e, sub):
        k = bisect_left(self.game, e)
        before = next((self.game[j] for j in range(k - 1, -1, -1) if self.game[j] in sub), None)
        after = next((self.game[j] for j in range(k + 1, len(self.game)) if self.game[j] in sub), None)
        return before, after

    def by_callers(self, e, sub):
        votes = Counter(sub[c] for c in self.refs_in[e] if c in sub)
        if len(votes) == 1:
            ((s, n),) = votes.items()
            return s, f"{n} referencing function{'s' if n > 1 else ''}"
        return None

    def by_neighbours(self, e, sub):
        before, after = self.neighbours(e, sub)
        if before is not None and after is not None and sub[before] == sub[after]:
            return sub[before], f"between {before:08x} and {after:08x}"
        return None

    def by_initializer(self, e, sub):
        if e not in self.initializers:
            return None
        after = next((a for a in self.game[bisect_left(self.game, e) + 1:] if a not in self.initializers), None)
        if after in sub:
            return sub[after], f"initializer of the file starting here, before {after:08x}"
        return None

    def by_callees(self, e, sub):
        votes = Counter(sub[c] for c in self.refs_out[e] if c in sub
                        and len({sub[r] for r in self.refs_in[c] if r in sub}) <= 1)
        if len(votes) == 1:
            ((s, n),) = votes.items()
            return s, f"{n} referenced function{'s' if n > 1 else ''}"
        return None

    def shared(self, e, sub):
        votes = Counter(sub[c] for c in self.refs_in[e] if c in sub)
        if len(votes) > 1:
            return "core", ", ".join(f"{s} {n}" for s, n in sorted(votes.items()))
        return None

    def previous(self, e, sub):
        if self.refs_in[e]:
            return None
        before, _ = self.neighbours(e, sub)
        if before is not None:
            return sub[before], f"after {before:08x}, nothing references it"
        return None

    RULE_ORDER = ("neighbours", "initializer", "callers", "shared", "callees", "previous")

    def propagate(self):
        rules = {"neighbours": self.by_neighbours, "initializer": self.by_initializer, "callers": self.by_callers,
                 "callees": self.by_callees, "shared": self.shared, "previous": self.previous}
        while True:
            for name in self.RULE_ORDER:
                todo = [e for e in self.game if e not in self.sub]
                found = {}
                for e in todo:
                    r = rules[name](e, self.sub)
                    if r:
                        found[e] = r
                for e, (s, why) in found.items():
                    self.set(e, s, name, why)
                if found:
                    break
            else:
                return

    def check_rules(self):
        """Predict each class-seeded function from the other seeds alone."""
        seeded = {e: s for e, s in self.sub.items() if self.why[e][0] == "class"}
        out = []
        for name, rule in (("neighbours", self.by_neighbours), ("callers", self.by_callers),
                           ("callees", self.by_callees)):
            hit = tried = 0
            for e, s in list(seeded.items()):
                del seeded[e]
                r = rule(e, seeded)
                seeded[e] = s
                if r:
                    tried += 1
                    hit += r[0] == s
            out.append((name, tried, hit))
        return out

    def files(self):
        """Runs of functions that start at a dynamic initializer: each is one source file or
        several, so a run that mixes subsystems is worth a look."""
        starts = [k for k, e in enumerate(self.game)
                  if k == 0 or (e in self.initializers and self.game[k - 1] not in self.initializers)]
        out = []
        for n, k in enumerate(starts):
            run = self.game[k:starts[n + 1] if n + 1 < len(starts) else len(self.game)]
            sizes = Counter()
            for e in run:
                sizes[self.sub.get(e, "")] += self.bx.functions[e].size
            out.append((run[0], run[-1], sizes))
        return out

    # -- funclets ---------------------------------------------------------------------

    # -- output -----------------------------------------------------------------------

    def run(self):
        self.build_graph()
        self.seed()
        seeds = Counter(rule for rule, _ in self.why.values())
        for name, tried, hit in self.check_rules():
            print(f"check: the {name} rule predicts {tried} class-seeded functions, "
                  f"{hit} ({100.0 * hit / tried if tried else 0:.1f}%) correctly")
        self.propagate()
        for t, target in self.stub_target.items():
            if target in self.sub:
                self.set(t, self.sub[target], "stub", f"jumps to {target:08x}")
        parents = self.bx.eh_parents()
        for f, p in parents.items():
            if p in self.sub:
                self.set(f, self.sub[p], "funclet", f"EH tables of {p:08x}")
        rows = {int(r["entry"], 16): r for r in self.rows}
        changed = 0
        for e, r in rows.items():
            if r["library"] == "1":
                continue
            want = self.sub.get(e, "")
            if r["subsystem"] != want:
                r["subsystem"] = want
                changed += 1
        ledger.write_ledger(LEDGER, list(rows.values()))
        with open(OUT, "w", encoding="utf-8") as f:
            f.write("entry\tsubsystem\trule\tevidence\n")
            for e in sorted(self.sub):
                f.write(f"{e:08x}\t{self.sub[e]}\t{self.why[e][0]}\t{self.why[e][1]}\n")
        rules = Counter(rule for rule, _ in self.why.values())
        sizes = defaultdict(int)
        for e in self.game:
            sizes[self.sub.get(e, "(none)")] += self.bx.functions[e].size
        print("seeds: " + ", ".join(f"{k} {v}" for k, v in seeds.most_common()))
        print("assigned by rule: " + ", ".join(f"{k} {rules[k]}" for k in
                                                ("function", "class", "namespace", "range", "import") + self.RULE_ORDER
                                                + ("stub", "funclet") if rules[k]))
        print("game code bytes by subsystem: " + ", ".join(f"{k} {v}" for k, v in sorted(sizes.items())))
        left = [e for e in self.game if e not in self.sub]
        print(f"unassigned: {len(left)} functions; funclets without a parent: "
              f"{sum(1 for fn in self.bx.functions.values() if fn.kind == 'funclet' and not fn.library and fn.entry not in parents)}")
        files = self.files()
        mixed = [(lo, hi, sizes) for lo, hi, sizes in files if len(sizes) > 1]
        print(f"files bounded by initializers: {len(files)}, {len(mixed)} with more than one subsystem; "
              f"over 300 bytes outside the largest:")
        for lo, hi, sizes in mixed:
            if sum(sizes.values()) - sizes.most_common(1)[0][1] > 300:
                print(f"  {lo:08x}-{hi:08x} " + ", ".join(f"{k} {v}" for k, v in sizes.most_common()))
        print(f"ledger: {changed} rows changed; wrote {OUT}")


if __name__ == "__main__":
    Assigner().run()
