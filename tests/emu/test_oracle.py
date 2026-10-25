"""Emulation oracle checks: the harness runs functions of the original and gives the
results their C semantics predict, including MSVC 6 behaviour that differs from C99.

Run from the repository root: .venv/bin/python -I -m unittest discover -s tests -t .
Skipped when original/client/Ragexe.exe is not present.
"""
import os
import struct
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from binindex import EXE  # noqa: E402
from emu import EmulationError, Emulator  # noqa: E402

# analysis/labels/crt.functions.tsv and inferred.functions.tsv
ATOI, ITOA, ATOF, STRLWR, STRICMP, SPRINTF = 0x5C236A, 0x5D25FC, 0x5C28B5, 0x5D3D6A, 0x5D0140, 0x5C1908
FOPEN, FREAD, FCLOSE, TIME = 0x5C2292, 0x5C242F, 0x5C20AB, 0x5C38DF
CGPAK_GET_FILE_NAME = 0x4DEC00
INSTALL = os.path.join(ROOT, "original", "install")


def double_args(x):
    return struct.unpack("<II", struct.pack("<d", x))


@unittest.skipUnless(os.path.exists(EXE), "the original executable is not present")
class CrtFunctions(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.emu = Emulator().init_crt()

    def test_atoi(self):
        for text, want in [("42", 42), ("  -1234xyz", -1234), ("+7", 7), ("", 0), ("x1", 0), ("\t\n 99", 99)]:
            with self.subTest(text=text):
                self.assertEqual(self.emu.call(ATOI, self.emu.cstr(text)).signed, want)

    def test_itoa(self):
        for value, radix, want in [(255, 16, "ff"), (-42, 10, "-42"), (-1, 16, "ffffffff"), (5, 2, "101"), (0, 10, "0")]:
            with self.subTest(value=value, radix=radix):
                buf = self.emu.scratch(size=40)
                self.emu.call(ITOA, value, buf, radix)
                self.assertEqual(self.emu.read_cstr(buf), want)

    def test_atof_returns_in_st0(self):
        for text, want in [("3.25", 3.25), (" -1e3", -1000.0), ("0.1", 0.1), ("abc", 0.0)]:
            with self.subTest(text=text):
                self.assertEqual(self.emu.call(ATOF, self.emu.cstr(text)).st0, want)

    def test_strlwr_in_place(self):
        s = self.emu.cstr("Data\\Texture\\Prontera.BMP")
        self.assertEqual(self.emu.call(STRLWR, s).eax, s)
        self.assertEqual(self.emu.read_cstr(s), "data\\texture\\prontera.bmp")

    def test_stricmp(self):
        self.assertEqual(self.emu.call(STRICMP, self.emu.cstr(".BMP"), self.emu.cstr(".bmp")).signed, 0)
        self.assertLess(self.emu.call(STRICMP, self.emu.cstr("abc"), self.emu.cstr("ABD")).signed, 0)

    def test_sprintf_uses_three_exponent_digits(self):
        buf = self.emu.scratch(size=128)
        r = self.emu.call(SPRINTF, buf, self.emu.cstr("%s(%ld): %5.2f|%x|%e"), self.emu.cstr("file.c"), 42,
                          *double_args(3.14159), 0xBEEF, *double_args(3.14159))
        self.assertEqual(self.emu.read_cstr(buf), "file.c(42):  3.14|beef|3.141590e+000")
        self.assertEqual(r.eax, len("file.c(42):  3.14|beef|3.141590e+000"))

    @unittest.skipUnless(os.path.exists(os.path.join(INSTALL, "data.grf")), "the install tree is not present")
    def test_fopen_reads_the_install_tree(self):
        f = self.emu.call(FOPEN, self.emu.cstr("DATA.GRF"), self.emu.cstr("rb")).eax
        self.assertNotEqual(f, 0)
        buf = self.emu.scratch(size=46)
        self.assertEqual(self.emu.call(FREAD, buf, 1, 46, f).eax, 46)
        self.assertEqual(self.emu.call(FCLOSE, f).eax, 0)
        with open(os.path.join(INSTALL, "data.grf"), "rb") as g:
            self.assertEqual(self.emu.mem_read(buf, 46), g.read(46))

    def test_missing_file(self):
        self.assertEqual(self.emu.call(FOPEN, self.emu.cstr("no\\such.file"), self.emu.cstr("rb")).eax, 0)


@unittest.skipUnless(os.path.exists(EXE), "the original executable is not present")
class GameFunctions(unittest.TestCase):
    def test_cgpak_get_file_name(self):
        emu = Emulator()

        def reference(path):
            return path[max(path.rfind("\\"), path.rfind("/")) + 1:]

        for path in ["data\\texture\\abc.bmp", "data/sprite/x.spr", "noslash.txt", "dir\\", "\\abc", "a/b\\c/d.e", ""]:
            with self.subTest(path=path):
                dst = emu.scratch(size=260)
                emu.call(CGPAK_GET_FILE_NAME, dst, emu.cstr(path), this=0)
                self.assertEqual(emu.read_cstr(dst), reference(path))


@unittest.skipUnless(os.path.exists(EXE), "the original executable is not present")
class Harness(unittest.TestCase):
    def test_unstubbed_import_is_reported(self):
        emu = Emulator()
        with self.assertRaisesRegex(EmulationError, r"unstubbed import KERNEL32\.DLL!Get(Local|System)Time"):
            emu.call(TIME, 0)

    def test_runs_are_deterministic(self):
        def run():
            emu = Emulator().init_crt()
            buf = emu.scratch(size=64)
            emu.call(SPRINTF, buf, emu.cstr("%d-%s"), 7, emu.cstr("x"))
            return emu.read_cstr(buf), emu.heap_next, [n for n, _ in emu.calls]
        self.assertEqual(run(), run())

    def test_instruction_limit(self):
        emu = Emulator(max_instructions=1000)
        loop = emu.scratch(b"\xeb\xfe")          # jmp $
        emu.uc.mem_protect(loop & ~0xFFF, 0x1000, 7)
        with self.assertRaisesRegex(EmulationError, "did not return"):
            emu.call(loop)


if __name__ == "__main__":
    unittest.main()
