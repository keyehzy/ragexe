"""The class graph export (tools/class_graph.py) against M1's requirement: constructors,
destructors and sizes for the RTTI classes, with the exceptions docs/PLAN.md names.

Run from the repository root: .venv/bin/python -I -m unittest discover -s tests -t .
"""
import csv
import os
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GRAPH = os.path.join(ROOT, "analysis", "exports", "Ragexe.exe.class_graph.tsv")

# type_info objects are static data the compiler emits; the five std::*_base classes are
# empty tags with no functions; std::ios_base is a virtual base its streams construct.
NO_CONSTRUCTOR = {"type_info", "std::ios_base", "std::ctype_base", "std::codecvt_base",
                  "std::time_base", "std::money_base", "std::messages_base"}
TAG_BASES = {"std::ctype_base", "std::codecvt_base", "std::time_base", "std::money_base",
             "std::messages_base"}


class ClassGraph(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(GRAPH, encoding="utf-8") as f:
            cls.rows = {r["class"]: r for r in csv.DictReader(f, delimiter="\t")}
        cls.rtti = {c: r for c, r in cls.rows.items() if r["origin"] == "rtti"}

    def test_rtti_classes_have_constructors(self):
        missing = {c for c, r in self.rtti.items() if not (r["constructors"] or r["inlined_in"])}
        self.assertEqual(missing, NO_CONSTRUCTOR)

    def test_rtti_classes_have_destructors_and_sizes(self):
        no_dtor = {c for c, r in self.rtti.items() if not (r["destructors"] or r["deleting_destructors"])}
        no_size = {c for c, r in self.rtti.items() if not r["size"]}
        self.assertEqual(no_dtor, TAG_BASES)
        self.assertEqual(no_size, TAG_BASES)

    def test_bases_are_classes(self):
        for c, r in self.rows.items():
            for b in r["bases"].split():
                self.assertIn(b.rsplit("@", 1)[0], self.rows, c)

    def test_known_hierarchy(self):
        self.assertEqual(self.rows["CRagConnection"]["bases"], "CConnection@0")
        self.assertEqual(self.rows["UIColorButton"]["bases"], "UIButton@0")
        self.assertIn("ViewFrustum@0xf4", self.rows["CView"]["members"])


if __name__ == "__main__":
    unittest.main()
