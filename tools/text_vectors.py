"""Test vectors for the port's text calls (docs/render/text.md), taken from the original in
the emulation oracle.

usage: .venv/bin/python -I tools/text_vectors.py > tests/unit/text_vectors.inc

  - FontEntry::FontEntry (004077d0) for every service language (0067c9d8) 0 to 8 and font
    language 0 to 5, then FontEntry::CreateFont (00407870): CreateFontA's arguments with each
    language's faces installed, only its alternative, or neither (EnumFontFamiliesExA answers
    from that set through MatchFaceNameProc's contract).
  - The text functions on random text, positions, colours and fonts (docs/render/text.md):
    SceneRenderer::DrawText and DrawOutlinedText, UIWindow::DrawText and DrawShadowedText,
    UIFrameWnd::DrawRightAlignedText, UIListBox::DrawColorCodedText (with colour codes),
    DrawVerticalText, the name balloons' DrawOutlinedText, and the measures GetTextWidth,
    GetTextHeight, GetVerticalTextHeight and FitTextWithEllipsis. GetSurfaceDCWithFont is
    stood in for, recording the font asked for; TextOutA and SetTextColor are recorded;
    GetTextExtentPoint32A answers with extent() below, which the unit test shares. The code
    page (0067c998) is 1252, so characters are single bytes.
"""
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EBP, UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_FPCW  # noqa: E402

from emu import Emulator  # noqa: E402
from emu.emulator import TEB  # noqa: E402
from emu.winapi import Stub  # noqa: E402

FONT_ENTRY, CREATE_FONT = 0x4077D0, 0x407870
SERVICE, CODE_PAGE = 0x67C9D8, 0x67C998
GET_DC_WITH_FONT, RELEASE_DC = 0x407680, 0x407770
DC_SURFACE, DC_SURFACE_END, NO_OP = 0x403010, 0x403070, 0x541110
SCENE_TEXT, SCENE_OUTLINED = 0x410DF0, 0x410EB0
WINDOW_TEXT, SHADOWED, RIGHT_ALIGNED, COLOR_CODED = 0x4A5CB0, 0x4A5EA0, 0x4A5D20, 0x4A5DC0
VERTICAL, BALLOON, VERTICAL_BALLOON = 0x4A6010, 0x4A5F30, 0x4A6130
WIDTH, HEIGHT, VERTICAL_HEIGHT, FIT = 0x4A6280, 0x4A6310, 0x4A63A0, 0x4A6460
STAMP, STAMP_END, RENDERER_PTR, FRAME_RATE = 0x4CBA20, 0x4CBB74, 0x625E98, 0x410CF0
FACES = [b"\xb1\xbc\xb8\xb2", b"Arial", bytes.fromhex("826c827220826f835383568362834e"),
         b"\xcb\xce\xcc\xe5", b"\xb2\xd3\xa9\xfa\xc5\xe9", b"Tahoma"]
ALTERNATIVES = [b"Gulim", b"Arial", b"MS Gothic", b"SimSun", b"MingLiU", b"Tahoma"]


def extent(font, text):
    """GetTextExtentPoint32A's stand-in: a width from each byte and the font, a height from
    the font."""
    language, height, weight, vertical = font
    cx = sum(b % 7 + 4 for b in text) * height // 12 + (len(text) if weight > 400 else 0)
    cy = height + 2 + (3 if vertical else 0) + language
    return cx, cy


def c_string(data):
    return "".join(f"\\{c:03o}" if c >= 0x80 or c in (0x22, 0x5C) or c < 0x20 else chr(c) for c in data)


class Oracle:
    def __init__(self):
        emu = self.emu = Emulator().init_crt()
        emu.uc.reg_write(UC_X86_REG_FPCW, 0x007F)
        emu.write_u32(CODE_PAGE, 1252)
        self.fonts = {}
        self.calls = []
        self.color = 0
        self.installed = set()

        def dc_with_font(this):
            esp = emu.uc.reg_read(UC_X86_REG_ESP)
            font = tuple(emu.read_u32(esp + 8 + 4 * k) for k in range(4))
            dc = 0x5000 + len(self.fonts)
            self.fonts[dc] = (font[0], font[1], font[2], font[3] & 0xFF)
            return dc

        emu.replace_thiscall(GET_DC_WITH_FONT, 0, dc_with_font)
        emu.replace_thiscall(RELEASE_DC, 0, lambda this: 0)
        emu.replace_thiscall(DC_SURFACE, 3, lambda this, w, h, s: this)
        emu.replace_thiscall(DC_SURFACE_END, 0, lambda this: 0)

        def text_out(dc, x, y, text, length):
            data = emu.mem_read(text, length) if length > 0 else b""
            x, y = (v - (1 << 32) if v >= 1 << 31 else v for v in (x, y))
            self.calls.append((x, y, bytes(data), self.color, self.fonts[dc]))
            return 1

        def set_color(dc, color):
            previous, self.color = self.color, color
            return previous

        def get_extent(dc, text, length, size):
            data = bytes(emu.mem_read(text, length)) if length > 0 else b""
            emu.mem_write(size, struct.pack("<2i", *extent(self.fonts[dc], data)))
            return 1

        def char_prev(code_page, start, current, flags):
            return current - 1 if current > start else start

        def create_font(*args):
            face = emu.read_cstr(args[13], 64, encoding="latin-1").encode("latin-1") if args[13] else None
            self.created = args[:13] + (face,)
            return 0x7700

        def enum_fonts(dc, logfont, proc, lparam, flags):
            face = emu.read_cstr(lparam, 64, encoding="latin-1").encode("latin-1")
            # MatchFaceNameProc answers 0 on a match, which ends the enumeration with 0.
            return 0 if face in self.installed else 1

        stubs = emu.api.stubs
        stubs["GDI32.DLL!TextOutA"] = Stub(5, text_out)
        stubs["GDI32.DLL!SetTextColor"] = Stub(2, set_color)
        stubs["GDI32.DLL!GetTextExtentPoint32A"] = Stub(4, get_extent)
        stubs["USER32.DLL!CharPrevExA"] = Stub(4, char_prev)
        stubs["GDI32.DLL!CreateFontA"] = Stub(14, create_font)
        stubs["GDI32.DLL!EnumFontFamiliesExA"] = Stub(5, enum_fonts)
        stubs["USER32.DLL!GetDC"] = Stub(1, lambda hwnd: 0x1234)
        # A window: its surface at +0x28; a renderer: its back buffer at +0x7c, with an
        # AddRef-like slot 1 that does nothing but pop its this, as a COM method.
        emu.replace_thiscall(NO_OP, 1, lambda this, com_this: 0)
        self.window = emu.scratch(size=0x100)
        emu.write_u32(self.window + 0x28, 0x8888)
        back = emu.scratch(size=0x10)
        emu.write_u32(back, emu.scratch(struct.pack("<2I", NO_OP, NO_OP)))
        self.renderer = emu.scratch(size=0x300)
        emu.write_u32(self.renderer + 0x7C, back)

    def run(self, address, *args, this):
        self.calls = []
        self.fonts = {}
        self.color = 0
        result = self.emu.call(address, *args, this=this).eax
        return result, self.calls


def random_text(rng, codes=False):
    n = rng.randrange(0, 18)
    out = bytearray()
    while len(out) < n:
        r = rng.random()
        if codes and r < 0.12:
            out += b"^" + bytes(rng.choice(b"0123456789abcdefABCDEFxyz\xa9") for _ in range(6))
        elif r < 0.2:
            out.append(rng.randrange(0x80, 0x100))
        else:
            out.append(rng.randrange(0x20, 0x7F))
    return bytes(out)


def main():
    rng = random.Random(53)
    o = Oracle()
    emu = o.emu
    lines = ["// Generated by tools/text_vectors.py from the original in the emulation oracle."]

    # Fonts: FontEntry's record, then CreateFontA's arguments.
    rows = []
    record = emu.scratch(size=0x14)
    for service in range(9):
        emu.write_u32(SERVICE, service)
        for language in range(6):
            for flag in (0, 1):
                height, weight = rng.choice([10, 12, 14]), rng.choice([400, 700])
                emu.call(FONT_ENTRY, language, height, weight, flag, this=record)
                got = struct.unpack("<3iB", emu.mem_read(record, 13))
                for installed in (0, 1, 2):
                    lang = got[0]
                    o.installed = {FACES[lang], ALTERNATIVES[lang]} if installed == 0 else (
                        {ALTERNATIVES[lang]} if installed == 1 else set())
                    o.created = None
                    emu.call(CREATE_FONT, this=record)
                    a = o.created
                    face = a[13]
                    which = -1 if face is None else (0 if face == FACES[lang] else 1)
                    signed = ", ".join(str(v - (1 << 32) if v >= 1 << 31 else v) for v in a[:13])
                    rows.append(f"    {{{service}, {language}, {height}, {weight}, {flag}, {{{got[0]}, {got[1]}, "
                                f"{got[2]}, {got[3]}}}, {installed}, {{{signed}}}, {which}}},")
    lines.append("// {service, language, height, weight, flag; the record (language, height, weight, flag);")
    lines.append("// installed (0 both faces, 1 the alternative, 2 neither); CreateFontA's first 13")
    lines.append("// arguments; the face: 0 the language's, 1 its alternative, -1 none}.")
    lines.append("static const FontVector kFonts[] = {")
    lines += rows
    lines.append("};")

    emu.write_u32(SERVICE, 0)
    cases = []

    def case(kind, args, result, calls, text, extra=None):
        name = f"kText{len(cases)}"
        lines.append(f"static const RunVector {name}Runs[] = {{"
                     + ", ".join(f'{{{x}, {y}, "{c_string(t)}", {len(t)}, 0x{c:08x}, '
                                 f'{{{f[0]}, {f[1]}, {f[2]}, {f[3]}}}}}' for x, y, t, c, f in calls)
                     + (" {}" if not calls else "") + "};")
        cases.append(f"    {{{kind}, \"{c_string(text)}\", {len(text)}, {{{', '.join(str(a) for a in args)}}}, "
                     f"{result}, {name}Runs, {len(calls)}, {extra if extra is not None else 0}}},")

    for n in range(400):
        kind = n % 13
        text = random_text(rng, codes=kind == 5)
        cstr = emu.scratch(text + b"\0")
        x, y = rng.randrange(-20, 600), rng.randrange(-20, 400)
        length = rng.choice([0, 0, rng.randrange(0, len(text) + 1)])
        lang, height = rng.randrange(6), rng.choice([10, 12, 13, 16])
        color, other = rng.getrandbits(24), rng.getrandbits(24)
        bold = rng.randrange(2)
        weight = rng.choice([400, 700])
        args = [x, y, length, lang, height, color, other, bold, weight]
        extra = None
        if kind == 0:
            result, calls = o.run(SCENE_TEXT, x, y, cstr, color, this=o.renderer)
        elif kind == 1:
            result, calls = o.run(SCENE_OUTLINED, x, y, cstr, color, other, lang, height, weight, this=o.renderer)
        elif kind == 2:
            result, calls = o.run(WINDOW_TEXT, x, y, cstr, length, lang, height, color, this=o.window)
        elif kind == 3:
            result, calls = o.run(SHADOWED, x, y, cstr, length, color, other, lang, height, this=o.window)
        elif kind == 4:
            result, calls = o.run(RIGHT_ALIGNED, x, y, cstr, length, lang, height, color, this=o.window)
        elif kind == 5:
            box = emu.scratch(struct.pack("<I", color))
            result, calls = o.run(COLOR_CODED, x, y, cstr, length, box, lang, height, this=o.window)
            extra = emu.read_u32(box)
        elif kind == 6:
            result, calls = o.run(VERTICAL, x, y, cstr, length, lang, height, color, this=o.window)
        elif kind == 7:
            result, calls = o.run(BALLOON, x, y, cstr, length, color, other, lang, height, bold, this=o.window)
        elif kind == 8:
            result, calls = o.run(VERTICAL_BALLOON, x, y, cstr, length, color, other, lang, height, bold,
                                  this=o.window)
        elif kind == 9:
            result, calls = o.run(WIDTH, cstr, length, lang, height, bold, this=o.window)
        elif kind == 10:
            result, calls = o.run(HEIGHT, cstr, length, lang, height, bold, this=o.window)
        elif kind == 11:
            result, calls = o.run(VERTICAL_HEIGHT, cstr, length, lang, height, bold, this=o.window)
        else:
            dest = emu.scratch(size=len(text) + 8)
            # At, just under and just over the text's own width and its width with "...".
            whole = extent((lang, height, 400, 0), text)[0]
            dots = extent((lang, height, 400, 0), b"...")[0]
            width = rng.choice([rng.randrange(0, 160), whole, whole - 1, whole + 1, whole + dots,
                                whole + dots - 1])
            args[2] = width
            result, calls = o.run(FIT, cstr, dest, width, lang, height, this=o.window)
            fitted = emu.read_cstr(dest, 64, encoding="latin-1").encode("latin-1")
            calls = [(0, 0, fitted, 0, (0, 0, 0, 0))]
        case(kind, args, result & 0xFFFFFFFF, calls, text, extra)
    lines.append("// {kind (the order above), text, its length, {x, y, length (the width for 12), language,")
    lines.append("// height, colour, the other colour, bold, weight}, the result, the runs {x, y, text,")
    lines.append("// length, colour, the font asked for (language, height, weight, vertical)} (for 12 the")
    lines.append("// text fitted), and for 5 the colour after}.")
    lines.append("static const TextVector kTexts[] = {")
    lines += cases
    lines.append("};")
    lines += stamps(o, rng) + frame_rates(o, rng)
    print("\n".join(lines))


def run_vector(calls):
    return "{" + ", ".join(f'{{{x}, {y}, "{c_string(t)}", {len(t)}, 0x{c:08x}, {{{f[0]}, {f[1]}, {f[2]}, {f[3]}}}}}'
                           for x, y, t, c, f in calls) + "}"


def stamps(o, rng):
    """CMode::SaveScreenshot (004cba20) up to its last DrawText: the date GetLocalTime gives,
    drawn five times above the renderer's (00625e98) bottom edge (+0x28)."""
    emu = o.emu
    emu.write_u32(RENDERER_PTR, o.renderer)
    clock = {}

    def local_time(out):
        emu.mem_write(out, struct.pack("<8H", *clock["time"]))
        return 0

    emu.api.stubs["KERNEL32.DLL!GetLocalTime"] = Stub(1, local_time)

    def stop(uc, address, size, user):
        # Out before the folder and the file: the SEH frame unlinked, then to the sentinel.
        ebp = uc.reg_read(UC_X86_REG_EBP)
        emu.write_u32(TEB, emu.read_u32(ebp - 0xC))
        uc.reg_write(UC_X86_REG_EIP, emu.sentinel)

    hook = emu.uc.hook_add(UC_HOOK_CODE, stop, begin=STAMP_END, end=STAMP_END)
    mode = emu.scratch(size=0x40)
    rows = []
    for n in range(120):
        time = [rng.randrange(1980, 2040), rng.randrange(1, 13), rng.randrange(7), rng.randrange(1, 32),
                rng.randrange(24), rng.randrange(60), rng.randrange(60), rng.randrange(1000)]
        if n % 10 == 9:
            time[:7] = [rng.randrange(0x10000), rng.randrange(0x10000), rng.randrange(7)] + [
                rng.randrange(0x10000) for _ in range(4)]
        clock["time"] = time
        height = rng.choice([rng.randrange(0, 1200), 480, 600, 768])
        enabled = 0 if n % 15 == 7 else 1
        emu.write_u32(o.renderer + 0x28, height)
        emu.write_u32(mode + 0x2C, enabled)
        _, calls = o.run(STAMP, this=mode)
        rows.append(f"    {{{{{', '.join(str(v) for v in time[:7])}}}, {height}, {enabled}, "
                    f"{run_vector(calls)}}},")
    emu.uc.hook_del(hook)
    return ["// {GetLocalTime's year, month, day of the week, day, hour, minute and second; the",
            "// renderer's height; CMode+0x2c; the runs, as kTexts'}.",
            "static const StampVector kStamps[] = {"] + rows + ["};"]


def frame_rates(o, rng):
    """SceneRenderer 00410cf0, which nothing calls: the frame count (+0x3c) over the time
    since +0x40 by timeGetTime, and the texture count +0x25c."""
    emu = o.emu
    clock = {}
    emu.api.stubs["WINMM.DLL!timeGetTime"] = Stub(0, lambda: clock["now"])
    # Counts and times whose rate lies within 2**-56 of a midpoint between two floats beside an
    # n.25 or n.75: divided through a double, it would round to the tie and print the next tenth.
    near_ties = [(1113335922, 2062688255), (1310373445, 2059526143), (1504768294, 2056396799),
                 (1603263528, 2054807551), (1256672854, 1516347391), (1337736429, 1514561535),
                 (1909941338, 2049843199), (2007800872, 2048253951)]
    rows = []
    for n in range(300 + len(near_ties)):
        frames = rng.choice([rng.randrange(0, 200), rng.randrange(0, 100000), rng.getrandbits(32)])
        start = rng.getrandbits(32)
        elapsed = rng.choice([rng.randrange(0, 5000), 0, 1000, 1001, 2000, rng.getrandbits(32),
                              (1 << 32) - rng.randrange(1, 100)])
        if n % 25 == 3:
            frames = 0
        if n % 20 == 0:
            # n.25 and n.75 exactly: ties at the tenths, which the CRT rounds up.
            m = rng.randrange(1, 50)
            frames, elapsed = rng.choice([1, 3, 9, 11, 13]) * m, 4000 * m
        if n >= 300:
            frames, elapsed = near_ties[n - 300]
        clock["now"] = (start + elapsed) & 0xFFFFFFFF
        unused = rng.choice([0, rng.randrange(0, 40), rng.getrandbits(32)])
        width, height = rng.choice([(640, 480), (800, 600), (1024, 768), (rng.randrange(0, 2000),
                                                                           rng.randrange(0, 2000))])
        for offset, value in ((0x3C, frames), (0x40, start), (0x25C, unused), (0x24, width), (0x28, height)):
            emu.write_u32(o.renderer + offset, value)
        _, calls = o.run(FRAME_RATE, this=o.renderer)
        after = (emu.read_u32(o.renderer + 0x3C), emu.read_u32(o.renderer + 0x40))
        rows.append(f"    {{{frames}u, {start}u, {clock['now']}u, {unused}u, {width}, {height}, "
                    f"{run_vector(calls)}, {after[0]}u, {after[1]}u}},")
    return ["// {+0x3c, +0x40, timeGetTime, +0x25c, the renderer's width and height; the runs, as",
            "// kTexts'; +0x3c and +0x40 after}.",
            "static const FrameRateVector kFrameRates[] = {"] + rows + ["};"]


if __name__ == "__main__":
    main()
