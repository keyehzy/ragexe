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
from emu.directx import DDSCAPS_PRIMARYSURFACE, DDSCAPS_ZBUFFER  # noqa: E402
from emu.winapi import WINDOW  # noqa: E402

# analysis/labels/crt.functions.tsv and inferred.functions.tsv
ATOI, ITOA, ATOF, STRLWR, STRICMP, SPRINTF = 0x5C236A, 0x5D25FC, 0x5C28B5, 0x5D3D6A, 0x5D0140, 0x5C1908
FOPEN, FREAD, FCLOSE, TIME = 0x5C2292, 0x5C242F, 0x5C20AB, 0x5C38DF
CGPAK_GET_FILE_NAME = 0x4DEC00
C3DDEVICE_CTOR, C3DDEVICE_INIT, C3DDEVICE_RELEASE, C3DDEVICE_SIZE = 0x404240, 0x404410, 0x4042E0, 0x268
IID_HAL_DEVICE, IID_TNL_HAL_DEVICE = 0x5EC118, 0x5EC0D8          # GUIDs in .rdata
SCENE_FLUSH, SCENE_STAGE_MODE, SCENE_RENDERER_SIZE = 0x410560, 0x4112D0, 0x2A0
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
class DirectXFakes(unittest.TestCase):
    """C3dDevice and SceneRenderer code run against the DirectDraw/Direct3D 7 fakes."""

    def init_device(self, flags, mode=None, device=IID_HAL_DEVICE):
        emu = Emulator()
        dev = emu.scratch(size=C3DDEVICE_SIZE)
        emu.call(C3DDEVICE_CTOR, this=dev)
        r = emu.call(C3DDEVICE_INIT, WINDOW, 0, device, emu.scratch(mode) if mode else 0, flags, this=dev)
        return emu, dev, r.signed

    def test_windowed_init(self):
        emu, _, result = self.init_device(2)
        self.assertEqual(result, 0)
        calls = emu.directx.calls
        self.assertEqual([c.method for c in calls[:4]],
                         ["GetDisplayMode", "SetCooperativeLevel", "CreateSurface", "CreateClipper"])
        primary = calls[2].detail
        self.assertEqual((primary["width"], primary["height"], primary["caps"]), (800, 600, DDSCAPS_PRIMARYSURFACE))
        create = next(c for c in calls if c.method == "CreateDevice")
        self.assertEqual(create.detail, {"device": "IID_IDirect3DHALDevice", "target": "surface#4"})
        zbuffer = next(c for c in calls if c.method == "CreateSurface" and c.detail["caps"] & DDSCAPS_ZBUFFER)
        self.assertEqual(zbuffer.detail["pixel_format"][3], 16, "the z-buffer callback stops at the 16-bit format")
        device = next(o for o in emu.directx.objects.values() if o.interface == "IDirect3DDevice7")
        self.assertEqual(device.viewport, (0, 0, 800, 600, 0.0, 1.0))
        self.assertEqual(device.render_states, {
            "CULLMODE": 1, "ZFUNC": 4, "TEXTUREPERSPECTIVE": 1, "ALPHAFUNC": 7, "ALPHAREF": 0xF0,
            "COLORKEYENABLE": 1, "ZENABLE": 1, "ZWRITEENABLE": 1, "SRCBLEND": 5, "DESTBLEND": 6})
        self.assertEqual(device.stage_states[(1, "BORDERCOLOR")], 0xFF000000)

    def test_fullscreen_init_and_release(self):
        mode = bytearray(0x7C)
        struct.pack_into("<IIII", mode, 0, 0x7C, 0x1006, 768, 1024)
        struct.pack_into("<I", mode, 0x18, 60)                  # refresh rate
        struct.pack_into("<I", mode, 0x54, 16)                  # bits per pixel
        emu, dev, result = self.init_device(3, bytes(mode), IID_TNL_HAL_DEVICE)
        self.assertEqual(result, 0)
        calls = emu.directx.calls
        self.assertEqual(calls[2].method, "SetDisplayMode")
        self.assertEqual(calls[2].args, [1024, 768, 16, 60, 0])
        self.assertEqual(calls[3].detail["backbuffers"], 1)
        self.assertEqual(calls[4].method, "GetAttachedSurface")
        self.assertEqual(next(c for c in calls if c.method == "CreateDevice").detail["target"], "backbuffer#3")
        self.assertEqual(emu.call(C3DDEVICE_RELEASE, this=dev).eax, 0)
        self.assertEqual([c.result for c in emu.directx.calls if c.obj == "device#6" and c.method == "Release"],
                         [0], "no device reference left, so no message box")

    def scene(self, emu, device, prims):
        lst = emu.scratch(b"".join(struct.pack("<I", p) for p in prims))
        sr = emu.scratch(size=SCENE_RENDERER_SIZE)
        emu.write_u32(sr + 0x78, device.address)
        emu.write_u32(sr + 0x188, lst)
        emu.write_u32(sr + 0x18C, lst + 4 * len(prims))
        return sr

    def test_draw_records_vertices_and_state(self):
        emu = Emulator()
        dx = emu.directx
        device = dx.create_device()
        texture = dx.create_texture(64, 64)
        tex = emu.scratch(size=0x20)                            # +0xc the surface, +0x18 the u and v scale
        emu.write_u32(tex + 0xC, texture.address)
        emu.mem_write(tex + 0x18, struct.pack("<2f", 0.5, 0.25))
        corners = [(10, 20, 0, 0), (110, 20, 1, 0), (10, 120, 0, 1), (110, 120, 1, 1)]
        prims = []
        for mode in (0, 3):
            verts = emu.scratch(b"".join(struct.pack("<4fII2f", x, y, 0.5, 1.0, 0xFF808080, 0xFF000000, u, v)
                                         for x, y, u, v in corners))
            p = emu.scratch(size=0x24)
            for off, v in ((0, verts), (8, 4), (0xC, tex), (0x14, 5), (0x18, 2), (0x1C, mode), (0x20, 5)):
                emu.write_u32(p + off, v)
            prims.append(p)
        emu.call(SCENE_FLUSH, this=self.scene(emu, device, prims))
        self.assertEqual([c.method for c in dx.calls].count("SetTexture"), 1, "the second draw keeps the texture")
        self.assertEqual(len(dx.draws), 2)
        first, second = (d.detail for d in dx.draws)
        self.assertEqual((first["primitive"], first["fvf"]), ("TRIANGLESTRIP", 0x1C4))
        self.assertEqual(first["textures"], {0: texture.label})
        self.assertEqual(first["render_states"], {"SRCBLEND": 5, "DESTBLEND": 2})
        self.assertEqual([v["xyz"][:2] for v in first["vertices"]], [(x, y) for x, y, _, _ in corners])
        self.assertEqual([v["tex0"] for v in first["vertices"]], [(u * 0.5, v * 0.25) for _, _, u, v in corners])
        self.assertEqual(first["vertices"][0]["diffuse"], 0xFF808080)
        self.assertEqual((first["stage_states"][(0, "COLOROP")], second["stage_states"][(0, "COLOROP")]), (4, 7))

    def test_stage_modes_set_every_argument_they_use(self):
        for mode in range(16):
            with self.subTest(mode=mode):
                emu = Emulator()
                device = emu.directx.create_device()
                emu.call(SCENE_STAGE_MODE, mode, this=self.scene(emu, device, []))
                st = device.stage_states
                for stage in (0, 1):
                    for kind in ("COLOR", "ALPHA"):
                        op = st.get((stage, kind + "OP"))
                        self.assertIsNotNone(op)
                        used = {1: (), 2: (1,), 3: (2,)}.get(op, (1, 2))
                        for n in used:
                            self.assertIn((stage, f"{kind}ARG{n}"), st)

    def test_unfaked_method_is_reported(self):
        emu = Emulator()
        surface = emu.directx.create_texture(8, 8)
        update_overlay = emu.read_u32(emu.read_u32(surface.address) + 4 * 33)
        with self.assertRaisesRegex(EmulationError, r"unfaked method IDirectDrawSurface7::UpdateOverlay"):
            emu.call(update_overlay, surface.address, 0, 0, 0, 0, 0)


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
