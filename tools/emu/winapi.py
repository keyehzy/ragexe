"""Stubs for the imports a single emulated function may reach.

Each stub is (argument count, function, stdcall). Win32 imports are __stdcall, so the
stub pops its arguments. A stub returns EAX, or (EAX, EDX).

The stubs are deterministic: time starts at a fixed value and advances by a fixed step
per query, and process and thread IDs are constants. File access is read-only and
resolves names case-insensitively under original/install, the original install tree.
Anything the oracle has not needed yet is left out on purpose; the emulator stops with
the import's name when one is called.

There is one window, WINDOW, whose client area is the profile's display size at
WINDOW_ORIGIN on the screen. DirectDrawCreateEx hands out the DirectDraw 7 fake of
directx.py.
"""
import os
import struct
import unicodedata

from .directx import IID_IDirectDraw7

PROCESS_ID = 0x00000F00
THREAD_ID = 0x00000F04
TICK_START, TICK_STEP = 1_000_000, 16           # milliseconds
PROCESS_HEAP = 0x00DE0000
MODULE_HANDLES = 0x70000000
STD_HANDLES = {0xFFFFFFF6: 0x10, 0xFFFFFFF5: 0x14, 0xFFFFFFF4: 0x18}   # STD_INPUT/OUTPUT/ERROR_HANDLE
FILE_TYPE_DISK, FILE_TYPE_CHAR = 1, 2
STARTUPINFO_SIZE = 68
ERROR_CALL_NOT_IMPLEMENTED = 120
CT_CTYPE1 = 1
C1_UPPER, C1_LOWER, C1_DIGIT, C1_SPACE, C1_PUNCT, C1_CNTRL, C1_BLANK, C1_XDIGIT, C1_ALPHA = \
    0x1, 0x2, 0x4, 0x8, 0x10, 0x20, 0x40, 0x80, 0x100
LCMAP_LOWERCASE, LCMAP_UPPERCASE = 0x100, 0x200
INVALID_HANDLE_VALUE = 0xFFFFFFFF
GENERIC_WRITE = 0x40000000
ERROR_FILE_NOT_FOUND, ERROR_ACCESS_DENIED = 2, 5
FILE_ATTRIBUTE_DIRECTORY, FILE_ATTRIBUTE_ARCHIVE = 0x10, 0x20
ANSI_CODE_PAGE, OEM_CODE_PAGE = 1252, 437      # a Western-European Windows, as for iRO
WINDOW = 0x00010010
WINDOW_ORIGIN = (64, 32)
DDERR_INVALIDPARAMS = 0x80070057


class Stub:
    __slots__ = ("nargs", "fn", "stdcall")

    def __init__(self, nargs, fn, stdcall=True):
        self.nargs, self.fn, self.stdcall = nargs, fn, stdcall


class WinApi:
    def __init__(self, emu, files_root):
        self.emu = emu
        self.files_root = files_root
        self.last_error = 0
        self.ticks = TICK_START
        self.tls = {}
        self.next_tls = 1
        self.handles = {}                          # handle -> [data, position]
        self.modules = {}
        self.next_handle = 0x100
        e = emu
        k32 = {
            "GetLastError": Stub(0, lambda: self.last_error),
            "SetLastError": Stub(1, self.set_last_error),
            "GetCurrentProcessId": Stub(0, lambda: PROCESS_ID),
            "GetCurrentThreadId": Stub(0, lambda: THREAD_ID),
            "GetCurrentProcess": Stub(0, lambda: 0xFFFFFFFF),
            "GetCurrentThread": Stub(0, lambda: 0xFFFFFFFE),
            "GetTickCount": Stub(0, self.tick),
            "Sleep": Stub(1, lambda ms: 0),
            "TlsAlloc": Stub(0, self.tls_alloc),
            "TlsFree": Stub(1, lambda i: self.tls.pop(i, None) is not None or 1),
            "TlsGetValue": Stub(1, lambda i: self.tls.get(i, 0)),
            "TlsSetValue": Stub(2, self.tls_set),
            "InitializeCriticalSection": Stub(1, lambda cs: 0),
            "DeleteCriticalSection": Stub(1, lambda cs: 0),
            "EnterCriticalSection": Stub(1, lambda cs: 0),
            "LeaveCriticalSection": Stub(1, lambda cs: 0),
            "InterlockedIncrement": Stub(1, lambda p: self.interlocked(p, 1)),
            "InterlockedDecrement": Stub(1, lambda p: self.interlocked(p, -1)),
            "InterlockedExchange": Stub(2, self.interlocked_exchange),
            "GetProcessHeap": Stub(0, lambda: PROCESS_HEAP),
            "HeapAlloc": Stub(3, lambda heap, flags, size: e.alloc(size)),
            "HeapFree": Stub(3, lambda heap, flags, block: e.freed.append(block) or 1),
            "HeapSize": Stub(3, lambda heap, flags, block: e.blocks.get(block, 0)),
            "HeapCompact": Stub(2, lambda heap, flags: 0),
            "lstrlenA": Stub(1, lambda p: len(e.read_cstr(p)) if p else 0),
            "OutputDebugStringA": Stub(1, lambda p: 0),
            "CreateFileA": Stub(7, self.create_file),
            "ReadFile": Stub(5, self.read_file),
            "GetFileSize": Stub(2, self.get_file_size),
            "SetFilePointer": Stub(4, self.set_file_pointer),
            "CloseHandle": Stub(1, self.close_handle),
            "GetFileAttributesA": Stub(1, self.get_file_attributes),
            "GetACP": Stub(0, lambda: ANSI_CODE_PAGE),
            "GetOEMCP": Stub(0, lambda: OEM_CODE_PAGE),
            "GetCPInfo": Stub(2, self.get_cp_info),
            "SetUnhandledExceptionFilter": Stub(1, lambda fn: 0),
            "GetModuleHandleA": Stub(1, self.get_module_handle),
            "GetStartupInfoA": Stub(1, self.get_startup_info),
            "GetStdHandle": Stub(1, lambda which: STD_HANDLES.get(which, INVALID_HANDLE_VALUE)),
            "GetFileType": Stub(1, lambda h: FILE_TYPE_CHAR if h in STD_HANDLES.values() else FILE_TYPE_DISK),
            "SetHandleCount": Stub(1, lambda n: n),
            "GetStringTypeW": Stub(4, self.not_implemented),
            "LCMapStringW": Stub(6, self.not_implemented),
            "GetStringTypeA": Stub(5, self.get_string_type),
            "LCMapStringA": Stub(6, self.lc_map_string),
            "GetProcAddress": Stub(2, lambda module, name: 0),
        }
        winmm = {
            "timeGetTime": Stub(0, self.tick),
        }
        user32 = {
            "GetClientRect": Stub(2, self.get_client_rect),
            "ClientToScreen": Stub(2, self.client_to_screen),
            "SetRect": Stub(5, self.set_rect),
            "SetWindowLongA": Stub(3, self.set_window_long),
        }
        ddraw = {
            "DirectDrawCreateEx": Stub(4, self.direct_draw_create_ex),
        }
        self.window_longs = {}
        self.stubs = {f"KERNEL32.DLL!{k}": v for k, v in k32.items()}
        self.stubs.update({f"WINMM.DLL!{k}": v for k, v in winmm.items()})
        self.stubs.update({f"USER32.DLL!{k}": v for k, v in user32.items()})
        self.stubs.update({f"DDRAW.DLL!{k}": v for k, v in ddraw.items()})

    def lookup(self, name):
        return self.stubs.get(name)

    # -- process ----------------------------------------------------------------------

    def set_last_error(self, code):
        self.last_error = code
        return 0

    def tick(self):
        self.ticks += TICK_STEP
        return self.ticks

    def tls_alloc(self):
        i = self.next_tls
        self.next_tls += 1
        self.tls[i] = 0
        return i

    def tls_set(self, i, value):
        self.tls[i] = value
        return 1

    def interlocked(self, p, delta):
        v = (self.emu.read_u32(p) + delta) & 0xFFFFFFFF
        self.emu.write_u32(p, v)
        return v

    def interlocked_exchange(self, p, value):
        old = self.emu.read_u32(p)
        self.emu.write_u32(p, value)
        return old

    def get_module_handle(self, name):
        """The image base for NULL, else a made-up handle per module name. GetProcAddress
        finds nothing in those, so callers take their fallback paths."""
        if not name:
            return self.emu.base
        key = self.emu.read_cstr(name).lower()
        return self.modules.setdefault(key, MODULE_HANDLES + 0x10000 * len(self.modules))

    def get_startup_info(self, out):
        """A STARTUPINFOA with only cb set: no inherited handles (cbReserved2 = 0)."""
        self.emu.mem_write(out, struct.pack("<I", STARTUPINFO_SIZE) + b"\0" * (STARTUPINFO_SIZE - 4))
        return 0

    def not_implemented(self, *args):
        """The wide-character functions are absent, as on Windows 9x; the CRT falls back to
        the ANSI ones."""
        self.last_error = ERROR_CALL_NOT_IMPLEMENTED
        return 0

    @staticmethod
    def ctype1(byte):
        ch = bytes([byte]).decode("cp1252", errors="replace")
        cat = unicodedata.category(ch)
        t = 0
        if cat == "Lu":
            t |= C1_UPPER | C1_ALPHA
        elif cat == "Ll":
            t |= C1_LOWER | C1_ALPHA
        elif cat.startswith("L"):
            t |= C1_ALPHA
        if ch in "0123456789":
            t |= C1_DIGIT
        if ch in "0123456789abcdefABCDEF":
            t |= C1_XDIGIT
        if ch in " \t\n\v\f\r":
            t |= C1_SPACE
        if ch in " \t":
            t |= C1_BLANK
        if cat.startswith("C") or ch == "\ufffd":
            t |= C1_CNTRL
        elif cat[0] in "PS":
            t |= C1_PUNCT
        return t

    def get_string_type(self, lcid, info_type, src, count, out):
        if info_type != CT_CTYPE1:
            return 0
        data = self.emu.mem_read(src, count) if count >= 0 else self.emu.read_cstr(src).encode("cp1252") + b"\0"
        self.emu.mem_write(out, b"".join(struct.pack("<H", self.ctype1(b)) for b in data))
        return 1

    def lc_map_string(self, lcid, flags, src, count, dest, dest_count):
        data = self.emu.mem_read(src, count) if count >= 0 else self.emu.read_cstr(src).encode("cp1252") + b"\0"
        text = data.decode("cp1252", errors="replace")
        if flags & LCMAP_UPPERCASE:
            text = "".join(c.upper() if len(c.upper()) == 1 else c for c in text)
        elif flags & LCMAP_LOWERCASE:
            text = "".join(c.lower() if len(c.lower()) == 1 else c for c in text)
        out = text.encode("cp1252", errors="replace")
        if dest_count == 0:
            return len(out)
        self.emu.mem_write(dest, out[:dest_count])
        return min(len(out), dest_count)

    def get_cp_info(self, code_page, out):
        """CPINFO for a single-byte code page: MaxCharSize 1, DefaultChar '?', no lead bytes."""
        if code_page not in (ANSI_CODE_PAGE, OEM_CODE_PAGE):
            return 0
        self.emu.mem_write(out, struct.pack("<I2s12s", 1, b"?\0", b"\0" * 12))
        return 1

    # -- windows ----------------------------------------------------------------------

    def get_client_rect(self, hwnd, out):
        w, h, _ = self.emu.directx.profile.display
        self.emu.mem_write(out, struct.pack("<4i", 0, 0, w, h))
        return 1

    def client_to_screen(self, hwnd, point):
        x, y = struct.unpack("<2i", self.emu.mem_read(point, 8))
        self.emu.mem_write(point, struct.pack("<2i", x + WINDOW_ORIGIN[0], y + WINDOW_ORIGIN[1]))
        return 1

    def set_rect(self, out, left, top, right, bottom):
        self.emu.mem_write(out, struct.pack("<4I", left, top, right, bottom))
        return 1

    def set_window_long(self, hwnd, index, value):
        old = self.window_longs.get(index, 0)
        self.window_longs[index] = value
        return old

    # -- DirectDraw -------------------------------------------------------------------

    def direct_draw_create_ex(self, guid, out, iid, outer):
        if self.emu.mem_read(iid, 16) != IID_IDirectDraw7:
            return DDERR_INVALIDPARAMS
        self.emu.write_u32(out, self.emu.directx.create_directdraw().address)
        return 0

    # -- files ------------------------------------------------------------------------

    def resolve(self, name):
        """A Windows path under the install tree, matched case-insensitively."""
        parts = [p for p in name.replace("/", "\\").split("\\") if p and p != "."]
        if parts and len(parts[0]) == 2 and parts[0][1] == ":":
            parts = parts[1:]
        path = self.files_root
        for part in parts:
            if part == "..":
                path = os.path.dirname(path)
                continue
            try:
                entries = {e.lower(): e for e in os.listdir(path)}
            except OSError:
                return None
            if part.lower() not in entries:
                return None
            path = os.path.join(path, entries[part.lower()])
        return path

    def create_file(self, name, access, share, security, disposition, flags, template):
        path = self.resolve(self.emu.read_cstr(name))
        if access & GENERIC_WRITE:
            self.last_error = ERROR_ACCESS_DENIED
            return INVALID_HANDLE_VALUE
        if path is None or not os.path.isfile(path):
            self.last_error = ERROR_FILE_NOT_FOUND
            return INVALID_HANDLE_VALUE
        with open(path, "rb") as f:
            data = f.read()
        h = self.next_handle
        self.next_handle += 4
        self.handles[h] = [data, 0]
        return h

    def read_file(self, h, buffer, n, read_out, overlapped):
        if h not in self.handles:
            return 0
        data, pos = self.handles[h]
        chunk = data[pos:pos + n]
        self.emu.mem_write(buffer, chunk)
        self.handles[h][1] = pos + len(chunk)
        if read_out:
            self.emu.write_u32(read_out, len(chunk))
        return 1

    def get_file_size(self, h, high_out):
        if h not in self.handles:
            return INVALID_HANDLE_VALUE
        if high_out:
            self.emu.write_u32(high_out, 0)
        return len(self.handles[h][0])

    def set_file_pointer(self, h, distance, high, method):
        if h not in self.handles:
            return INVALID_HANDLE_VALUE
        data, pos = self.handles[h]
        d = struct.unpack("<i", struct.pack("<I", distance))[0]
        pos = [0, pos, len(data)][method] + d
        self.handles[h][1] = pos
        return pos

    def close_handle(self, h):
        return 1 if self.handles.pop(h, None) is not None else 0

    def get_file_attributes(self, name):
        path = self.resolve(self.emu.read_cstr(name))
        if path is None:
            self.last_error = ERROR_FILE_NOT_FOUND
            return INVALID_HANDLE_VALUE
        return FILE_ATTRIBUTE_DIRECTORY if os.path.isdir(path) else FILE_ATTRIBUTE_ARCHIVE
