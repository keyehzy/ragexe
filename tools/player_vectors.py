"""Test vectors for the port's player characters (docs/render/player.md), taken from the
original in the emulation oracle.

usage: .venv/bin/python -I tools/player_vectors.py > tests/unit/player_vectors.inc

  - The names: ClientState 00588ba0 fills its tables, then 00593920 and 00593960 (the
    body's .act and .spr), 005939a0 and 00593a20 (the head's) and 00593b70 (the .imf)
    build the paths for every job, both sexes and hair styles from -1 to 21.
  - CImfRes::FindEntryWithValue (0042a220) on a synthetic .imf.
  - CPc::RenderActFrameItems (00544be0): players with synthetic layers (some missing, heads
    with one or two items, anchors that match or not), in idle, walking and attack
    actions, every direction, with assorted flags and colours, drawn under views
    CView::BuildView builds, starting from assorted head offsets (006864f0, 006864f4).
    Each quad it passes to SceneRenderer::AddSpriteQuad (0040e980) is recorded, with the
    layer whose sprite it came from, and the head offset it leaves.
The x87 runs at single precision, as DirectDraw's FPUSETUP leaves it in the client.
"""
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from unicorn.x86_const import UC_X86_REG_FPCW  # noqa: E402

from actor_vectors import Oracle, bits, f32, hexs  # noqa: E402
from emu import Emulator  # noqa: E402

INIT_NAMES, CLIENT_STATE = 0x588BA0, 0x686670
BODY_ACT, BODY_SPR, HEAD_ACT, HEAD_SPR, IMF_NAME = 0x593920, 0x593960, 0x5939A0, 0x593A20, 0x593B70
FIND_ENTRY, RENDER = 0x42A220, 0x544BE0
LAYER_ACT_NAME, LAYER_SPR_NAME = 0x546460, 0x546240
GET_FRAME_COUNT = 0x421A80
MESSAGE, ON_DRAWN = 0x543540, 0x502CC0
CPC_VTABLE, HEAD_OFFSET, DEFAULT_ITEM = 0x5E9B94, 0x6864F0, 0x605B70
IMF_ACTIONS, IMF_FRAMES = 104, 3
ACTIONS = list(range(0, 16)) + list(range(32, 40)) + list(range(80, 88))


def c_bytes(data):
    return '"' + "".join(f"\\x{b:02x}" for b in data) + '"'


def names():
    emu = Emulator(max_instructions=50_000_000).init_crt()
    emu.call(INIT_NAMES, this=CLIENT_STATE)
    out = emu.scratch(size=0x100)
    hair_cell = emu.scratch(size=4)
    lines = ["// ClientState's paths: {job, sex, body .act, body .spr, .imf}, then {hair, sex, head",
             "// .act, head .spr, the hair style it leaves}."]
    lines.append("static const BodyNameVector kBodyNames[] = {")
    for job in range(45):
        for sex in (0, 1):
            texts = []
            for fn in (BODY_ACT, BODY_SPR):
                emu.call(fn, job, sex, out, this=CLIENT_STATE)
                texts.append(emu.mem_read(out, 0x100).split(b"\0")[0])
            emu.call(IMF_NAME, job, 0, sex, out, this=CLIENT_STATE)
            texts.append(emu.mem_read(out, 0x100).split(b"\0")[0])
            lines.append(f"    {{{job}, {sex}, {', '.join(c_bytes(t) for t in texts)}}},")
    lines.append("};")
    lines.append("static const HeadNameVector kHeadNames[] = {")
    for hair in range(-1, 22):
        for sex in (0, 1):
            texts = []
            for fn in (HEAD_ACT, HEAD_SPR):
                emu.write_u32(hair_cell, hair)
                emu.call(fn, 0, hair_cell, sex, out, this=CLIENT_STATE)
                texts.append(emu.mem_read(out, 0x100).split(b"\0")[0])
            lines.append(f"    {{{hair}, {sex}, {', '.join(c_bytes(t) for t in texts)}, "
                         f"{struct.unpack('<i', emu.mem_read(hair_cell, 4))[0]}}},")
    lines.append("};")
    return lines


class PlayerOracle(Oracle):
    def __init__(self):
        super().__init__()
        emu = self.emu
        self.counts = {}
        self.empty_frame = emu.scratch(size=0x4C)
        emu.replace_thiscall(GET_FRAME_COUNT, 1, lambda this, a: self.counts.get((this, a), 1))
        emu.replace_thiscall(MESSAGE, 5, lambda this, *args: 0)
        emu.replace_thiscall(ON_DRAWN, 3, lambda this, *args: 0)
        self.named = "x"
        self.x_name = emu.cstr("x")
        self.no_name = emu.cstr("")
        self.player = None

        def layer_name(table):
            def fn(this, layer):
                return self.x_name if emu.read_u32(emu.read_u32(this + table) + 4 * layer) else self.no_name
            return fn

        emu.replace_thiscall(LAYER_ACT_NAME, 1, layer_name(0x2FC))
        emu.replace_thiscall(LAYER_SPR_NAME, 1, layer_name(0x2EC))

    def get_frame(self, act, action, frame):
        return self.frames.get((act, action, frame), self.empty_frame)

    def get_item(self, frame, index):
        """ActFrame::GetItem (00420520) as it is: the item vector at +0x24, or out of range
        the static item at 00605b70."""
        first, last = self.emu.read_u32(frame + 0x24), self.emu.read_u32(frame + 0x28)
        if first and index < (last - first) // 0x24:
            return first + 0x24 * index
        return DEFAULT_ITEM

    def layer_act(self, actions):
        """A CActRes stand-in whose frames carry real item and anchor vectors, so that the
        original's GetItem runs. actions maps an action to its frames, each (items,
        anchors)."""
        emu = self.emu
        act = emu.scratch(size=0x144)
        for a, frames in actions.items():
            self.counts[(act, a)] = len(frames) if len(frames) > 1 else 1
            for f, (items, anchors) in enumerate(frames):
                frame = emu.scratch(size=0x4C)
                data = emu.scratch(b"".join(struct.pack("<iiiI4BffiI", *it) for it in items) or bytes(4))
                emu.write_u32(frame + 0x24, data)
                emu.write_u32(frame + 0x28, data + 0x24 * len(items))
                emu.write_u32(frame + 0x30, len(items))
                anchor_data = emu.scratch(b"".join(struct.pack("<3i", *an) for an in anchors) or bytes(4))
                emu.write_u32(frame + 0x3C, anchor_data)
                emu.write_u32(frame + 0x48, len(anchors))
                self.frames[(act, a, f)] = frame
        return act

    def imf(self, orders):
        """A CImfRes: orders[layer][action][frame]."""
        emu = self.emu
        imf = emu.scratch(size=0x19BC)
        entries = []
        for layer, actions in enumerate(orders):
            emu.write_u32(imf + 0x120 + 4 * layer, len(actions))
            records = bytearray()
            for a, frames in enumerate(actions):
                emu.write_u32(imf + 0x15C + 4 * (layer * 104 + a), len(frames))
                values = emu.scratch(struct.pack(f"<{len(frames)}i", *frames) or bytes(4))
                records += struct.pack("<4I", 0, values, values + 4 * len(frames), 0) + bytes(0x20)
            table = emu.scratch(bytes(records) or bytes(4))
            entries.append(struct.pack("<4I", 0, table, table + len(records), 0))
        base = emu.scratch(b"".join(entries))
        emu.write_u32(imf + 0x114, base)
        emu.write_u32(imf + 0x118, base + 16 * len(entries))
        return imf


def random_layer(rng, n8, n32, head):
    actions = {}
    for a in ACTIONS:
        frames = []
        for _ in range(rng.choice([1, 2, 3, 4])):
            count = rng.choice([1, 2, 2, 3]) if head else rng.choice([1, 1, 2])
            items = []
            for _ in range(count):
                kind = 1 if n32 and rng.random() < 0.15 else 0
                image = rng.randrange(n32 if kind else n8) if rng.random() > 0.08 else -1
                items.append((rng.randrange(-60, 30), rng.randrange(-100, 10), image, rng.choice([0, 0, 1]),
                              rng.randrange(256), rng.randrange(256), rng.randrange(256),
                              rng.choice([255, 255, 200, rng.randrange(256)]),
                              f32(rng.choice([1.0, 1.0, -1.0, 0.8, 1.25])), f32(rng.choice([1.0, 1.0, 0.9])),
                              rng.choice([0, 0, 0, 10, -20]), kind))
            anchors = []
            if rng.random() < 0.8:
                anchors.append((rng.randrange(-20, 20), rng.randrange(-80, 0), rng.choice([0, 0, 0, 1])))
            frames.append((items, anchors))
        actions[a] = frames
    return actions


def main():
    rng = random.Random(41)
    lines = ["// Generated by tools/player_vectors.py from the original in the emulation oracle."]
    lines += names()

    o = PlayerOracle()
    emu = o.emu
    # Both layers alike in shape, as in data.grf's: the search reads every layer at the
    # action and frame it checked against one.
    shape = [rng.choice([0, 1, IMF_FRAMES, IMF_FRAMES]) for _ in range(IMF_ACTIONS)]
    orders = [[[rng.choice([0, 1, 1, 2, -1]) if rng.random() < 0.1 else (layer if rng.random() < 0.8 else 1 - layer)
                for _ in range(n)] for n in shape] for layer in range(2)]
    imf = o.imf(orders)
    o.resources["imf"] = imf
    lines.append("// The .imf's orders, layer by layer, action by action: {count, orders...}.")
    for layer, actions in enumerate(orders):
        flat = []
        for frames in actions:
            flat += [len(frames)] + frames
        lines.append(f"static const int kImfLayer{layer}[] = {{{', '.join(map(str, flat))}}};")
    lines.append("// FindEntryWithValue (0042a220): {value, action, frame, layer}.")
    lines.append("static const int kImfFinds[][4] = {")
    for _ in range(300):
        value, action, frame = rng.choice([0, 1, 1, 0]), rng.randrange(0, 110), rng.randrange(0, 5)
        layer = emu.call(FIND_ENTRY, value, action, frame, this=imf).signed
        lines.append(f"    {{{value}, {action}, {frame}, {layer}}},")
    lines.append("};")

    views = []
    for pitch, yaw, distance in [(-45.0, 0.0, 300.0), (-30.0, 135.0, 250.0), (-60.0, 290.0, 400.0)]:
        at = (rng.uniform(-30, 30), rng.uniform(-10, 0), rng.uniform(-30, 30))
        views.append((o.build_view(f32(pitch), f32(yaw), f32(distance), at), at))

    images8 = [(rng.randrange(1, 90), rng.randrange(1, 110), rng.choice([0, 0, 1]), rng.choice([0, 0, 1]))
               for _ in range(10)]
    images32 = [(rng.randrange(1, 40), rng.randrange(1, 40), 0, 0) for _ in range(2)]
    lines.append("// The layers' sprite {width, height, halved x, halved y}, 8-bit then 32-bit.")
    lines.append(f"static const std::int16_t kImages8[][4] = {{{', '.join('{' + ', '.join(map(str, i)) + '}' for i in images8)}}};")
    lines.append(f"static const std::int16_t kImages32[][4] = {{{', '.join('{' + ', '.join(map(str, i)) + '}' for i in images32)}}};")
    # one sprite per layer, alike, so that the palette a quad comes with tells its layer
    sprs = [o.sprite(images8, images32) for _ in range(8)]
    records = {}
    for spr in sprs:
        for t, images in ((0, images8), (1, images32)):
            table = emu.read_u32(spr + (0x514 if t == 0 else 0x524))
            for i in range(len(images)):
                records[emu.read_u32(table + 4 * i)] = (t, i)
    palettes = {spr + 0x110: layer for layer, spr in enumerate(sprs)}

    layer_sets = []
    for k in range(6):
        layer_sets.append([random_layer(rng, len(images8), len(images32), layer == 1)
                           if layer < 2 or rng.random() < 0.6 else None for layer in range(8)])
    lines.append("// The layer sets: per set, per layer, per action in kActions, its frames {items, anchors}.")
    lines.append(f"static const int kActions[] = {{{', '.join(map(str, ACTIONS))}}};")
    set_refs = []
    acts = []
    for s, layers in enumerate(layer_sets):
        acts.append([o.layer_act(layer) if layer else 0 for layer in layers])
        layer_refs = []
        for li, layer in enumerate(layers):
            if layer is None:
                layer_refs.append("{nullptr, 0}")
                continue
            frame_refs = []
            for a in ACTIONS:
                for f, (items, anchors) in enumerate(layer[a]):
                    name = f"kSet{s}Layer{li}Action{a}Frame{f}"
                    lines.append(f"static const ItemVector {name}Items[] = {{" + (", ".join(
                        f"{{{it[0]}, {it[1]}, {it[2]}, {it[3]}, {it[4]}, {it[5]}, {it[6]}, {it[7]}, "
                        f"0x{bits(it[8]):08x}, 0x{bits(it[9]):08x}, {it[10]}, {it[11]}}}" for it in items) or "{}") + "};")
                    anchor_text = ", ".join(f"{an[0]}, {an[1]}, {an[2]}" for an in anchors) or "0"
                    lines.append(f"static const int {name}Anchors[] = {{{anchor_text}}};")
                    frame_refs.append(f"{{{a}, {f}, {name}Items, {len(items)}, {name}Anchors, {len(anchors)}}}")
            lines.append(f"static const FrameVector kSet{s}Layer{li}[] = {{{', '.join(frame_refs)}}};")
            layer_refs.append(f"{{kSet{s}Layer{li}, {len(frame_refs)}}}")
        set_refs.append("{" + ", ".join(layer_refs) + "}")
    lines.append(f"static const LayerVector kLayerSets[][8] = {{{', '.join(set_refs)}}};")

    player = emu.scratch(size=0x400)
    imf_name = emu.cstr("imf")
    spr_table = emu.scratch(size=40)
    act_table = emu.scratch(size=40)
    from actor_vectors import ADD_SPRITE_QUAD, SCALES  # noqa: E402
    emu.replace_thiscall(ADD_SPRITE_QUAD, 3, o.add_quad)
    lines.append("// Players {set, view, position, base action, direction, frame, scale, x offset, angle,")
    lines.append("// depth index, key colour, colour, flags 0x80 0x83 0x86, ground colour, head offset before},")
    lines.append("// then the quads {the first 8 values, image type, image, layer, flags} and the head")
    lines.append("// offset after.")
    players = []
    for k in range(320):
        s = k % len(layer_sets)
        view_index = k % len(views)
        (view, scales), at = views[view_index]
        for key, v in zip(SCALES, scales):
            emu.write_u32(SCALES[key], v)
        emu.mem_write(player, bytes(0x400))
        emu.write_u32(player, CPC_VTABLE)
        pos = (f32(at[0] + rng.uniform(-50, 50)), f32(rng.uniform(-20, 5)), f32(at[2] + rng.uniform(-50, 50)))
        base = rng.choice([0, 8, 32, 80])
        direction = rng.randrange(8)
        frame = rng.randrange(0, 4)
        scale = f32(rng.choice([1.0, 1.0, 0.75, 1.5]))
        offset_x, angle, depth_index = rng.choice([0, 0, 5, -9]), rng.choice([0, 0, 15]), rng.randrange(0, 30)
        key_color = rng.choice([0xFFFF0000, 0xFFFF0000, 0xFF00FF00])
        color = rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 0x80FF8040, rng.getrandbits(32)])
        flags = [rng.random() < 0.2, rng.random() < 0.2, rng.random() < 0.15]
        o.ground = tuple(f32(rng.choice([1.0, 1.0, 0.5, 1.3])) for _ in range(3))
        head_before = (rng.randrange(-30, 30), rng.randrange(-30, 30))
        emu.mem_write(HEAD_OFFSET, struct.pack("<ii", *head_before))
        emu.mem_write(player + 4, struct.pack("<3f", *pos))
        emu.write_u32(player + 0x20, base)
        emu.write_u32(player + 0x24, base + direction)
        emu.write_u32(player + 0x28, frame)
        emu.mem_write(player + 0x3C, struct.pack("<f", scale))
        emu.mem_write(player + 0x5C, struct.pack("<iii", offset_x, angle, depth_index))
        emu.write_u32(player + 0x68, key_color)
        emu.write_u32(player + 0x7C, color)
        for offset, on in zip((0x80, 0x83, 0x86), flags):
            emu.mem_write(player + offset, bytes([int(on)]))
        emu.write_u32(player + 0x280, imf_name)
        for layer in range(10):
            emu.write_u32(spr_table + 4 * layer, sprs[layer] if layer < 8 and acts[s][layer] else 0)
            emu.write_u32(act_table + 4 * layer, acts[s][layer] if layer < 8 else 0)
        emu.write_u32(player + 0x2EC, spr_table)
        emu.write_u32(player + 0x2FC, act_table)
        o.quads.clear()
        emu.call(RENDER, emu.scratch(struct.pack("<12I", *view)), 0, this=player)
        head_after = struct.unpack("<ii", emu.mem_read(HEAD_OFFSET, 8))
        name = f"kPlayer{k}"
        lines.append(f"static const QuadVector {name}Quads[] = {{")
        for values, palette, fl in o.quads:
            t, i = records[values[11]]
            lines.append(f"    {{{{{hexs(values[:8])}}}, {t}, {i}, {palettes[palette]}, 0x{fl:x}}},")
        if not o.quads:
            lines.append("    {},")
        lines.append("};")
        players.append(
            f"    {{{s}, {view_index}, {{{hexs(map(bits, pos))}}}, {base}, {direction}, {frame}, "
            f"0x{bits(scale):08x}, {offset_x}, {angle}, {depth_index}, 0x{key_color:08x}, 0x{color:08x}, "
            f"{{{', '.join(str(int(f)) for f in flags)}}}, {{{hexs(map(bits, o.ground))}}}, "
            f"{{{head_before[0]}, {head_before[1]}}}, {name}Quads, {len(o.quads)}, "
            f"{{{head_after[0]}, {head_after[1]}}}}},")
    lines.append("// The views {view matrix, scales}.")
    lines.append("static const ViewVector kViews[] = {")
    for (view, scales), _ in views:
        lines.append(f"    {{{{{hexs(view)}}}, {{{hexs(scales)}}}}},")
    lines.append("};")
    lines.append("static const PlayerVector kPlayers[] = {")
    lines += players
    lines.append("};")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
