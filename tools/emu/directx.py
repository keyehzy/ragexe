"""DirectDraw 7 and Direct3D 7 fakes for the emulation oracle: COM objects that record
every call made to them.

An object is 8 bytes in emulated memory, a vtable pointer and an id. Each vtable slot
points at its own one-byte stub; a code hook on the stub area finds the interface and
method, reads `this` and the arguments (COM methods are __stdcall with `this` first),
records the call and runs the method's handler, which returns the HRESULT. A method
without a handler stops the run with its name, as an unstubbed import does.

The interfaces are the DirectX 7 ones the client asks for by IID (IID_IDirectDraw7 at
005ec478, IID_IDirect3D7 at 005ec148, the HAL and T&L HAL device IDs at 005ec118 and
005ec0d8), with their slots in the order of the DirectX 7 SDK. The client's own vtable
offsets agree: C3dDevice::Init sets render states through slot 20 (+0x50) and texture
stage states through slot 37 (+0x94), with the argument counts of SetRenderState and
SetTextureStageState.

What the fakes do:
  - Surfaces get real pixel memory in their own region, so Lock gives a pointer and a
    pitch the caller can write through. A primary surface takes the display mode's size
    and pixel format; FLIP | COMPLEX with a back buffer count creates the attached back
    buffer.
  - The device keeps the render states, texture stage states, textures, transforms and
    viewport it is given. Every draw call records its primitive type, its vertices
    (decoded by FVF) and indices, and a snapshot of that state, for comparison with what
    the port submits (docs/PLAN.md, section 4.3, draw-call comparison).
  - Enumerations call back into the client through Emulator.callbacks: display modes,
    z-buffer formats and texture formats from the profile below.
  - Reference counts are kept: C3dDevice::Release reports a device still referenced.
"""
import struct
import uuid

DD_OK = 0
E_NOINTERFACE = 0x80004002
DDERR_INVALIDPARAMS = 0x80070057
DDERR_NOTFOUND = 0x887600FF
DDENUMRET_CANCEL = 0

COM_BASE, COM_SIZE = 0x40000000, 0x00400000          # stubs, vtables, objects, structures
STUB_AREA = 0x2000
SURFACE_BASE, SURFACE_SIZE = 0x50000000, 0x08000000   # pixel and vertex buffer memory


def guid(text):
    return uuid.UUID(text).bytes_le


IID_IDirectDraw7 = guid("15e65ec0-3b9c-11d2-b92f-00609797ea5b")
IID_IDirect3D7 = guid("f5049e77-4861-11d2-a407-00a0c90629a8")
IID_IDirectDrawGammaControl = guid("69c11c3e-b46b-11d1-ad7a-00c04fc29b4e")
DEVICES = {
    guid("84e63de0-46aa-11cf-816f-0000c020156e"): "IID_IDirect3DHALDevice",
    guid("f5049e78-4861-11d2-a407-00a0c90629a8"): "IID_IDirect3DTnLHalDevice",
    guid("a4665c60-2673-11cf-a31a-00aa00b93356"): "IID_IDirect3DRGBDevice",
    guid("50936643-13e9-11d1-89aa-00a0c9054129"): "IID_IDirect3DRefDevice",
}

IUNKNOWN = [("QueryInterface", 2), ("AddRef", 0), ("Release", 0)]
INTERFACES = {
    "IDirectDraw7": IUNKNOWN + [
        ("Compact", 0), ("CreateClipper", 3), ("CreatePalette", 4), ("CreateSurface", 3),
        ("DuplicateSurface", 2), ("EnumDisplayModes", 4), ("EnumSurfaces", 4), ("FlipToGDISurface", 0),
        ("GetCaps", 2), ("GetDisplayMode", 1), ("GetFourCCCodes", 2), ("GetGDISurface", 1),
        ("GetMonitorFrequency", 1), ("GetScanLine", 1), ("GetVerticalBlankStatus", 1), ("Initialize", 1),
        ("RestoreDisplayMode", 0), ("SetCooperativeLevel", 2), ("SetDisplayMode", 5),
        ("WaitForVerticalBlank", 2), ("GetAvailableVidMem", 3), ("GetSurfaceFromDC", 2),
        ("RestoreAllSurfaces", 0), ("TestCooperativeLevel", 0), ("GetDeviceIdentifier", 2),
        ("StartModeTest", 3), ("EvaluateMode", 2)],
    "IDirectDrawSurface7": IUNKNOWN + [
        ("AddAttachedSurface", 1), ("AddOverlayDirtyRect", 1), ("Blt", 5), ("BltBatch", 3), ("BltFast", 5),
        ("DeleteAttachedSurface", 2), ("EnumAttachedSurfaces", 2), ("EnumOverlayZOrders", 3), ("Flip", 2),
        ("GetAttachedSurface", 2), ("GetBltStatus", 1), ("GetCaps", 1), ("GetClipper", 1), ("GetColorKey", 2),
        ("GetDC", 1), ("GetFlipStatus", 1), ("GetOverlayPosition", 2), ("GetPalette", 1),
        ("GetPixelFormat", 1), ("GetSurfaceDesc", 1), ("Initialize", 2), ("IsLost", 0), ("Lock", 4),
        ("ReleaseDC", 1), ("Restore", 0), ("SetClipper", 1), ("SetColorKey", 2), ("SetOverlayPosition", 2),
        ("SetPalette", 1), ("Unlock", 1), ("UpdateOverlay", 5), ("UpdateOverlayDisplay", 1),
        ("UpdateOverlayZOrder", 2), ("GetDDInterface", 1), ("PageLock", 1), ("PageUnlock", 1),
        ("SetSurfaceDesc", 2), ("SetPrivateData", 4), ("GetPrivateData", 3), ("FreePrivateData", 1),
        ("GetUniquenessValue", 1), ("ChangeUniquenessValue", 0), ("SetPriority", 1), ("GetPriority", 1),
        ("SetLOD", 1), ("GetLOD", 1)],
    "IDirectDrawClipper": IUNKNOWN + [
        ("GetClipList", 3), ("GetHWnd", 1), ("Initialize", 2), ("IsClipListChanged", 1), ("SetClipList", 2),
        ("SetHWnd", 2)],
    "IDirectDrawPalette": IUNKNOWN + [
        ("GetCaps", 1), ("GetEntries", 4), ("Initialize", 3), ("SetEntries", 4)],
    "IDirectDrawGammaControl": IUNKNOWN + [("GetGammaRamp", 2), ("SetGammaRamp", 2)],
    "IDirect3D7": IUNKNOWN + [
        ("EnumDevices", 2), ("CreateDevice", 3), ("CreateVertexBuffer", 3), ("EnumZBufferFormats", 3),
        ("EvictManagedTextures", 0)],
    "IDirect3DDevice7": IUNKNOWN + [
        ("GetCaps", 1), ("EnumTextureFormats", 2), ("BeginScene", 0), ("EndScene", 0), ("GetDirect3D", 1),
        ("SetRenderTarget", 2), ("GetRenderTarget", 1), ("Clear", 6), ("SetTransform", 2), ("GetTransform", 2),
        ("SetViewport", 1), ("MultiplyTransform", 2), ("GetViewport", 1), ("SetMaterial", 1),
        ("GetMaterial", 1), ("SetLight", 2), ("GetLight", 2), ("SetRenderState", 2), ("GetRenderState", 2),
        ("BeginStateBlock", 0), ("EndStateBlock", 1), ("PreLoad", 1), ("DrawPrimitive", 5),
        ("DrawIndexedPrimitive", 7), ("SetClipStatus", 1), ("GetClipStatus", 1), ("DrawPrimitiveStrided", 5),
        ("DrawIndexedPrimitiveStrided", 7), ("DrawPrimitiveVB", 5), ("DrawIndexedPrimitiveVB", 7),
        ("ComputeSphereVisibility", 5), ("GetTexture", 2), ("SetTexture", 2), ("GetTextureStageState", 3),
        ("SetTextureStageState", 3), ("ValidateDevice", 1), ("ApplyStateBlock", 1), ("CaptureStateBlock", 1),
        ("DeleteStateBlock", 1), ("CreateStateBlock", 2), ("Load", 5), ("LightEnable", 2),
        ("GetLightEnable", 2), ("SetClipPlane", 2), ("GetClipPlane", 2), ("GetInfo", 3)],
    "IDirect3DVertexBuffer7": IUNKNOWN + [
        ("Lock", 3), ("Unlock", 0), ("ProcessVertices", 7), ("GetVertexBufferDesc", 1), ("Optimize", 2),
        ("ProcessVerticesStrided", 7)],
}

RENDER_STATES = {
    2: "ANTIALIAS", 4: "TEXTUREPERSPECTIVE", 7: "ZENABLE", 8: "FILLMODE", 9: "SHADEMODE", 10: "LINEPATTERN",
    14: "ZWRITEENABLE", 15: "ALPHATESTENABLE", 16: "LASTPIXEL", 19: "SRCBLEND", 20: "DESTBLEND",
    22: "CULLMODE", 23: "ZFUNC", 24: "ALPHAREF", 25: "ALPHAFUNC", 26: "DITHERENABLE", 27: "ALPHABLENDENABLE",
    28: "FOGENABLE", 29: "SPECULARENABLE", 30: "ZVISIBLE", 33: "STIPPLEDALPHA", 34: "FOGCOLOR",
    35: "FOGTABLEMODE", 36: "FOGSTART", 37: "FOGEND", 38: "FOGDENSITY", 40: "EDGEANTIALIAS",
    41: "COLORKEYENABLE", 47: "ZBIAS", 48: "RANGEFOGENABLE", 52: "STENCILENABLE", 53: "STENCILFAIL",
    54: "STENCILZFAIL", 55: "STENCILPASS", 56: "STENCILFUNC", 57: "STENCILREF", 58: "STENCILMASK",
    59: "STENCILWRITEMASK", 60: "TEXTUREFACTOR", **{128 + k: f"WRAP{k}" for k in range(8)},
    136: "CLIPPING", 137: "LIGHTING", 139: "AMBIENT", 140: "FOGVERTEXMODE", 141: "COLORVERTEX",
    142: "LOCALVIEWER", 143: "NORMALIZENORMALS", 144: "COLORKEYBLENDENABLE", 145: "DIFFUSEMATERIALSOURCE",
    146: "SPECULARMATERIALSOURCE", 147: "AMBIENTMATERIALSOURCE", 148: "EMISSIVEMATERIALSOURCE",
    151: "VERTEXBLEND", 152: "CLIPPLANEENABLE",
}
STAGE_STATES = {
    1: "COLOROP", 2: "COLORARG1", 3: "COLORARG2", 4: "ALPHAOP", 5: "ALPHAARG1", 6: "ALPHAARG2",
    7: "BUMPENVMAT00", 8: "BUMPENVMAT01", 9: "BUMPENVMAT10", 10: "BUMPENVMAT11", 11: "TEXCOORDINDEX",
    12: "ADDRESS", 13: "ADDRESSU", 14: "ADDRESSV", 15: "BORDERCOLOR", 16: "MAGFILTER", 17: "MINFILTER",
    18: "MIPFILTER", 19: "MIPMAPLODBIAS", 20: "MAXMIPLEVEL", 21: "MAXANISOTROPY", 22: "BUMPENVLSCALE",
    23: "BUMPENVLOFFSET", 24: "TEXTURETRANSFORMFLAGS",
}
TEXTURE_OPS = {
    1: "DISABLE", 2: "SELECTARG1", 3: "SELECTARG2", 4: "MODULATE", 5: "MODULATE2X", 6: "MODULATE4X", 7: "ADD",
    8: "ADDSIGNED", 9: "ADDSIGNED2X", 10: "SUBTRACT", 11: "ADDSMOOTH", 12: "BLENDDIFFUSEALPHA",
    13: "BLENDTEXTUREALPHA", 14: "BLENDFACTORALPHA", 15: "BLENDTEXTUREALPHAPM", 16: "BLENDCURRENTALPHA",
    17: "PREMODULATE", 18: "MODULATEALPHA_ADDCOLOR", 19: "MODULATECOLOR_ADDALPHA",
    20: "MODULATEINVALPHA_ADDCOLOR", 21: "MODULATEINVCOLOR_ADDALPHA", 22: "BUMPENVMAP",
    23: "BUMPENVMAPLUMINANCE", 24: "DOTPRODUCT3",
}
TEXTURE_ARGS = {0: "DIFFUSE", 1: "CURRENT", 2: "TEXTURE", 3: "TFACTOR", 4: "SPECULAR"}
TA_COMPLEMENT, TA_ALPHAREPLICATE = 0x10, 0x20


def texture_arg(value):
    """A D3DTA_ argument as text, with its modifiers: TEXTURE|COMPLEMENT."""
    name = TEXTURE_ARGS.get(value & 0xF, str(value & 0xF))
    if value & TA_COMPLEMENT:
        name += "|COMPLEMENT"
    if value & TA_ALPHAREPLICATE:
        name += "|ALPHAREPLICATE"
    return name


PRIMITIVES = {1: "POINTLIST", 2: "LINELIST", 3: "LINESTRIP", 4: "TRIANGLELIST", 5: "TRIANGLESTRIP",
              6: "TRIANGLEFAN"}

# DDSURFACEDESC2 (124 bytes) and DDPIXELFORMAT (32 bytes)
DESC_SIZE, PF_SIZE = 0x7C, 0x20
DESC_FLAGS, DESC_HEIGHT, DESC_WIDTH, DESC_PITCH, DESC_BACKBUFFERS, DESC_SURFACE = 4, 8, 0xC, 0x10, 0x14, 0x24
DESC_PF, DESC_CAPS = 0x48, 0x68
DDSD_CAPS, DDSD_HEIGHT, DDSD_WIDTH, DDSD_PITCH, DDSD_BACKBUFFERCOUNT, DDSD_LPSURFACE, DDSD_PIXELFORMAT = \
    0x1, 0x2, 0x4, 0x8, 0x20, 0x800, 0x1000
DDSCAPS_BACKBUFFER, DDSCAPS_COMPLEX, DDSCAPS_FLIP, DDSCAPS_FRONTBUFFER, DDSCAPS_PRIMARYSURFACE = \
    0x4, 0x8, 0x10, 0x20, 0x200
DDSCAPS_TEXTURE, DDSCAPS_3DDEVICE, DDSCAPS_ZBUFFER = 0x1000, 0x2000, 0x20000
DDPF_ALPHAPIXELS, DDPF_RGB, DDPF_ZBUFFER, DDPF_STENCILBUFFER = 0x1, 0x40, 0x400, 0x4000
DDBLT_COLORFILL = 0x400

D3DFVF_POSITION_MASK, D3DFVF_NORMAL, D3DFVF_RESERVED1, D3DFVF_DIFFUSE, D3DFVF_SPECULAR = \
    0xE, 0x10, 0x20, 0x40, 0x80
D3DPRCAPS_ZBUFFERLESSHSR = 0x8000
DEVICE_DESC_SIZE = 236          # D3DDEVICEDESC7


def pixel_format(bits, r=0, g=0, b=0, a=0, flags=None):
    if flags is None:
        flags = DDPF_RGB | (DDPF_ALPHAPIXELS if a else 0)
    return struct.pack("<8I", PF_SIZE, flags, 0, bits, r, g, b, a)


def z_format(bits, stencil=0):
    mask = (1 << (bits - stencil)) - 1
    smask = ((1 << stencil) - 1) << (bits - stencil) if stencil else 0
    flags = DDPF_ZBUFFER | (DDPF_STENCILBUFFER if stencil else 0)
    return struct.pack("<8I", PF_SIZE, flags, 0, bits, stencil, mask, smask, 0)


PF_565 = pixel_format(16, 0xF800, 0x07E0, 0x001F)
PF_555 = pixel_format(16, 0x7C00, 0x03E0, 0x001F)
PF_1555 = pixel_format(16, 0x7C00, 0x03E0, 0x001F, 0x8000)
PF_4444 = pixel_format(16, 0x0F00, 0x00F0, 0x000F, 0xF000)
PF_8888 = pixel_format(32, 0xFF0000, 0x00FF00, 0x0000FF, 0xFF000000)
PF_X888 = pixel_format(32, 0xFF0000, 0x00FF00, 0x0000FF)


class Profile:
    """The display and device the fakes present. The defaults are an 800x600 desktop in
    16-bit 565 colour and a hardware device with a 16-bit z-buffer."""
    def __init__(self):
        self.display = (800, 600, PF_565)
        self.display_modes = [(w, h, pf) for w, h in ((640, 480), (800, 600), (1024, 768))
                              for pf in (PF_565, PF_X888)]
        self.z_formats = [z_format(16), z_format(24), z_format(32, 8)]
        self.texture_formats = [PF_565, PF_1555, PF_4444, PF_8888]
        self.video_memory = 32 << 20
        self.max_texture = 2048


class Call:
    """One recorded method call. args are the raw 32-bit arguments after `this`; detail
    holds what the handler decoded (structures, vertices, a state snapshot)."""
    __slots__ = ("interface", "obj", "method", "args", "result", "detail", "caller")

    def __init__(self, interface, obj, method, args, caller):
        self.interface, self.obj, self.method, self.args, self.caller = interface, obj, method, args, caller
        self.result, self.detail = None, {}

    def __repr__(self):
        return f"{self.obj}.{self.method}({', '.join(f'0x{a:x}' for a in self.args)}) -> 0x{(self.result or 0):x}"


class ComObject:
    def __init__(self, dx, interface, role):
        self.dx, self.interface, self.role = dx, interface, role
        self.refs = 1
        self.id = len(dx.objects) + 1
        self.address = dx.com_alloc(8)
        dx.emu.write_u32(self.address, dx.vtables[interface])
        dx.emu.write_u32(self.address + 4, self.id)
        dx.objects[self.address] = self

    @property
    def label(self):
        return f"{self.role}#{self.id}"

    # IUnknown
    def QueryInterface(self, riid, out):
        iid = self.dx.emu.mem_read(riid, 16)
        target = self.query(iid)
        if target is None:
            if out:
                self.dx.emu.write_u32(out, 0)
            return E_NOINTERFACE
        target.refs += 1
        self.dx.emu.write_u32(out, target.address)
        self.dx.current.detail["iid"] = str(uuid.UUID(bytes_le=iid))
        return DD_OK

    def query(self, iid):
        return None

    def AddRef(self):
        self.refs += 1
        return self.refs

    def Release(self):
        self.refs -= 1
        return self.refs


class Surface(ComObject):
    def __init__(self, dx, width, height, pf, caps, role):
        super().__init__(dx, "IDirectDrawSurface7", role)
        self.width, self.height, self.pf, self.caps = width, height, pf, caps
        bits = struct.unpack_from("<I", pf, 12)[0] or 16
        self.pitch = (width * bits // 8 + 3) & ~3
        self.memory = dx.surface_alloc(self.pitch * height)
        self.attached = []
        self.clipper = None
        self.color_keys = {}
        self.locked = False

    def desc(self):
        d = bytearray(DESC_SIZE)
        flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT
        struct.pack_into("<IIIIi", d, 0, DESC_SIZE, flags, self.height, self.width, self.pitch)
        d[DESC_PF:DESC_PF + PF_SIZE] = self.pf
        struct.pack_into("<I", d, DESC_CAPS, self.caps)
        return d

    def write_desc(self, out, with_memory=False):
        d = self.desc()
        if with_memory:
            struct.pack_into("<I", d, DESC_FLAGS, struct.unpack_from("<I", d, DESC_FLAGS)[0] | DDSD_LPSURFACE)
            struct.pack_into("<I", d, DESC_SURFACE, self.memory)
        size = self.dx.emu.read_u32(out) or DESC_SIZE
        self.dx.emu.mem_write(out, bytes(d[:min(size, DESC_SIZE)]))

    def pixels(self):
        """The surface memory as bytes, row by row (pitch included)."""
        return self.dx.emu.mem_read(self.memory, self.pitch * self.height)

    def AddAttachedSurface(self, surface):
        s = self.dx.objects.get(surface)
        if s is None:
            return DDERR_INVALIDPARAMS
        s.refs += 1
        self.attached.append(s)
        self.dx.current.detail["surface"] = s.label
        return DD_OK

    def GetAttachedSurface(self, caps, out):
        want = self.dx.emu.read_u32(caps)
        for s in self.attached:
            if s.caps & want == want:
                s.refs += 1
                self.dx.emu.write_u32(out, s.address)
                self.dx.current.detail["surface"] = s.label
                return DD_OK
        self.dx.emu.write_u32(out, 0)
        return DDERR_NOTFOUND

    def GetSurfaceDesc(self, out):
        self.write_desc(out)
        return DD_OK

    def GetPixelFormat(self, out):
        self.dx.emu.mem_write(out, self.pf)
        return DD_OK

    def GetCaps(self, out):
        self.dx.emu.mem_write(out, struct.pack("<4I", self.caps, 0, 0, 0))
        return DD_OK

    def Lock(self, rect, desc, flags, event):
        self.locked = True
        self.write_desc(desc, with_memory=True)
        self.dx.current.detail["rect"] = self.dx.rect(rect)
        return DD_OK

    def Unlock(self, rect):
        self.locked = False
        return DD_OK

    def IsLost(self):
        return DD_OK

    def Restore(self):
        return DD_OK

    def SetClipper(self, clipper):
        self.clipper = self.dx.objects.get(clipper)
        if self.clipper:
            self.clipper.refs += 1
        self.dx.current.detail["clipper"] = self.clipper.label if self.clipper else None
        return DD_OK

    def SetColorKey(self, flags, key):
        value = struct.unpack("<II", self.dx.emu.mem_read(key, 8)) if key else None
        self.color_keys[flags] = value
        self.dx.current.detail["key"] = value
        return DD_OK

    def Blt(self, dst_rect, src, src_rect, flags, fx):
        d = self.dx.current.detail
        d.update(dst=self.dx.rect(dst_rect), src=self.dx.label_of(src), src_rect=self.dx.rect(src_rect))
        if flags & DDBLT_COLORFILL and fx:
            d["fill"] = self.dx.emu.read_u32(fx + 0x50)     # DDBLTFX.dwFillColor
        return DD_OK

    def BltFast(self, x, y, src, src_rect, trans):
        self.dx.current.detail.update(x=x, y=y, src=self.dx.label_of(src), src_rect=self.dx.rect(src_rect))
        return DD_OK

    def Flip(self, override, flags):
        return DD_OK


class Clipper(ComObject):
    def __init__(self, dx):
        super().__init__(dx, "IDirectDrawClipper", "clipper")
        self.hwnd = 0

    def SetHWnd(self, flags, hwnd):
        self.hwnd = hwnd
        return DD_OK

    def GetHWnd(self, out):
        self.dx.emu.write_u32(out, self.hwnd)
        return DD_OK


class GammaControl(ComObject):
    def __init__(self, dx):
        super().__init__(dx, "IDirectDrawGammaControl", "gamma")
        self.ramp = b"".join(struct.pack("<H", v * 257) for v in range(256)) * 3

    def GetGammaRamp(self, flags, out):
        self.dx.emu.mem_write(out, self.ramp)
        return DD_OK

    def SetGammaRamp(self, flags, ramp):
        self.ramp = self.dx.emu.mem_read(ramp, 3 * 512)
        return DD_OK


class DirectDraw(ComObject):
    def __init__(self, dx):
        super().__init__(dx, "IDirectDraw7", "ddraw")
        self.mode = dx.profile.display
        self.cooperative = None
        self.d3d = None

    def query(self, iid):
        if iid == IID_IDirectDraw7:
            return self
        if iid == IID_IDirect3D7:
            if self.d3d is None:
                self.d3d = Direct3D(self.dx, self)
                self.d3d.refs = 0
            return self.d3d
        return None

    def GetDisplayMode(self, out):
        w, h, pf = self.mode
        d = bytearray(DESC_SIZE)
        bits = struct.unpack_from("<I", pf, 12)[0]
        struct.pack_into("<IIIIi", d, 0, DESC_SIZE, DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT,
                         h, w, w * bits // 8)
        d[DESC_PF:DESC_PF + PF_SIZE] = pf
        self.dx.emu.mem_write(out, bytes(d))
        return DD_OK

    def SetCooperativeLevel(self, hwnd, flags):
        self.cooperative = (hwnd, flags)
        return DD_OK

    def SetDisplayMode(self, width, height, bits, refresh, flags):
        pf = next((m[2] for m in self.dx.profile.display_modes
                   if struct.unpack_from("<I", m[2], 12)[0] == bits), None)
        if pf is None:
            return DDERR_INVALIDPARAMS
        self.mode = (width, height, pf)
        return DD_OK

    def RestoreDisplayMode(self):
        self.mode = self.dx.profile.display
        return DD_OK

    def EnumDisplayModes(self, flags, desc, context, callback):
        for w, h, pf in self.dx.profile.display_modes:
            d = bytearray(DESC_SIZE)
            bits = struct.unpack_from("<I", pf, 12)[0]
            struct.pack_into("<IIIIi", d, 0, DESC_SIZE, DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT,
                             h, w, w * bits // 8)
            d[DESC_PF:DESC_PF + PF_SIZE] = pf
            if (yield callback, [self.dx.com_bytes(d), context]) == DDENUMRET_CANCEL:
                break
        return DD_OK

    def CreateSurface(self, desc, out, outer):
        emu = self.dx.emu
        d = emu.mem_read(desc, DESC_SIZE)
        flags, height, width = struct.unpack_from("<III", d, DESC_FLAGS)
        caps = struct.unpack_from("<I", d, DESC_CAPS)[0] if flags & DDSD_CAPS else 0
        pf = d[DESC_PF:DESC_PF + PF_SIZE] if flags & DDSD_PIXELFORMAT else self.mode[2]
        if struct.unpack_from("<I", pf, 0)[0] == 0:
            pf = self.mode[2]
        if caps & DDSCAPS_PRIMARYSURFACE:
            width, height = self.mode[0], self.mode[1]
            role = "primary"
        elif caps & DDSCAPS_ZBUFFER:
            role = "zbuffer"
        elif caps & DDSCAPS_TEXTURE:
            role = "texture"
        else:
            role = "surface"
        if not (flags & DDSD_WIDTH and flags & DDSD_HEIGHT) and role != "primary":
            return DDERR_INVALIDPARAMS
        s = Surface(self.dx, width, height, pf, caps | (DDSCAPS_FRONTBUFFER if caps & DDSCAPS_FLIP else 0), role)
        backs = struct.unpack_from("<I", d, DESC_BACKBUFFERS)[0] if flags & DDSD_BACKBUFFERCOUNT else 0
        for _ in range(backs if caps & DDSCAPS_FLIP else 0):
            b = Surface(self.dx, width, height, pf,
                        (caps & ~(DDSCAPS_PRIMARYSURFACE | DDSCAPS_FRONTBUFFER)) | DDSCAPS_BACKBUFFER, "backbuffer")
            b.refs = 0
            s.attached.append(b)
        emu.write_u32(out, s.address)
        self.dx.current.detail.update(surface=s.label, width=width, height=height, caps=caps,
                                      backbuffers=backs, pixel_format=struct.unpack("<8I", pf))
        return DD_OK

    def CreateClipper(self, flags, out, outer):
        c = Clipper(self.dx)
        self.dx.emu.write_u32(out, c.address)
        self.dx.current.detail["clipper"] = c.label
        return DD_OK

    def GetCaps(self, driver, hel):
        for p in (driver, hel):
            if p:
                size = self.dx.emu.read_u32(p)
                self.dx.emu.mem_write(p + 4, b"\0" * max(size - 4, 0))
        return DD_OK

    def GetAvailableVidMem(self, caps, total, free):
        for p in (total, free):
            if p:
                self.dx.emu.write_u32(p, self.dx.profile.video_memory)
        return DD_OK

    def TestCooperativeLevel(self):
        return DD_OK

    def RestoreAllSurfaces(self):
        return DD_OK

    def WaitForVerticalBlank(self, flags, event):
        return DD_OK

    def FlipToGDISurface(self):
        return DD_OK


class Direct3D(ComObject):
    def __init__(self, dx, ddraw):
        super().__init__(dx, "IDirect3D7", "d3d")
        self.ddraw = ddraw

    def query(self, iid):
        return self if iid == IID_IDirect3D7 else None

    def CreateDevice(self, clsid, surface, out):
        kind = DEVICES.get(self.dx.emu.mem_read(clsid, 16))
        target = self.dx.objects.get(surface)
        if kind is None or target is None:
            return DDERR_INVALIDPARAMS
        dev = Device(self.dx, self, kind, target)
        self.dx.emu.write_u32(out, dev.address)
        self.dx.current.detail.update(device=kind, target=target.label)
        return DD_OK

    def EnumZBufferFormats(self, clsid, callback, context):
        for pf in self.dx.profile.z_formats:
            if (yield callback, [self.dx.com_bytes(pf), context]) == DDENUMRET_CANCEL:
                break
        return DD_OK

    def CreateVertexBuffer(self, desc, out, flags):
        size, caps, fvf, count = struct.unpack("<4I", self.dx.emu.mem_read(desc, 16))
        vb = VertexBuffer(self.dx, caps, fvf, count)
        self.dx.emu.write_u32(out, vb.address)
        self.dx.current.detail.update(buffer=vb.label, fvf=fvf, vertices=count)
        return DD_OK

    def EvictManagedTextures(self):
        return DD_OK


class VertexBuffer(ComObject):
    def __init__(self, dx, caps, fvf, count):
        super().__init__(dx, "IDirect3DVertexBuffer7", "vb")
        self.caps, self.fvf, self.count = caps, fvf, count
        self.stride = fvf_stride(fvf)
        self.memory = dx.surface_alloc(self.stride * count)

    def Lock(self, flags, data_out, size_out):
        self.dx.emu.write_u32(data_out, self.memory)
        if size_out:
            self.dx.emu.write_u32(size_out, self.stride * self.count)
        return DD_OK

    def Unlock(self):
        return DD_OK

    def GetVertexBufferDesc(self, out):
        self.dx.emu.mem_write(out, struct.pack("<4I", 16, self.caps, self.fvf, self.count))
        return DD_OK

    def Optimize(self, device, flags):
        return DD_OK


class Device(ComObject):
    def __init__(self, dx, d3d, kind, target):
        super().__init__(dx, "IDirect3DDevice7", "device")
        self.d3d, self.kind, self.target = d3d, kind, target
        self.render_states = {}
        self.stage_states = {}
        self.textures = {}
        self.transforms = {}
        self.viewport = None
        self.in_scene = False

    def snapshot(self):
        return {"render_states": dict(self.render_states), "stage_states": dict(self.stage_states),
                "textures": dict(self.textures), "target": self.target.label}

    def GetCaps(self, out):
        d = bytearray(DEVICE_DESC_SIZE)
        p = self.dx.profile
        struct.pack_into("<I", d, 4, 56)                      # dpcLineCaps.dwSize
        struct.pack_into("<I", d, 0x3C, 56)                   # dpcTriCaps.dwSize
        struct.pack_into("<4I", d, 0x7C, 1, 1, p.max_texture, p.max_texture)
        struct.pack_into("<HH", d, 0xB8, 8, 2)                # texture blend stages, simultaneous textures
        clsid = next(k for k, v in DEVICES.items() if v == self.kind)
        d[0xC4:0xD4] = clsid
        self.dx.emu.mem_write(out, bytes(d))
        return DD_OK

    def EnumTextureFormats(self, callback, context):
        for pf in self.dx.profile.texture_formats:
            if (yield callback, [self.dx.com_bytes(pf), context]) == DDENUMRET_CANCEL:
                break
        return DD_OK

    def BeginScene(self):
        self.in_scene = True
        return DD_OK

    def EndScene(self):
        self.in_scene = False
        return DD_OK

    def SetRenderTarget(self, surface, flags):
        s = self.dx.objects.get(surface)
        if s is None:
            return DDERR_INVALIDPARAMS
        self.target = s
        self.dx.current.detail["target"] = s.label
        return DD_OK

    def Clear(self, count, rects, flags, color, z, stencil):
        self.dx.current.detail.update(rects=[self.dx.d3drect(rects + 16 * k) for k in range(count)] if rects else [],
                                      flags=flags, color=color, z=struct.unpack("<f", struct.pack("<I", z))[0],
                                      stencil=stencil)
        return DD_OK

    def SetTransform(self, kind, matrix):
        m = struct.unpack("<16f", self.dx.emu.mem_read(matrix, 64))
        self.transforms[kind] = m
        self.dx.current.detail["matrix"] = m
        return DD_OK

    def GetTransform(self, kind, out):
        self.dx.emu.mem_write(out, struct.pack("<16f", *self.transforms.get(
            kind, (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1))))
        return DD_OK

    def SetViewport(self, vp):
        x, y, w, h, zmin, zmax = struct.unpack("<4I2f", self.dx.emu.mem_read(vp, 24))
        self.viewport = (x, y, w, h, zmin, zmax)
        self.dx.current.detail["viewport"] = self.viewport
        return DD_OK

    def GetViewport(self, out):
        self.dx.emu.mem_write(out, struct.pack("<4I2f", *(self.viewport or (0, 0, 0, 0, 0.0, 1.0))))
        return DD_OK

    def SetRenderState(self, state, value):
        name = RENDER_STATES.get(state, str(state))
        self.render_states[name] = value
        self.dx.current.detail["state"] = name
        return DD_OK

    def GetRenderState(self, state, out):
        self.dx.emu.write_u32(out, self.render_states.get(RENDER_STATES.get(state, str(state)), 0))
        return DD_OK

    def SetTextureStageState(self, stage, state, value):
        name = STAGE_STATES.get(state, str(state))
        self.stage_states[(stage, name)] = value
        self.dx.current.detail["state"] = name
        return DD_OK

    def GetTextureStageState(self, stage, state, out):
        self.dx.emu.write_u32(out, self.stage_states.get((stage, STAGE_STATES.get(state, str(state))), 0))
        return DD_OK

    def SetTexture(self, stage, surface):
        s = self.dx.objects.get(surface)
        self.textures[stage] = s.label if s else None
        self.dx.current.detail["texture"] = self.textures[stage]
        return DD_OK

    def ValidateDevice(self, passes):
        self.dx.emu.write_u32(passes, 1)
        return DD_OK

    def draw(self, kind, fvf, vertices, indices=None):
        d = self.dx.current.detail
        d.update(primitive=PRIMITIVES.get(kind, str(kind)), fvf=fvf, vertices=vertices, indices=indices,
                 **self.snapshot())
        self.dx.draws.append(self.dx.current)
        return DD_OK

    def DrawPrimitive(self, kind, fvf, data, count, flags):
        return self.draw(kind, fvf, self.dx.read_vertices(fvf, data, count))

    def DrawIndexedPrimitive(self, kind, fvf, data, vcount, idx, icount, flags):
        indices = list(struct.unpack(f"<{icount}H", self.dx.emu.mem_read(idx, 2 * icount)))
        return self.draw(kind, fvf, self.dx.read_vertices(fvf, data, vcount), indices)

    def DrawPrimitiveVB(self, kind, buffer, start, count, flags):
        vb = self.dx.objects[buffer]
        return self.draw(kind, vb.fvf, self.dx.read_vertices(vb.fvf, vb.memory + start * vb.stride, count))

    def DrawIndexedPrimitiveVB(self, kind, buffer, start, count, idx, icount, flags):
        vb = self.dx.objects[buffer]
        indices = list(struct.unpack(f"<{icount}H", self.dx.emu.mem_read(idx, 2 * icount)))
        return self.draw(kind, vb.fvf, self.dx.read_vertices(vb.fvf, vb.memory + start * vb.stride, count),
                         indices)


def fvf_layout(fvf):
    """[(field, format)] for one vertex of the flexible vertex format fvf."""
    out = []
    pos = fvf & D3DFVF_POSITION_MASK
    if pos == 0x2:
        out.append(("xyz", "3f"))
    elif pos == 0x4:
        out += [("xyz", "3f"), ("rhw", "f")]
    elif pos:
        out += [("xyz", "3f"), ("blend", f"{(pos - 4) // 2}f")]
    if fvf & D3DFVF_NORMAL:
        out.append(("normal", "3f"))
    if fvf & D3DFVF_RESERVED1:
        out.append(("reserved", "I"))
    if fvf & D3DFVF_DIFFUSE:
        out.append(("diffuse", "I"))
    if fvf & D3DFVF_SPECULAR:
        out.append(("specular", "I"))
    for k in range((fvf >> 8) & 0xF):
        n = {0: 2, 1: 3, 2: 4, 3: 1}[(fvf >> (16 + 2 * k)) & 3]
        out.append((f"tex{k}", f"{n}f"))
    return out


def fvf_stride(fvf):
    return struct.calcsize("<" + "".join(f for _, f in fvf_layout(fvf)))


class DirectX:
    """The fake DirectDraw/Direct3D objects of one Emulator, and what was called on them."""
    def __init__(self, emu):
        self.emu = emu
        self.profile = Profile()
        self.objects = {}
        self.calls = []
        self.draws = []
        self.current = None
        uc = emu.uc
        uc.mem_map(COM_BASE, COM_SIZE, 7)
        uc.mem_map(SURFACE_BASE, SURFACE_SIZE, 3)
        uc.mem_write(COM_BASE, b"\xc3" * STUB_AREA)
        self.com_next = COM_BASE + STUB_AREA
        self.surface_next = SURFACE_BASE
        self.stubs = {}
        self.vtables = {}
        stub = COM_BASE
        for name, methods in INTERFACES.items():
            table = self.com_alloc(4 * len(methods))
            self.vtables[name] = table
            for k, (method, nargs) in enumerate(methods):
                emu.write_u32(table + 4 * k, stub)
                self.stubs[stub] = (name, method, nargs)
                stub += 1
        self.stub_end = stub
        emu.hook_code(COM_BASE, stub - 1, self._on_method)

    # -- memory -----------------------------------------------------------------------

    def com_alloc(self, size, align=8):
        addr = (self.com_next + align - 1) & ~(align - 1)
        if addr + size > COM_BASE + COM_SIZE:
            raise MemoryError("COM area exhausted")
        self.com_next = addr + size
        return addr

    def com_bytes(self, data):
        addr = self.com_alloc(len(data))
        self.emu.mem_write(addr, bytes(data))
        return addr

    def surface_alloc(self, size):
        addr = (self.surface_next + 15) & ~15
        if addr + size > SURFACE_BASE + SURFACE_SIZE:
            raise MemoryError("surface memory exhausted")
        self.surface_next = addr + max(size, 1)
        return addr

    # -- decoding ---------------------------------------------------------------------

    def rect(self, p):
        return struct.unpack("<4i", self.emu.mem_read(p, 16)) if p else None

    def d3drect(self, p):
        return struct.unpack("<4i", self.emu.mem_read(p, 16))

    def label_of(self, addr):
        o = self.objects.get(addr)
        return o.label if o else (None if addr == 0 else f"0x{addr:08x}")

    def read_vertices(self, fvf, data, count):
        layout = fvf_layout(fvf)
        fmt = "<" + "".join(f for _, f in layout)
        stride = struct.calcsize(fmt)
        raw = self.emu.mem_read(data, stride * count)
        out = []
        for k in range(count):
            values = struct.unpack_from(fmt, raw, k * stride)
            v, pos = {}, 0
            for name, f in layout:
                n = struct.calcsize("<" + f) // 4
                v[name] = values[pos] if n == 1 else tuple(values[pos:pos + n])
                pos += n
            out.append(v)
        return out

    # -- creation ---------------------------------------------------------------------

    def create_directdraw(self):
        return DirectDraw(self)

    def create_device(self, width=None, height=None, kind="IID_IDirect3DHALDevice"):
        """A device rendering to a back buffer of the given size (the display size by
        default), for running draw code without C3dDevice::Init."""
        ddraw = self.create_directdraw()
        w, h, pf = ddraw.mode
        target = Surface(self, width or w, height or h, pf, DDSCAPS_3DDEVICE | DDSCAPS_BACKBUFFER, "backbuffer")
        ddraw.query(IID_IDirect3D7)
        return Device(self, ddraw.d3d, kind, target)

    def create_texture(self, width, height, pf=PF_4444):
        return Surface(self, width, height, pf, DDSCAPS_TEXTURE, "texture")

    # -- dispatch ---------------------------------------------------------------------

    def _on_method(self, uc, address, size, _):
        entry = self.stubs.get(address)
        if entry is None:
            return
        interface, method, nargs = entry
        emu = self.emu
        esp = emu.esp()
        ret = emu.read_u32(esp)
        this = emu.read_u32(esp + 4)
        args = [emu.read_u32(esp + 8 + 4 * k) for k in range(nargs)]
        obj = self.objects.get(this)
        handler = getattr(obj, method, None) if obj is not None else None
        if obj is None or handler is None:
            emu.fault(f"call to the unfaked method {interface}::{method} from 0x{ret:08x}")
            return
        call = Call(interface, obj.label, method, args, ret)
        self.calls.append(call)
        self.current = call

        def done(result):
            call.result = result & 0xFFFFFFFF
        emu.complete(handler(*args), ret, esp + 8 + 4 * nargs, done)
