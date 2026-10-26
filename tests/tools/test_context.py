"""Checks of the naming context tools (tools/fn_context.py, binindex, recover_layouts)
against facts of the binary that docs/PLAN.md records.

Run from the repository root: .venv/bin/python -I -m unittest discover -s tests -t .
Skipped when original/client/Ragexe.exe is not present.
"""
import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from binindex import EXE  # noqa: E402

G_INT_MAP_NIL = 0x628B3C           # the nil node CRagConnection and CKeyPad share
FONT_CACHE_TREE_NIL = 0x623C5C     # created by std::Tree::InitHead 0x407f00
STRING_DTOR, STRING_DTOR_COPY = 0x422DC0, 0x4B3FD0
WRAP_TEXT_LINES = 0x4A5340         # __cdecl; starts with the strlen idiom or ecx, -1
SET_RESIZE_EDGE = 0x440250         # UIResizer::SetResizeEdge, mov [ecx + 0x64], eax
VECTOR_00641F98_DESTROY = 0x4A70F0


@unittest.skipUnless(os.path.exists(EXE), "original/client/Ragexe.exe not present")
class Context(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import fn_context
        cls.ctx = fn_context.Context()

    def test_ecx_writes_are_not_reads(self):
        bx = self.ctx.bx
        self.assertFalse(bx.reads_ecx_first(bx.functions[WRAP_TEXT_LINES]))
        self.assertTrue(bx.reads_ecx_first(bx.functions[SET_RESIZE_EDGE]))

    def test_tree_nil_nodes(self):
        nils = self.ctx.tree_nils()
        self.assertEqual(nils[G_INT_MAP_NIL][0], 0x18)
        self.assertEqual(nils[FONT_CACHE_TREE_NIL][0], 0x18)
        self.assertIn(0x407F00, nils[FONT_CACHE_TREE_NIL][1])

    def test_same_code(self):
        self.assertIn(STRING_DTOR, self.ctx.same_bodies()[STRING_DTOR_COPY])

    def test_data_uses(self):
        users = {f for _, f in self.ctx.data_uses()[0x641F9C]}
        self.assertIn(VECTOR_00641F98_DESTROY, users)


class ThisAtCallSite(unittest.TestCase):
    """recover_layouts' callers test: ECX written just before the call and not read."""

    def insns(self, code):
        import capstone
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        md.detail = True
        ins = list(md.disasm(code + b"\xe8\x00\x00\x00\x00", 0x1000))
        return ins, len(ins) - 1

    def test_this(self):
        from recover_layouts import Layouts
        self.assertTrue(Layouts.sets_ecx_before(*self.insns(b"\x8b\xce")))              # mov ecx, esi
        self.assertTrue(Layouts.sets_ecx_before(*self.insns(b"\xb9\x70\x66\x68\x00")))  # mov ecx, 0x686670

    def test_pushed_argument(self):
        from recover_layouts import Layouts
        # mov ecx, [0x688a60]; push ecx
        self.assertFalse(Layouts.sets_ecx_before(*self.insns(b"\x8b\x0d\x60\x8a\x68\x00\x51")))


if __name__ == "__main__":
    unittest.main()
